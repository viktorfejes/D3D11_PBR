TextureCube skybox_tex : register(t0);
SamplerState samp : register(s0);

struct VSOutput {
    float4 position : SV_POSITION;
    float3 texCoord : SAMPLE_DIR;
};

float4 main(VSOutput input) : SV_Target {
    return skybox_tex.Sample(samp, normalize(input.texCoord));
}
