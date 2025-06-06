#pragma once

#include "material.hpp"
#include "mesh.hpp"
#include "scene.hpp"
#include "shader_system.hpp"
#include "texture.hpp"
#include "window.hpp"

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#define MAX_MESHES 16
#define MAX_MATERIALS 16
#define MAX_TEXTURES 64

// NOTE: Should be 16 byte aligned
struct alignas(16) CBPerFrame {
    // DirectX::XMFLOAT4X4 view_matrix;
    // DirectX::XMFLOAT4X4 projection_matrix;
    DirectX::XMFLOAT4X4 viewProjectionMatrix;
    DirectX::XMFLOAT3 camera_position;
    float padding[13];
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
    DirectX::XMFLOAT3 albedo_color;     // 12 bytes
    float metallic_value;               // 4 bytes -> float4 boundary
    DirectX::XMFLOAT3 emission_color;   // 12 bytes
    float roughness_value;              // 4 bytes -> float4 boundary
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

struct Renderer {
    ShaderSystemState shader_system;

    // Graphics Context
    Microsoft::WRL::ComPtr<ID3D11Device> pDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> pContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> pSwapChain;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> pRenderTargetView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> pDepthStencilView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pDepthStencilBuffer;

    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> pDepthStencilState;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> skybox_depth_state;

    // Temporary "global" sampler
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> skybox_sampler;

    Microsoft::WRL::ComPtr<ID3D11RasterizerState> default_raster;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> skybox_raster;

    Microsoft::WRL::ComPtr<ID3D11Buffer> pCBPerObject;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pCBPerFrame;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pCBPerMaterial;

    // Blend states
    Microsoft::WRL::ComPtr<ID3D11BlendState> pDefaultBS;
    Microsoft::WRL::ComPtr<ID3D11BlendState> pAdditiveBS;

    D3D_FEATURE_LEVEL featureLevel;

    Mesh meshes[MAX_MESHES];
    Material materials[MAX_MATERIALS];
    Texture textures[MAX_TEXTURES];

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
};

namespace renderer {

bool initialize(Renderer *renderer, Window *window);
void shutdown(Renderer *renderer);

// TODO: These should probably be static, as well
PipelineId create_pbr_shader_pipeline(Renderer *renderer);
PipelineId create_tonemap_shader_pipeline(Renderer *renderer);
bool create_bloom_shader_pipeline(Renderer *renderer, PipelineId *threshold_pipeline, PipelineId *downsample_pipeline, PipelineId *upsample_pipeline);
PipelineId create_fxaa_pipeline(Renderer *renderer);
PipelineId create_skybox_pipeline(Renderer *renderer);
PipelineId create_gbuffer_pipeline(Renderer *renderer);

void begin_frame(Renderer *renderer);
void end_frame(Renderer *renderer);
void render(Renderer *renderer, Scene *scene);

void render_scene(Renderer *renderer, Scene *scene);
void render_gbuffer(Renderer *renderer, Scene *scene);
void render_bloom_pass(Renderer *renderer);
void render_fxaa_pass(Renderer *renderer);
void render_tonemap_pass(Renderer *renderer);
void render_skybox(Renderer *renderer, SceneCamera *camera);

void bind_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv);
void clear_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv, float *clear_color);

bool convert_equirectangular_to_cubemap(Renderer *renderer);
bool generate_irradiance_cubemap(Renderer *renderer);
bool generate_IBL_prefilter(Renderer *renderer, uint32_t total_mips);
bool generate_BRDF_LUT(Renderer *renderer);

void on_window_resize(Renderer *renderer, uint16_t width, uint16_t height);

ID3D11Device *get_device(Renderer *renderer);

} // namespace renderer
