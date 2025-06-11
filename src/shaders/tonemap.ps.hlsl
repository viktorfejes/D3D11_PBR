Texture2D scene_color : register(t0);
Texture2D bloom_tex : register(t1);
SamplerState samp : register(s0);

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Fast approximation - most performant
float3 LinearToSRGB_Fast(float3 color) {
    return pow(saturate(color), 1.0f / 2.2f);
}

// Optimized accurate conversion with branch removal
float3 LinearToSRGB_Optimized(float3 color) {
    color = saturate(color);
    
    // Use step() to avoid branching - GPU friendly
    float3 selector = step(0.0031308f, color);
    
    // Linear portion: color * 12.92
    float3 linear_part = color * 12.92f;
    
    // Gamma portion: 1.055 * pow(color, 1/2.4) - 0.055
    float3 gamma_part = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    
    // Blend between linear and gamma using selector
    return lerp(linear_part, gamma_part, selector);
}

float3 LinearToSRGB_Polynomial(float3 color) {
    color = saturate(color);

    // Optimized polynomial approximation that's very close to correct sRGB
    // Coefficients chosen for minimal error in typical color ranges
    float3 S1 = sqrt(color);
    float3 S2 = sqrt(S1);
    float3 S3 = sqrt(S2);

    return 0.585122381f * S1 + 0.783140355f * S2 - 0.368262736f * S3;
}

float3 ReinhardExtended(float3 color, float max_white) {
    float3 numerator = color * (1.0f + (color / (max_white * max_white)));
    return numerator / (1.0f + color);
}

float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 Uncharted2Tonemap(float3 x) {
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2(float3 color) {
    float W = 11.2f; // White point
    color = Uncharted2Tonemap(color * 2.0f);
    float3 whiteScale = 1.0f / Uncharted2Tonemap(W);
    return color * whiteScale;
}

float4 main(PSInput input) : SV_TARGET {
    float3 tex = scene_color.Sample(samp, input.uv).rgb;
    float3 b_tex = bloom_tex.Sample(samp, input.uv).rgb;
    float3 composite = tex + b_tex;
    return float4(LinearToSRGB_Polynomial(ACESFilm(composite)), 1.0f);
}


