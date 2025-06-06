cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 viewProjectionMatrix;
};

cbuffer PerObjectConstants : register(b2) {
    row_major float4x4 worldMatrix;
    row_major float3x3 worldInvTranspose;
    float _padding0;
    float _padding1;
    float _padding2;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 clipSpacePosition    : SV_POSITION;
    float2 texCoord             : TEXCOORD0;
    float3 worldPosition        : POSITION_WS;
    float3 worldNormal          : NORMAL_WS;
    float4 worldTangent         : TANGENT_WS;   // Stores handedness in w component
};

VSOutput main(VSInput input) {
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);
    float3 transformedTangent = normalize(mul(input.tangent.xyz, worldInvTranspose));

    VSOutput output;
    output.clipSpacePosition = mul(worldPos, viewProjectionMatrix);
    output.texCoord = input.texCoord;
    output.worldPosition = worldPos.xyz;
    output.worldNormal = normalize(mul(input.normal, worldInvTranspose));
    output.worldTangent = float4(transformedTangent, input.tangent.w);

    return output;
}

