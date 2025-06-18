#pragma once

#include "light.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "scene.hpp"
#include "shader_system.hpp"
#include "texture.hpp"
#include "window.hpp"

#include <DirectXMath.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#define MAX_MESHES 32
#define MAX_MATERIALS 32
#define MAX_TEXTURES 64
#define MAX_LIGHTS 32

struct alignas(16) CBPerFrame {
    DirectX::XMFLOAT4X4 view_matrix;
    DirectX::XMFLOAT4X4 projection_matrix;
    DirectX::XMFLOAT4X4 view_projection_matrix;
    DirectX::XMFLOAT4X4 inv_view_projection_matrix;
    DirectX::XMFLOAT3 camera_position;
    float padding[1];
};

// I might merge this with the per frame Constant Buffer
struct alignas(16) CBSkybox {
    DirectX::XMFLOAT4X4 view_matrix;
    DirectX::XMFLOAT4X4 projection_matrix;
};

struct alignas(16) CBPerObject {
    DirectX::XMFLOAT4X4 worldMatrix;
    DirectX::XMFLOAT4X4 worldInvTrans;
};

struct alignas(16) CBPerMaterial {
    DirectX::XMFLOAT3 albedo_color;
    float metallic_value;
    float roughness_value;
    float emission_intensity;
    float _padding[2];
};

struct FSVertex {
    DirectX::XMFLOAT2 pos;
    DirectX::XMFLOAT2 uv;
};

struct alignas(16) BloomConstants {
    float texel_size[2];
    float bloom_threshold;
    float bloom_intensity;
    float bloom_knee;
    float bloom_mip_strength;
    float padding[2];
};

struct alignas(16) FXAAConstants {
    float texel_size[2];
    float padding[2];
};

struct alignas(16) CBEquirectToCube {
    uint32_t face_index;
    float padding[3];
};

struct alignas(16) CBGBufferPerObject {
    DirectX::XMFLOAT4X4 world_matrix;
    DirectX::XMFLOAT3X3 world_inv_transpose;
    float padding[3];
};

struct alignas(16) CBShadowPass {
    DirectX::XMFLOAT4X4 view_projection_matrix;
};

struct CBLight {
    DirectX::XMFLOAT3 direction;
    float intensity;
    DirectX::XMFLOAT4X4 view_projection_matrix;
    DirectX::XMFLOAT4 uv_rect;
};

enum RasterizerState {
    RASTER_SOLID_BACKFACE,
    RASTER_SOLID_FRONTFACE,
    RASTER_SOLID_NONE,
    RASTER_WIREFRAME,
    RASTER_SHADOW_DEPTH_BIAS,
    RASTER_REVERSE_Z,

    RASTER_STATE_COUNT
};

enum DepthStencilState {
    DEPTH_DEFAULT,
    DEPTH_READ_ONLY,
    DEPTH_NONE,
    DEPTH_REVERSE_Z,
    DEPTH_EQUAL_ONLY,
    DEPTH_LESS_EQUAL_NO_WRITE,

    DEPTH_STATE_COUNT
};

enum BlendState {
    BLEND_OPAQUE,
    BLEND_ALPHA,
    BLEND_ADDITIVE,
    BLEND_PREMULTIPLIED_ALPHA,
    BLEND_DISABLE_WRITE,

    BLEND_STATE_COUNT
};

enum SamplerState {
    SAMPLER_LINEAR_WRAP,
    SAMPLER_LINEAR_CLAMP,
    SAMPLER_POINT_WRAP,
    SAMPLER_POINT_CLAMP,
    SAMPLER_SHADOW_COMPARISON,
    SAMPLER_ANISOTROPIC_WRAP,

    SAMPLER_STATE_COUNT
};

struct Renderer {
    ShaderSystemState shader_system;

    // Graphics Context
    Microsoft::WRL::ComPtr<ID3D11Device1> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain;
    Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation> annotation;
    D3D_FEATURE_LEVEL featureLevel;

    TextureId swapchain_texture;

    Microsoft::WRL::ComPtr<ID3D11Buffer> pCBPerObject;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pCBPerFrame;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pCBPerMaterial;

    // Blend states
    Microsoft::WRL::ComPtr<ID3D11BlendState> pDefaultBS;
    Microsoft::WRL::ComPtr<ID3D11BlendState> pAdditiveBS;

    Mesh meshes[MAX_MESHES];
    Material materials[MAX_MATERIALS];

    Texture textures[MAX_TEXTURES];
    TextureId amre_fallback_texture;
    TextureId normal_fallback_texture;

    Light lights[MAX_LIGHTS];

    /** @brief Pointer to the current window */
    Window *pWindow = NULL;

    ShaderId fullscreen_triangle_vs;
    PipelineId pbr_shader;
    PipelineId tonemap_shader;

