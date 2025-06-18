TextureCube envMap : register(t0);
RWTexture2DArray<float4> irradiance_map : register(u0);

SamplerState samp : register(s0);

cbuffer Constants : register(b0) {
    uint resolution;
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

// https://github.com/David-DiGioia/monet/blob/aaa548acdd0447f65d1598b9434a01d971e10cbc/shaders/cubemap_to_irradiance.frag
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint face = id.z;
    uint2 pixel = id.xy;

    // Early exit if outside texture bounds
    if (pixel.x >= resolution || pixel.y >= resolution || face >= 6) {
        return;
    }

    // Get UV coordinates from pixel position
    float2 uv = (float2(pixel) + 0.5) / float(resolution);
    
    // Get the direction for this pixel (this is our normal)
    float3 normal = getDirection(face, uv);
    
    float3 irradiance = float3(0.0, 0.0, 0.0);
    
    // Create tangent space basis
    float3 up = float3(0.0, 1.0, 0.0);
    float3 right = normalize(cross(up, normal));
    up = cross(normal, right);
    
    float phiDelta = 0.025;
    float thetaDelta = 0.025;
    int nrSamples = 0;
    
    // Sample hemisphere around normal
    for (float phi = 0.0; phi < 2.0 * PI; phi += phiDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += thetaDelta) {
            // Spherical to cartesian (in tangent space)
            float3 tangentSample = float3(
                sin(theta) * cos(phi), 
                sin(theta) * sin(phi), 
                cos(theta)
            );
            
            // Tangent space to world space
            float3 sampleVec = tangentSample.x * right + 
                              tangentSample.y * up + 
                              tangentSample.z * normal;
            
            // Sample environment map and weight by cosine and solid angle
            irradiance += envMap.SampleLevel(samp, sampleVec, 0).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    
    // Normalize the result
    irradiance = PI * irradiance / float(nrSamples);
    
    // Write to output texture
    irradiance_map[uint3(pixel, face)] = float4(irradiance, 1.0);
}
