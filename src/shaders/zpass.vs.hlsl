cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 viewProjectionMatrix;
    float3 cameraPosition;
};

cbuffer PerObjectConstants : register(b1) {
    row_major float4x4 worldMatrix;
    row_major float3x3 worldInvTransposeMatrix;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct VS_Input {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangent : TANGENT;
};
struct VS_Output {
    float4 pos : SV_POSITION;
};

VS_Output main(VS_Input input) {
    VS_Output output;
    output.pos = mul(mul(float4(input.position, 1.0f), worldMatrix), viewProjectionMatrix);
    return output;
};


