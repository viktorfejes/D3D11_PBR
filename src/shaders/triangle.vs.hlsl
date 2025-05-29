struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

static const float2 pos[3] = {
    float2(-1.0, -1.0),
    float2(-1.0,  3.0),
    float2( 3.0, -1.0)
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput o;
    o.position = float4(pos[vertexId], 0.0, 1.0);
    o.uv = (pos[vertexId] + 1.0) * 0.5;
    o.uv.y = 1.0 - o.uv.y;
    return o;
}
