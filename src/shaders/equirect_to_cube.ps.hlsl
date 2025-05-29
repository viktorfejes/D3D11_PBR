struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Pixel Shader
Texture2D equirectMap : register(t0);
SamplerState samp : register(s0);

cbuffer FaceIndexCB : register(b0) {
    uint faceIndex;
};

#define PI 3.14159265359

float3 getDirectionForFace(uint faceIndex, float2 uv) {
    // Remap uv to [-1, 1]
    uv = uv * 2.0 - 1.0;

    float3 dir = 0;
    switch (faceIndex) {
        case 0: dir = float3(1.0, uv.y, -uv.x); break;
        case 1: dir = float3(-1.0, uv.y, uv.x); break;
        case 2: dir = float3(uv.x, -1.0f, uv.y); break;
        case 3: dir = float3(uv.x, 1.0, -uv.y); break;
        case 4: dir = float3(uv.x, uv.y, 1.0); break;
        case 5: dir = float3(-uv.x, uv.y, -1.0); break;
    }
    return normalize(dir);
}

float3 sampleEquirectangular(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.z, dir.x) / (2.0 * PI) + 0.5;
    uv.y = asin(clamp(dir.y, -1.0, 1.0)) / PI + 0.5;
    return equirectMap.Sample(samp, uv).rgb;
}

float4 main(VSOutput input) : SV_Target {
    float3 dir = getDirectionForFace(faceIndex, input.uv);
    return float4(sampleEquirectangular(dir), 1.0);
}

