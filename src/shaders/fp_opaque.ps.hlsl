// IBL Related Textures
TextureCube ibl_irradiance_tex : register(t0);
TextureCube ibl_prefilter_tex  : register(t1);
Texture2D ibl_brdf_lut         : register(t2);

// Material Textures
Texture2D albedo_tex    : register(t3);
Texture2D metallic_tex  : register(t4);
Texture2D roughness_tex : register(t5);
Texture2D normal_tex    : register(t6);
Texture2D emission_tex  : register(t7);

// Sampler for textures
SamplerState linear_sampler : register(s0);

cbuffer PerMaterialConstants : register(b0) {
    float3 albedo_color;
    float metallic_value;
    float roughness_value;
    float emission_intensity;
};

struct VS_Output {
    float4 position        : SV_POSITION;
    float2 uv              : TEXCOORD0;
    float3 world_normal    : NORMAL_WS;
    float3 world_position  : WORLD_POSITION;
    float3 camera_position : CAMERA_POS;
    float3x3 TBN           : TBN;
};

#define PI 3.14159265359
#define INV_PI (1 / PI)

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float G_V = NdotV / (NdotV * (1.0 - k) + k);
    float G_L = NdotL / (NdotL * (1.0 - k) + k);

    return G_V * G_L;
}

float3 GammaCorrect(float3 color) {
    return pow(color, 1.0 / 2.2);
}

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
float G_SmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a2 = roughness * roughness * roughness * roughness;
    float GGXL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
    float GGXV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    return 0.5 / (GGXV + GGXL);
}

float V_SmithGGXCorrelatedFast(float NoV, float NoL, float roughness) {
    float a = roughness * roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

float Fd_Lambert() {
    return INV_PI;
}

float3 Fd_Burley(float NdotL, float NdotV, float LdotH, float roughness) {
    float F90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float3 FL = F_Schlick(NdotL, float3(1.0, 1.0, 1.0), float3(F90, F90, F90));
    float3 FV = F_Schlick(NdotV, float3(1.0, 1.0, 1.0), float3(F90, F90, F90));
    return FL * FV * INV_PI;
}

float3 F_Sheen(float LdotH, float3 sheenColor) {
    return sheenColor * pow(1 - LdotH, 5);
}

float4 main(VS_Output input) : SV_TARGET {
    // TODO: Hardcoding values for coat for now.
    float clear_coat = 0.0;
    float cc_roughness = 0.01;
    /*-----------------------------------------------------------*/

    float2 uv = input.uv;

    // ===========================================================
    // Sample Textures
    // ===========================================================
    float3 albedo = albedo_color * albedo_tex.Sample(linear_sampler, uv).rgb;
    float metallic = metallic_value * metallic_tex.Sample(linear_sampler, uv).r;
    float roughness = roughness_value * roughness_tex.Sample(linear_sampler, uv).r;
    float3 emission = emission_tex.Sample(linear_sampler, uv).rgb;
    float3 normal = normal_tex.Sample(linear_sampler, uv).xyz;

    // Base reflectance
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    // View vector
    float3 V = normalize(input.camera_position - input.world_position);
    // Base normal
    float3 N = normalize(mul(normal * 2.0f - 1.0f, input.TBN));
    // Reflection vector
    float3 R = reflect(-V, N);

    float NdotV = abs(dot(N, V)) + 1e-5;

    // ===========================================================
    // IBL
    // ===========================================================
    float ibl_exposure_ev = -1.0f; // TODO: not hardcode!
    float max_reflection_LOD = 4.0; // TODO: not hardcode?
    float mip_level = roughness * max_reflection_LOD;

    // Sample IBL textures
    float3 irradiance = ibl_irradiance_tex.Sample(linear_sampler, N).rgb;
    float3 prefiltered_color = ibl_prefilter_tex.SampleLevel(linear_sampler, R, mip_level).rgb;
    float2 brdf_sample = ibl_brdf_lut.Sample(linear_sampler, float2(NdotV, roughness)).rg;

    // Specular IBL
    float3 F_ibl = F0 * brdf_sample.x + (1.0 - F0) * brdf_sample.y;

    // Energy conservation
    float3 kS = F_ibl;
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    kD *= 1.0 - metallic;

    float3 diffuse_ibl = kD * albedo * irradiance;
    float3 specular_ibl = prefiltered_color * F_ibl;
    
    /*-----------------------------------------------------------*/
    // Coat implementation
    float mip_level_cc = cc_roughness * max_reflection_LOD;
    float3 prefiltered_cc = ibl_prefilter_tex.SampleLevel(linear_sampler, R, mip_level_cc).rgb;
    float Fc = clear_coat * (0.04 + (1.0 - 0.04) * pow(1.0 - NdotV, 5.0));
    float3 specular_coat = prefiltered_cc * Fc;
    // Account for coat
    diffuse_ibl *= (1.0 - Fc);
    specular_ibl *= (1.0 - Fc);
    /*-----------------------------------------------------------*/

    float3 ibl_lighting = exp2(ibl_exposure_ev) * (diffuse_ibl + specular_ibl + specular_coat);
    float3 Lo = emission + ibl_lighting;

    return float4(Lo, 1.0f);
}



