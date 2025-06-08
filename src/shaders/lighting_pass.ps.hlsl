// G-Buffer textures
Texture2D albedo_roughness : register(t0);
Texture2D world_normal : register(t1);
Texture2D emission_metallic : register(t2);

// Depth texture
Texture2D depth_tex : register(t3);

// IBL
TextureCube irradiance_tex : register(t4);
TextureCube prefilter_map : register(t5);
Texture2D brdf_lut : register(t6);

// Sampler for textures
SamplerState samp : register(s0);

cbuffer CBLighting : register(b0) {
    row_major float4x4 inv_view_projection;
    float3 camera_position;
    float _pad0;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;

    // =======================================================
    // Reconstruct World Position
    // =======================================================
    float z = depth_tex.Sample(samp, uv).r;

    // Convert UV to NDC
    float2 ndc;
    ndc.x = uv.x * 2.0 - 1.0;
    ndc.y = (1.0 - uv.y) * 2.0 - 1.0;
    float4 clip_position = float4(ndc, z, 1.0);

    float4 world_pos_h = mul(clip_position, inv_view_projection);
    float3 world_position = world_pos_h.xyz / world_pos_h.w;

    // =======================================================
    // Sample G-buffer
    // =======================================================
    float3 normal = normalize(world_normal.Sample(samp, uv).xyz * 2.0 - 1.0);
    float3 albedo = albedo_roughness.Sample(samp, uv).rgb;
    float3 emission = emission_metallic.Sample(samp, uv).rgb;
    float metallic = emission_metallic.Sample(samp, uv).a;
    float roughness = albedo_roughness.Sample(samp, uv).a;

    // View Vector
    float3 V = normalize(camera_position - world_position);
    // Reflection vector for IBL
    float3 R = reflect(-V, normal);

    float NdotV = abs(dot(normal, V)) + 1e-5;

    // Base reflectance
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // IBL Exposure EV
    float ibl_exposure_ev = -1.0f;

    // =======================================================
    // DIRECT LIGHTING
    // =======================================================

    // =======================================================
    // INDIRECT LIGHTING
    // =======================================================
    float3 irradiance = irradiance_tex.Sample(samp, normal).rgb;

    float max_reflection_LOD = 4.0;
    float mip_level = roughness * max_reflection_LOD;
    float3 prefiltered_color = prefilter_map.SampleLevel(samp, R, mip_level).rgb;

    // Fresnel for IBL
    float2 brdf_sample = brdf_lut.Sample(samp, float2(NdotV, roughness)).rg;
    float3 F_ibl = F0 * brdf_sample.x + (1.0 - F0) * brdf_sample.y;

    // Energy conservation
    float3 kS = F_ibl;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;

    // Combine diffuse and specular IBL
    float3 diffuse_ibl = kD * albedo * irradiance;
    float3 specular_ibl = prefiltered_color * F_ibl;
    float3 indirect_lighting = exp2(ibl_exposure_ev) * (diffuse_ibl + specular_ibl);

    float3 Lo = emission + indirect_lighting;

    return float4(Lo, 1.0f);
}


