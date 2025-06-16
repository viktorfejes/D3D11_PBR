Texture2D scene_tex : register(t0);
SamplerState samp : register(s0);

struct VS_Output {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(VS_Output input) : SV_TARGET {
    const float distortion_strength = 0.3;
    const float scale_factor = 0.25;
    const float aberration_strength = 0.02;

    float2 uv = input.uv;

    float2 centered_uv = uv - 0.5;

    // Calculate aspect ratio
    float aspect_ratio = length(ddx(uv)) / length(ddy(uv));

    // Adjust for aspect ratio to make distortion be correct
    centered_uv.x *= aspect_ratio;

    // Calculate distance from center
    float dist_from_center = length(centered_uv);

    // Apply barrel distortion forumal
    // Higher powers create more corner-focused distortion
    float distortion_factor = 1.0 + distortion_strength * dist_from_center * dist_from_center;

    float scale = 1.0 - distortion_strength * scale_factor;
    centered_uv *= scale;

    // Apply distortion
    float2 distorted_uv = centered_uv * distortion_factor;

    // Adjust back for aspect ratio
    distorted_uv.x /= aspect_ratio;

    // Move back to 0-1 UV space
    distorted_uv += 0.5;

    // Calculate chromatic aberration offset based on distance from center
    float aberration_factor = aberration_strength * dist_from_center * dist_from_center;

    // Calculate direction from center
    float2 aberration_direction = normalize(centered_uv);

    float2 red_uv = distorted_uv + aberration_direction * aberration_factor;
    float2 green_uv = distorted_uv;
    float2 blue_uv = distorted_uv - aberration_direction * aberration_factor * 0.5;

    float r = scene_tex.Sample(samp, red_uv).r;
    float g = scene_tex.Sample(samp, green_uv).g;
    float b = scene_tex.Sample(samp, blue_uv).b;

    return float4(r, g, b, 1.0);
}



