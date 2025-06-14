Texture2D scene_tex : register(t0);
SamplerState samp : register(s0);

struct VS_Output {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float2 BarrelDistortion(float2 uv, float strength) {
    float2 centered = uv * 2.0 - 1.0;
    float r2 = dot(centered, centered);
    centered *= 1.0 + strength * r2;
    return (centered * 0.5) + 0.5;
}

float2 curve_remap_uv(float2 uv, float curvature) {
    uv = uv * 2.0 - 1.0;
    float2 offset = abs(uv.yx) / curvature;
    uv += uv * offset * offset;
    uv = uv * 0.5 + 0.5;
    return uv;
}

float4 main(VS_Output input) : SV_TARGET {
    const float distortion_strength = 0.5;
    const float aberration_strength = 0.002;

    float2 uv = input.uv;

//  float2 curved_uv = curve_remap_uv(uv, 4.0);

//  // If UV is out of bounds, kill pixel
//  if (curved_uv.x < 0.0 || curved_uv.x > 1.0 || curved_uv.y < 0.0 || curved_uv.y > 1.0) {
//      return float4(0, 0, 0, 1);
//  }

//  float2 red_uv = curved_uv + float2(aberration_strength, 0.0);
//  float2 green_uv = curved_uv;
//  float2 blue_uv = curved_uv - float2(aberration_strength, 0.0);

//  float r = scene_tex.Sample(samp, saturate(red_uv)).r;
//  float g = scene_tex.Sample(samp, saturate(green_uv)).g;
//  float b = scene_tex.Sample(samp, saturate(blue_uv)).b;

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

    // Apply distortion
    float2 distorted_uv = centered_uv * distortion_factor;

    // Adjust back for aspect ratio
    distorted_uv.x /= aspect_ratio;

    // Move back to 0-1 UV space
    distorted_uv += 0.5;

    return float4(scene_tex.Sample(samp, distorted_uv).rgb, 1.0);
}



