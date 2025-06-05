// Textures
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D ORMTexture    : register(t2);

// Samplers
SamplerState linearSampler : register(s0);

cbuffer PerMaterialConstants : register(b0) {
    float3 albedoColor;
    float metallicValue;
    float roughnessValue;
    float3 emissionColor;
};

// Incoming data from vertex shader
struct VSOutput {
    float4 clipSpacePosition : SV_POSITION;
    float2 texCoord          : TEXCOORD0;
    float3 worldPosition     : POSITION_WS;
    float3 worldNormal       : NORMAL_WS;
    float4 worldTangent      : TANGENT_WS;   // Stores handedness in w component
};

// Output with multiple targets
struct PSOutput {
    float4 rt0 : SV_Target0;
    float4 rt1 : SV_Target1;
    float4 rt2 : SV_Target2;
};

PSOutput main(VSOutput input) {
    float2 uv = input.texCoord;

    float3 albedoTex = albedoTexture.Sample(linearSampler, uv).rgb;
    float3 normalTex = normalTexture.Sample(linearSampler, uv).xyz * 2.0 - 1.0; // Decode from [0,1] to [-1,1]
    float4 ORM = ORMTexture.Sample(linearSampler, uv);

    float roughness = roughnessValue * ORM.g;
    float metallic = metallicValue * ORM.b;
    float ao = ORM.r;

    // Reconstruct TBN matrix for normal mapping
    float3 N = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent.xyz);
    float3 B = cross(N, T) * input.worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);

    // Transform normal from tangent space to world space
    float3 worldNormalMap = normalize(mul(normalTex, TBN));

    // Encode world normal for storage
    float3 encodedNormal = worldNormalMap * 0.5 + 0.5;

    PSOutput output;
    // Albedo (RGB) + Roughness (A)
    output.rt0 = float4(albedoColor * albedoTex, roughness);
    // World-space normal (RGB) + Metallic (A)
    output.rt1 = float4(encodedNormal, metallic);
    // Emission color (RGB) + AO (A)
    output.rt2 = float4(emissionColor, ao);

    return output;
}
