Texture2D source_tex : register(t0);
SamplerState samp : register(s0);

   cbuffer PerFrameConstants : register(b0) {
       row_major float4x4 view_matrix;
       row_major float4x4 projection_matrix;
       row_major float4x4 view_projection_matrix;
       row_major float4x4 inv_view_projection_matrix;
       float3 camera_position;
       float _padding;
   };

cbuffer BloomConstants : register(b1) {
    float2 texel_size;
    float bloom_threshold;
    float bloom_intensity;
    float bloom_knee;
    float bloom_mip_strength;
    float2 padding;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 threshold_main(PSInput input) : SV_Target {
    float3 color = source_tex.Sample(samp, input.uv).rgb;
    float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
    
    float soft = luminance - bloom_threshold + bloom_knee;
    soft = clamp(soft, 0.0, 2.0 * bloom_knee);
    soft = soft * soft / (4.0 * bloom_knee + 0.00001);

    float contribution = max(soft, luminance - bloom_threshold) / max(luminance, 0.00001);
    return float4(color * contribution, 1.0f);
}

float4 downsample_main(PSInput input) : SV_Target {
    float3 result = float3(0.0, 0.0, 0.0);

//    float2 offset = texel_size * 0.5;
//    result += source_tex.Sample(samp, input.uv + float2(-offset.x, -offset.y)).rgb;
//    result += source_tex.Sample(samp, input.uv + float2(offset.x, -offset.y)).rgb;
//    result += source_tex.Sample(samp, input.uv + float2(-offset.x, offset.y)).rgb;
//    result += source_tex.Sample(samp, input.uv + float2(offset.x, offset.y)).rgb;


    // Sample 3x3 neighbourhood using 9 taps
    float2 offsets[9] = {
        float2(-1, -1), float2(0, -1), float2(1, -1),
        float2(-1, 0), float2(0, 0), float2(1, 0),
        float2(-1, 1), float2(0, 1), float2(1, 1),
    };

    // Predivided weights
    float weights[9] = {
        0.0625, 0.125, 0.0625,
        0.125, 0.25, 0.125,
        0.0625, 0.125, 0.0625,
    };

    for (int i = 0; i < 9; ++i) {
        float2 sample_uv = input.uv + offsets[i] * texel_size;
        result += source_tex.Sample(samp, sample_uv).rgb * weights[i];
    }

    return float4(result, 1.0f);
}

float4 upsample_main(PSInput input) : SV_Target {
    float3 result = float3(0.0, 0.0, 0.0);

//  float2 offset = texel_size * 0.5;
//  result += source_tex.Sample(samp, input.uv + float2(-offset.x, -offset.y)).rgb;
//  result += source_tex.Sample(samp, input.uv + float2(offset.x, -offset.y)).rgb;
//  result += source_tex.Sample(samp, input.uv + float2(-offset.x, offset.y)).rgb;
//  result += source_tex.Sample(samp, input.uv + float2(offset.x, offset.y)).rgb;

    float2 offsets[9] = {
        float2(-1, -1), float2(0, -1), float2(1, -1),
        float2(-1, 0), float2(0, 0), float2(1, 0),
        float2(-1, 1), float2(0, 1), float2(1, 1),
    };

    // Predivided weights
    float weights[9] = {
        0.0625, 0.125, 0.0625,
        0.125, 0.25, 0.125,
        0.0625, 0.125, 0.0625,
    };

    for (int i = 0; i < 9; ++i) {
        float2 sample_uv = input.uv + offsets[i] * texel_size;
        result += source_tex.Sample(samp, sample_uv).rgb * weights[i];
    }

    return float4(result  * bloom_intensity * bloom_mip_strength, 1.0f);
}
