cbuffer SkyboxCB : register(b0) {
    row_major float4x4 view;
    row_major float4x4 projection;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 texCoord : SAMPLE_DIR;
};

VSOutput main(uint vertexID : SV_VertexID) {
    static const float3 positions[36] = {
        float3(-1.0f, -1.0f, -1.0f),
        float3( 1.0f,  1.0f, -1.0f),
        float3( 1.0f, -1.0f, -1.0f),

        float3( 1.0f,  1.0f, -1.0f),
        float3(-1.0f, -1.0f, -1.0f),
        float3(-1.0f,  1.0f, -1.0f),

        float3(-1.0f, -1.0f,  1.0f),
        float3( 1.0f, -1.0f,  1.0f),
        float3( 1.0f,  1.0f,  1.0f),
        
        float3( 1.0f,  1.0f,  1.0f),
        float3(-1.0f,  1.0f,  1.0f),
        float3(-1.0f, -1.0f,  1.0f),

        float3(-1.0f,  1.0f,  1.0f),
        float3(-1.0f,  1.0f, -1.0f),
        float3(-1.0f, -1.0f, -1.0f),

        float3(-1.0f, -1.0f, -1.0f),
        float3(-1.0f, -1.0f,  1.0f),
        float3(-1.0f,  1.0f,  1.0f),

        float3( 1.0f,  1.0f,  1.0f),
        float3( 1.0f, -1.0f, -1.0f),
        float3( 1.0f,  1.0f, -1.0f),

        float3( 1.0f, -1.0f, -1.0f),
        float3( 1.0f,  1.0f,  1.0f),
        float3( 1.0f, -1.0f,  1.0f),

        float3(-1.0f, -1.0f, -1.0f),
        float3( 1.0f, -1.0f, -1.0f),
        float3( 1.0f, -1.0f,  1.0f),

        float3( 1.0f, -1.0f,  1.0f),
        float3(-1.0f, -1.0f,  1.0f),
        float3(-1.0f, -1.0f, -1.0f),

        float3(-1.0f,  1.0f, -1.0f),
        float3( 1.0f,  1.0f,  1.0f),
        float3( 1.0f,  1.0f, -1.0f),

        float3( 1.0f,  1.0f,  1.0f),
        float3(-1.0f,  1.0f, -1.0f),
        float3(-1.0f,  1.0f,  1.0f),
    };

    float3 local_position = positions[vertexID];

    // Remove translation from view matrix
    row_major float3x3 rot_view = (float3x3)view;

    float3 rotated_pos = mul(local_position, rot_view);
    float4 clip_pos = mul(float4(rotated_pos, 1.0f), projection);

    VSOutput o;
    o.position = clip_pos.xyww;
    o.texCoord = float3(-local_position.x, local_position.y, local_position.z);
    return o;
}
