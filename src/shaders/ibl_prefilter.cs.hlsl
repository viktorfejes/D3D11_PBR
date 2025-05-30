TextureCube env_map : register(t0);
RWTexture2DArray<float4> out_cube_mips : register(u0);
SamplerState samp : register(s0);

cbuffer PrefilterParams : register(b0) {
    uint current_mip;
    uint total_mip;
    float roughness;
    uint num_samples;
};

#define PI 3.14159265359
#define INV_PI (1 / PI)

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    float phi = 2.0 * 3.14159265 * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // From spherical coordinates to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // From tangent-space vector to world-space sample vector
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

// Van Der Corpus sequence for low-discrepancy sampling
float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

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

[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID) {
    // TODO: Should be passed in?
    uint face_size = 256u >> current_mip;

    if (id.x >= face_size || id.y >= face_size) {
        return;
    }

    // Calculate UV coordinates
    float2 uv = (float2(id.xy) + 0.5) / float(face_size);

    // Get normal direction for this texel
    float3 N = getDirection(id.z, uv);
    float3 R = N;   // For specular, reflection vector equals normal
    float3 V = R;   // View vector equals reflection for pre-filtering

    float3 prefiltered_color = float3(0.0, 0.0, 0.0);
    float total_weight = 0.0;

    if (current_mip == 0) {
        // For perfect mirror reflection, just sample directly
        prefiltered_color = env_map.SampleLevel(samp, N, 0).rgb;
    } else {
        // Sample environment map
        for (uint i = 0u; i < num_samples; ++i) {
            float2 Xi = Hammersley(i, num_samples);
            float3 H = ImportanceSampleGGX(Xi, N, roughness);
            float3 L = normalize(2.0 * dot(V, H) * H - V);

            float NdotL = max(dot(N, L), 0.0);
            if (NdotL > 0.0) {
                // Calculate weights for importance sampling
                float NdotH = max(dot(N, H), 0.0);
                float HdotV = max(dot(H, V), 0.0);
                float D = DistributionGGX(N, H, roughness);
                float pdf = D * NdotH / (4.0 * HdotV) + 0.0001;

                // Solid angle of current sample
                float sa_texel = 4.0 * PI / (6.0 * face_size * face_size);
                float sa_sample = 1.0 / (float(num_samples) * pdf + 0.0001);

                // Mip level for filtering
                float mip_level = max(0.0, 0.5 * log2(sa_sample / sa_texel));

                prefiltered_color += env_map.SampleLevel(samp, L, mip_level).rgb * NdotL;
                total_weight += NdotL;
            }
        }

        if (total_weight > 0.0) {
            prefiltered_color /= total_weight;
        }
    }

    // Write to output cubemap array (face = id.z)
    uint3 out_coord = uint3(id.x, id.y, id.z);
    out_cube_mips[out_coord] = float4(prefiltered_color, 1.0);
}



