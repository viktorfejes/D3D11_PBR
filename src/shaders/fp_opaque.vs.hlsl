cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 view_projection_matrix;
    float3 camera_position;
};

cbuffer PerObjectConstants : register(b1) {
    row_major float4x4 world_matrix;
    row_major float3x3 world_inv_transpose_matrix;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct VS_Input {
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 tex_coord : TEXCOORD;
    float4 tangent   : TANGENT;
};

struct VS_Output {
    float4 pos             : SV_POSITION;
    float2 uv              : TEXCOORD0;
    float3 world_normal    : NORMAL_WS;
    float3 world_position  : WORLD_POSITION;
    float3 camera_position : CAMERA_POS;
    float3x3 TBN           : TBN;
};

VS_Output main(VS_Input input) {
    float4 world_position = mul(float4(input.position, 1.0f), world_matrix);
    float3 world_normal = normalize(mul(input.normal, world_inv_transpose_matrix));
    float3 world_tangent = normalize(mul(input.tangent.xyz, world_inv_transpose_matrix));
    float3 world_bitangent = cross(world_normal, world_tangent) * input.tangent.w;

    VS_Output output;
    output.pos = mul(world_position, view_projection_matrix);
    output.uv = input.tex_coord;
    output.world_normal = world_normal;
    output.world_position = world_position.xyz;
    output.camera_position = camera_position;
    output.TBN = float3x3(world_tangent, world_bitangent, world_normal);

    return output;
};


