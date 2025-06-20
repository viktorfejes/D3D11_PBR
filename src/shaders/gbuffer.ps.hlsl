// Textures
Texture2D albedoTexture : register(t0);
Texture2D metallicTexture : register(t1);
Texture2D roughnessTexture : register(t2);
Texture2D coatTexture : register(t3);
Texture2D normalTexture : register(t4);
Texture2D emissionTexture : register(t5);

// Samplers
SamplerState linearSampler : register(s0);

cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 view_matrix;
    row_major float4x4 projection_matrix;
    row_major float4x4 view_projection_matrix;
    row_major float4x4 inv_view_projection_matrix;
    float3 camera_position;
    float _padding;
};

cbuffer PerMaterialConstants : register(b1) {
    float3 albedoColor;
    float metallicValue;
    float roughnessValue;
    float coatValue;
    float emissionIntensity;
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

    float roughness = roughnessValue * roughnessTexture.Sample(linearSampler, uv).r;
    float metallic = metallicValue * metallicTexture.Sample(linearSampler, uv).r;
    float coat = coatValue * coatTexture.Sample(linearSampler, uv).r;

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
    // World-space normal (RGB)
    output.rt1 = float4(encodedNormal, coat);
    // Emission color (RGB) + Metallic (A)
    output.rt2 = float4(emissionIntensity * emissionTexture.Sample(linearSampler, uv).rgb, metallic);

    return output;
}
