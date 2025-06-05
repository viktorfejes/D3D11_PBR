cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 viewProjectionMatrix;
    float3 cameraPosition;
};

cbuffer PerObjectConstants : register(b2) {
    row_major float4x4 worldMatrix;
};

struct VS_Input {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
    float4 tangent : TANGENT;
};
struct VS_Output {
    float4 pos : SV_POSITION;
    float3 NormalWS : NORMAL_WS;
    float2 TexCoord : TEXCOORD;
    float3 cameraPosition : CAMERA_POS;
    float3 WorldPos : WORLD_POSITION;
    float3x3 TBN : TBN;
};

VS_Output main(VS_Input input) {
    VS_Output output = (VS_Output)0;
    float4x4 mvpMatrix = mul(worldMatrix, viewProjectionMatrix);
    output.pos = mul(float4(input.position, 1.0f), mvpMatrix);
    output.NormalWS = normalize(mul(input.normal, (float3x3)worldMatrix));
    output.TexCoord = input.texCoord;
    output.cameraPosition = cameraPosition;
    // TODO: see if it can be done easier
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);
    output.WorldPos = worldPos.xyz;

    float3 N = normalize(mul(input.normal, (float3x3)worldMatrix));
    float3 T = normalize(mul(input.tangent.xyz, (float3x3)worldMatrix));
    float3 B = normalize(cross(N, T));
    output.TBN = float3x3(T, B, N);

    return output;
};


