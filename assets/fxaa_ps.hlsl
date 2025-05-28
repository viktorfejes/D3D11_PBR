Texture2D scene_tex : register(t0);
SamplerState samp : register(s0);

cbuffer FXAAConstants : register(b0) {
    float2 texel_size;
    float2 padding;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target {
    float2 uv = input.uv;

    // Sample neighborhood
    float3 rgbNW = scene_tex.Sample(samp, uv + texel_size * float2(-1.0, -1.0)).rgb;
    float3 rgbNE = scene_tex.Sample(samp, uv + texel_size * float2(1.0, -1.0)).rgb;
    float3 rgbSW = scene_tex.Sample(samp, uv + texel_size * float2(-1.0, 1.0)).rgb;
    float3 rgbSE = scene_tex.Sample(samp, uv + texel_size * float2(1.0, 1.0)).rgb;
    float3 rgbM = scene_tex.Sample(samp, uv).rgb;

    float3 luma_weights = float3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma_weights);
    float lumaNE = dot(rgbNE, luma_weights);
    float lumaSW = dot(rgbSW, luma_weights);
    float lumaSE = dot(rgbSE, luma_weights);
    float lumaM = dot(rgbM, luma_weights);

    float luma_min = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float luma_max = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = luma_max - luma_min;
    if (range < 0.02) {
        // early exit, no edge
        return float4(rgbM, 1.0);
    }

    // Edge direction
    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float2 offset = normalize(dir) * 0.5;
    float3 rgb_a = scene_tex.Sample(samp, uv + offset * texel_size).rgb;
    float3 rgb_b = scene_tex.Sample(samp, uv - offset * texel_size).rgb;

#if 1
    return float4((rgb_a + rgb_b) * 0.5, 1.0f);
#else
    if (range >= 0.02)
        return float4(1, 0, 1, 1); // show magenta for FXAA pixels
    else
        return float4(rgbM, 1.0); // normal otherwise
#endif
}