    // Framebuffer attributes for tonemapping?
    TextureId scene_color;
    TextureId scene_depth;

    // Bloom pass members
    PipelineId bloom_threshold_shader;
    PipelineId bloom_downsample_shader;
    PipelineId bloom_upsample_shader;
    TextureId bloom_mips[6];
    uint8_t mip_count;
    Microsoft::WRL::ComPtr<ID3D11Buffer> bloom_cb_ptr;

    // FXAA pass members
    PipelineId fxaa_shader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> fxaa_cb_ptr;
    TextureId fxaa_color;

    // TEMP: Cubemap texture id
    TextureId cubemap_id;
    TextureId irradiance_cubemap;
    TextureId prefilter_map;
    TextureId brdf_lut;
    Microsoft::WRL::ComPtr<ID3D11Buffer> face_cb_ptr;

    Microsoft::WRL::ComPtr<ID3D11Buffer> skybox_cb_ptr;
    PipelineId skybox_shader;

    // G-Buffer
    PipelineId gbuffer_pipeline;
    Microsoft::WRL::ComPtr<ID3D11Buffer> gbuffer_cb_ptr;
    TextureId gbuffer_rt0;
    TextureId gbuffer_rt1;
    TextureId gbuffer_rt2;

    // Lighting pass
    PipelineId lighting_pass_pipeline;
    Microsoft::WRL::ComPtr<ID3D11Buffer> light_buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> light_srv;

    // Depth prepass (Forward+)
    PipelineId zpass_pipeline;
    TextureId z_depth;

    // Opaque pass (Forward+)
    PipelineId fp_opaque_pipeline;
    TextureId fp_opaque_color;

    // Resolved MSAA texture for post
    TextureId resolved_color;
    TextureId ping_pong_color1;
    PipelineId post_shader;

    // Pipeline States
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_states[RASTER_STATE_COUNT];
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_states[DEPTH_STATE_COUNT];
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_states[BLEND_STATE_COUNT];
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_states[SAMPLER_STATE_COUNT];

    // Shadow Pass
    TextureId shadow_atlas;
    PipelineId shadowpass_shader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> shadowpass_cb_ptr;
};

namespace renderer {

bool initialize(Renderer *renderer, Window *window);
void shutdown(Renderer *renderer);

// TODO: These should probably be static, as well
PipelineId create_tonemap_shader_pipeline(Renderer *renderer);
bool create_bloom_shader_pipeline(Renderer *renderer, PipelineId *threshold_pipeline, PipelineId *downsample_pipeline, PipelineId *upsample_pipeline);
PipelineId create_fxaa_pipeline(Renderer *renderer);
PipelineId create_skybox_pipeline(Renderer *renderer);
PipelineId create_gbuffer_pipeline(Renderer *renderer);
PipelineId create_lighting_pass_pipeline(Renderer *renderer);
PipelineId create_depth_prepass(Renderer *renderer);
PipelineId create_forward_plus_opaque(Renderer *renderer);
bool create_post_process_pipeline(Renderer *renderer, PipelineId *out_pipeline);

void begin_frame(Renderer *renderer, Scene *scene);
void end_frame(Renderer *renderer);
void render(Renderer *renderer, Scene *scene);

void render_gbuffer(Renderer *renderer, Scene *scene, Texture *rt0, Texture *rt1, Texture *rt2, Texture *depth);
void render_lighting_pass(Renderer *renderer, Scene *scene, Texture *gbuffer_a, Texture *gbuffer_b, Texture *gbuffer_c, Texture *depth, Texture *irradiance_map, Texture *prefilter_map, Texture *brdf_lut, Texture *shadow_atlas, ID3D11ShaderResourceView *lights, Texture *rt);
void render_bloom_pass(Renderer *renderer, Texture *color_buffer, Texture **bloom_mips, uint32_t mip_count);
void render_fxaa_pass(Renderer *renderer);
void render_tonemap_pass(Renderer *renderer, Texture *scene_color, Texture *bloom_texture, Texture *out_rt);
void render_skybox(Renderer *renderer, Texture *skybox, Texture *depth, Texture *rt);
void render_depth_prepass(Renderer *renderer, Scene *scene);
void render_forward_plus_opaque(Renderer *renderer, Scene *scene);
void render_post_process(Renderer *renderer, Texture *in_tex, Texture *out_tex);

void bind_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv);
void clear_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv, float *clear_color);

bool convert_equirectangular_to_cubemap(Renderer *renderer);
bool generate_irradiance_cubemap(Renderer *renderer, uint16_t resolution);
bool generate_IBL_prefilter(Renderer *renderer, uint32_t total_mips);
bool generate_BRDF_LUT(Renderer *renderer);

void on_window_resize(Renderer *renderer, uint16_t width, uint16_t height);

ID3D11Device *get_device(Renderer *renderer);

} // namespace renderer
