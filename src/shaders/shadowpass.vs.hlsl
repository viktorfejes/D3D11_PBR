cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 view_matrix;
    row_major float4x4 projection_matrix;
    row_major float4x4 view_projection_matrix;
    row_major float4x4 inv_view_projection_matrix;
    float3 camera_position;
    float _padding;
};

cbuffer PerObjectConstants : register(b1) {
    row_major float4x4 worldMatrix;
    row_major float3x3 worldInvTranspose;
    float _padding0;
    float _padding1;
    float _padding2;
};

cbuffer CBShadowPass : register(b2) {
    row_major float4x4 light_view_projection_matrix;
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
};

VSOutput main(VSInput input) {
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);

    VSOutput output;
    output.clipSpacePosition = mul(worldPos, light_view_projection_matrix);
    output.texCoord = input.texCoord;

    return output;
}

