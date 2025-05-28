struct VSInput {
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOuput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOuput main(VSInput input) {
    VSOuput output;
    output.pos = float4(input.pos, 0.0f, 1.0f); // Z = 0, W = 1
    output.uv = input.uv;
    return output;
}
