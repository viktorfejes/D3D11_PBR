Texture2D albedoTex : register(t0);
Texture2D metallicTex : register(t1);
Texture2D roughnessTex : register(t2);
Texture2D normalTex : register(t3);
Texture2D emissionTex : register(t4);
SamplerState samp : register(s0);

cbuffer PerMaterialConstants : register(b0) {
    float3 albedo;
    float metallic;
    float roughness;
    float3 emission_color;
};

struct PS_Input {
    float4 pos : SV_POSITION;
    float3 NormalWS : NORMAL_WS;
    float2 TexCoord : TEXCOORD;
    float4 color : COLOR;
    float3 cameraPosition : CAMERA_POS;
    float3 WorldPos : WORLD_POSITION;
    float3x3 TBN : TBN;
};

#define PI 3.14159265359
#define INV_PI (1 / PI)

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
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

float4 main(PS_Input input) : SV_TARGET {
 // float3 albedoSample = albedo * albedoTex.Sample(samp, input.TexCoord).rgb;

 // float3 N_tangent_raw = normalTex.Sample(samp, input.TexCoord).xyz;
 // float3 N = normalize(mul(N_tangent_raw * 2.0f - 1.0f, input.TBN));
 // // N.y *= -1.0f; -- This is in case we need to flip the normals

 // float3 V = normalize(input.cameraPosition - input.WorldPos);
 // float3 L = normalize(float3(0.577f, 0.577f, -0.577f));
 // float3 H = normalize(V + L);

 // float3 lightColor = float3(1.0f, 1.0f, 1.0f);
 // float lightIntensity = 1.0f;

 // float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoSample, metallic);

 // float roughnessSample = roughness * roughnessTex.Sample(samp, input.TexCoord).r;

 // // BRDF components
 // float NDF = DistributionGGX(N, H, roughnessSample);
 // float G = GeometrySmith(N, V, L, roughnessSample);
 // float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

 // float3 numerator = NDF * G * F;
 // float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
 // float3 specular = numerator / denominator;

 // float3 kS = F;
 // float3 kD = 1.0 - kS;
 // kD *= 1.0 - metallic;

 // float NdotL = max(dot(N, L), 0.0);

 // float3 color = (kD * albedoSample / PI + specular) * lightColor * lightIntensity * NdotL;

 // return float4(GammaCorrect(color), 1.0f);

    // Directional light data (hardcoded for now)
    // Light direction is in World Space
    float3 lightDirection = float3(-0.577f, -0.577f, 0.577f);
    float3 lightColor = float3(1.0f, 1.0f, 1.0f);
    float lightIntensity = 1.0f;

    // World Normal normalized
    float3 N_tangent_raw = normalTex.Sample(samp, input.TexCoord).xyz;
    float3 N = normalize(mul(N_tangent_raw * 2.0f - 1.0f, input.TBN));
    // N.y *= -1.0f; -- This is in case we need to flip the normals

    // Normalized Light Direction
    float3 L = normalize(-lightDirection);
    // View vector
    float3 V = normalize(input.cameraPosition - input.WorldPos);
    // Half-vector between light and view
    float3 H = normalize(V + L);

    float NdotV = abs(dot(N, V)) + 1e-5;
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float LdotH = saturate(dot(L, H));

    // Sample the diffuse based on the input albedo color and the input albedo texture
    // When a texture is present albedo allows for tinting of the texture
    float3 diffuseSample = albedo * albedoTex.Sample(samp, input.TexCoord).rgb;
    // The diffuse color -> will have to modulate with metallic later
    float3 diffuseColor = diffuseSample;

    // The roughness value from the sample
    float roughnessSample = roughness * roughnessTex.Sample(samp, input.TexCoord).r;

    // Emission combined with the emission texture
    float3 emissionSample = emission_color * emissionTex.Sample(samp, input.TexCoord).rgb;

    // Diffuse BRDF
    float3 sheenColor = float3(0.0f, 0.0f, 0.0f);
    // TODO: I'm getting greyscale at grazing angles and I'm wondering if
    // that's normal, or it's caused by some mistake like the sheen?
    float3 Fd = diffuseColor * Fd_Lambert() + F_Sheen(LdotH, sheenColor);

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), diffuseColor, metallic);

    float D = D_GGX(NdotH, roughnessSample);
    float3 F = F_Schlick(LdotH, F0, float3(1.0f, 1.0f, 1.0f));
    float G = G_SmithGGXCorrelated(NdotV, NdotL, roughnessSample);

    // Specular BRDF
    float3 Fr = (D * G) * F; 

    float3 emission = emissionSample;
    float3 BRDF = (1.0 - metallic) * Fd + Fr;
    float3 Li = lightColor * lightIntensity;

    float3 Lo = emission + (BRDF * Li * NdotL);

    return float4(Lo, 1.0f);
}



