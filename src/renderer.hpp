#pragma once

#include "camera.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "scene.hpp"
#include "shader.hpp"
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
// TODO: Check if there is a way in C++ to force structs
// to a certain alignment.
struct alignas(16) CBPerFrame {
    DirectX::XMFLOAT4X4 viewProjectionMatrix;
    DirectX::XMFLOAT3 camera_position;
    float padding;
};

struct alignas(16) CBPerObject {
    DirectX::XMFLOAT4X4 worldMatrix;
};

struct alignas(16) CBPerMaterial {
    DirectX::XMFLOAT3 albedo;
    float metallic;
    float roughness;
    DirectX::XMFLOAT3 emission_color;
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

    // Temporary "global" sampler
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;

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

    Camera *active_camera;

    // Framebuffer attributes for tonemapping?
    TextureId scene_color;
    TextureId scene_depth;
    Microsoft::WRL::ComPtr<ID3D11Buffer> pSceneVertexBuffer;
    // Shader tonemap_shader;

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
    Microsoft::WRL::ComPtr<ID3D11Buffer> face_cb_ptr;
};

namespace renderer {

bool initialize(Renderer *renderer, Window *window);
void shutdown(Renderer *renderer);

bool create_default_shaders(Renderer *renderer);
PipelineId create_pbr_shader_pipeline(Renderer *renderer);
PipelineId create_tonemap_shader_pipeline(Renderer *renderer);
bool create_bloom_shader_pipeline(Renderer *renderer, PipelineId *threshold_pipeline, PipelineId *downsample_pipeline, PipelineId *upsample_pipeline);
PipelineId create_fxaa_pipeline(Renderer *renderer);

void begin_frame(Renderer *renderer);
void end_frame(Renderer *renderer);
void render(Renderer *renderer, Scene *scene);

void render_scene(Renderer *renderer, Scene *scene);
void render_bloom_pass(Renderer *renderer);
void render_fxaa_pass(Renderer *renderer);
void render_tonemap_pass(Renderer *renderer);
void bind_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv);
void clear_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv, float *clear_color);

bool convert_equirectangular_to_cubemap(Renderer *renderer);
bool generate_irradiance_cubemap(Renderer *renderer);

void on_window_resize(Renderer *renderer, uint16_t width, uint16_t height);

ID3D11Device *get_device(Renderer *renderer);

} // namespace renderer
