TextureCube envMap : register(t0);
SamplerState samp : register(s0);

cbuffer FaceIndexCB : register(b0) {
    uint faceIndex;
};

#define PI 3.14159265359

// Directions for each cubemap face
float3 getDirection(uint faceIndex, float2 uv) {
    uv = uv * 2.0 - 1.0;
    float3 dir = 0;
    switch (faceIndex) {
        case 0: dir = float3(1.0, -uv.y, -uv.x); break;  // +X
        case 1: dir = float3(-1.0, -uv.y, uv.x); break;  // -X
        case 2: dir = float3(uv.x, 1.0, uv.y); break;    // +Y
        case 3: dir = float3(uv.x, -1.0, -uv.y); break;  // -Y
        case 4: dir = float3(uv.x, -uv.y, 1.0); break;   // +Z
        case 5: dir = float3(-uv.x, -uv.y, -1.0); break; // -Z
    }
    return normalize(dir);
}

// Van der Corput sequence for better sample distribution
float radicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 hammersley(uint i, uint N) {
    return float2(float(i) / float(N), radicalInverse_VdC(i));
}

// Importance sampling for lambertian BRDF (cosine-weighted hemisphere)
float3 importanceSampleGGX(float2 Xi, float3 N) {
    // For lambertian BRDF, we want cosine-weighted hemisphere sampling
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt(Xi.y);
    float sinTheta = sqrt(1.0 - Xi.y);

    // Spherical to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Tangent space to world space
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    return tangent * H.x + bitangent * H.y + N * H.z;
}

float3 irradiance(float3 N) {
    float3 irradiance = 0.0;
    const uint SAMPLE_COUNT = 1024; // Increased for better quality
    
    for (uint i = 0; i < SAMPLE_COUNT; ++i) {
        float2 Xi = hammersley(i, SAMPLE_COUNT);
        float3 L = importanceSampleGGX(Xi, N);
        
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            irradiance += envMap.SampleLevel(samp, L, 0).rgb * NdotL;
        }
    }
    
    // For cosine-weighted sampling, the PDF is NdotL/PI
    // The integral of the rendering equation for lambertian BRDF becomes:
    // irradiance = (1/N) * sum(L(wi) * NdotL / (NdotL/PI)) = PI * (1/N) * sum(L(wi))
    return PI * irradiance / float(SAMPLE_COUNT);
}

float4 main(float4 pos : SV_POSITION) : SV_Target {
    // Get viewport dimensions from system values if available
    // Otherwise, you'll need to pass this as a constant buffer
    float2 uv = pos.xy / 32.0; // You mentioned this is hardcoded
    float3 dir = getDirection(faceIndex, uv);
    return float4(irradiance(dir), 1.0);
}
