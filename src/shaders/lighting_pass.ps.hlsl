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

// Shadow Atlas
Texture2D shadow_atlas : register(t7);

// Lights array
struct Light {
    float3 direction;
    float intensity;
    row_major float4x4 light_vp_matrix;
    float4 uv_rect;
};
StructuredBuffer<Light> lights : register(t8);

// Sampler for textures
SamplerState samp : register(s0);

cbuffer PerFrameConstants : register(b0) {
    row_major float4x4 view_matrix;
    row_major float4x4 projection_matrix;
    row_major float4x4 view_projection_matrix;
    row_major float4x4 inv_view_projection_matrix;
    float3 camera_position;
    float _padding;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

#define PI 3.14159265359
#define INV_PI (1 / PI)

float D_GGX(float NdotH, float roughness) {
    float a2 = roughness * roughness * roughness * roughness;
    float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * f * f);
}

float3 F_Schlick(float cosTheta, float3 F0, float3 F90) {
    float f = saturate(1.0 - cosTheta);
    float f2 = f * f;
    float fresnel = f2 * f2 * f; // pow(1.0f - cosTheta, 5.0f) for ~2 less instructions
    return F0 + (F90 - F0) * fresnel;
}

// This already contains the G / (4 N·V N·L) term
float G_smith_ggx_correlated(float NdotV, float NdotL, float roughness) {
    float a2 = roughness * roughness * roughness * roughness;
    float GGXL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
    float GGXV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert() {
    return INV_PI;
}

float compute_shadow(Texture2D shadow_atlas, SamplerState samp, float4 lightspace_pos) {
    float3 ndc = lightspace_pos.xyz / lightspace_pos.w;
    float2 uv = ndc.xy * 0.5f + 0.5f;
    float depth = ndc.z * 0.5f + 0.5f;

    float shadow = 0.0;
    float texel_size = 1.0 / 1024;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y) * texel_size;
            float sample = shadow_atlas.Sample(samp, uv + offset).r;
            shadow += (depth - 0.001 < sample) ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

float4 main(PSInput input) : SV_TARGET {
    // Some hardcoded value for now for coat
    float clear_coat = 0.0;
    float cc_roughness = 0.03;
    /*------------------------------------------------------*/
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

    float4 world_pos_h = mul(clip_position, inv_view_projection_matrix);
    float3 world_position = world_pos_h.xyz / world_pos_h.w;

    // =======================================================
    // Sample G-buffer
    // =======================================================
    float3 normal = normalize(world_normal.Sample(samp, uv).xyz * 2.0 - 1.0);
    float3 albedo = albedo_roughness.Sample(samp, uv).rgb;
    float3 emission = emission_metallic.Sample(samp, uv).rgb;
    float metallic = emission_metallic.Sample(samp, uv).a;
    float roughness = max(albedo_roughness.Sample(samp, uv).a, 0.04);

    // View Vector
    float3 V = normalize(camera_position - world_position);
    // Reflection vector for IBL
    float3 R = reflect(-V, normal);

    float NdotV = abs(dot(normal, V)) + 1e-5;

    // Base reflectance
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // IBL Exposure EV
    float ibl_exposure_ev = -10.0f;

    // =======================================================
    // DIRECT LIGHTING
    // =======================================================
    float3 direct_lighting = float3(0, 0, 0);
    // HACK: Hardcoded max lights...
    for (int i = 0; i < 1; ++i) {
        Light light = lights[i];

        float3 L = normalize(-light.direction);
        float3 H = normalize(V + L);

        float NdotL = saturate(dot(normal, L));
        float NdotH = saturate(dot(normal, H));
        float LdotH = saturate(dot(L, H));

        // Evaluate BRDF
        float D = D_GGX(NdotH, roughness);
        float3 F = F_Schlick(LdotH, F0, float3(1.0, 1.0, 1.0));
        float G = G_smith_ggx_correlated(NdotV, NdotL, roughness);
        float3 Fr = (D * G) * F;
        float3 Fd = albedo * Fd_Lambert();
        float3 BRDF = (1.0 - metallic) * Fd + Fr;

        // TODO: Add Clear Coat here as well

        // TODO: Shadow computation
        float4 lightspace_pos = mul(float4(world_position, 1.0), light.light_vp_matrix);
        float shadow = compute_shadow(shadow_atlas, samp, lightspace_pos);

        // HACK: Hardcoded color (the one after BRDF)
        direct_lighting += BRDF * float3(1.0, 0.0, 0.0) * light.intensity * NdotL * shadow;
    }

    // =======================================================
    // INDIRECT LIGHTING
    // =======================================================
    float3 irradiance = irradiance_tex.Sample(samp, float3(-normal.x, normal.y, normal.z)).rgb;

    float max_reflection_LOD = 4.0; // This is mip - 1 iirc
    float mip_level = roughness * max_reflection_LOD;
    float3 prefiltered_color = prefilter_map.SampleLevel(samp, float3(-R.x, R.y, R.z), mip_level).rgb;

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

    /*------------------------------------------------------*/
    // Hopefully, coat?
    float mip_level_cc = cc_roughness * max_reflection_LOD;
    float3 prefiltered_cc = prefilter_map.SampleLevel(samp, float3(-R.x, R.y, R.z), mip_level_cc).rgb;
    float Fc = clear_coat * (0.04 + (1.0 - 0.04) * pow(1.0 - NdotV, 5.0));
    float3 specular_coat = prefiltered_cc * Fc;
    // Account for coat?
    diffuse_ibl *= (1.0 - Fc);
    specular_ibl *= (1.0 - Fc);
    /*------------------------------------------------------*/

    float3 indirect_lighting = exp2(ibl_exposure_ev) * (diffuse_ibl + specular_ibl + specular_coat);

    float3 Lo = emission + direct_lighting + indirect_lighting;

    return float4(Lo, 1.0f);
}



