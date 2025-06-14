#include "renderer.hpp"

#include "application.hpp"
#include "id.hpp"
#include "logger.hpp"
#include "mesh.hpp"
#include "scene.hpp"
#include "shader_system.hpp"
#include "texture.hpp"

#include <DirectXMath.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgiformat.h>

#define RENDERING_METHOD_FORWARD_PLUS 0
#define RENDERING_METHOD_DEFERRED 1
#define RENDERING_METHOD RENDERING_METHOD_DEFERRED

#ifndef MIN
#define MIN(a, b) (a < b ? a : b)
#endif
#ifndef MAX
#define MAX(a, b) (a > b ? a : b)
#endif

#define UNUSED(x) (void)(x)

#ifdef _DEBUG
#define BEGIN_D3D11_EVENT(renderer, lname)         \
    if ((renderer) && (renderer)->annotation) {    \
        (renderer)->annotation->BeginEvent(lname); \
    }
#define END_D3D11_EVENT(renderer)               \
    if ((renderer) && (renderer)->annotation) { \
        (renderer)->annotation->EndEvent();     \
    }
#else
#define BEGIN_D3D11_EVENT(renderer, lname) UNUSED(renderer)
#define END_D3D11_EVENT(renderer) UNUSED(renderer)
#endif

// Static functions
static bool create_default_shaders(Renderer *renderer);
static bool create_pipeline_states(Renderer *renderer, ID3D11Device *device);
static bool resolve_msaa_texture(ID3D11DeviceContext *context, Texture *src, Texture *dst);

bool renderer::initialize(Renderer *renderer, Window *pWindow) {
    // Store pointer to window
    if (!pWindow) {
        LOG("The pointer provided for window was invalid.");
        return false;
    }
    renderer->pWindow = pWindow;

    // Set up the swap chain and with it the device
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.Width = pWindow->width;
    swapChainDesc.BufferDesc.Height = pWindow->height;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = pWindow->hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};
    // TODO: Will probably take the singlethreaded out at one point...
    UINT flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &swapChainDesc,
        renderer->pSwapChain.GetAddressOf(), // Output parameter
        renderer->pDevice.GetAddressOf(),    // Output parameter
        &renderer->featureLevel,
        renderer->pContext.GetAddressOf()); // Output parameter

    if (FAILED(hr)) {
        LOG("D3D11CreateDeviceAndSwapChain failed.");
        return false;
    }

    hr = renderer->pContext->QueryInterface(IID_PPV_ARGS(&renderer->annotation));
    if (FAILED(hr)) {
        LOG("%s: Couldn't query the annotation interface", __func__);
    }

    // We need to get the back buffer
    // NOTE: We need to add this pragma to clang (which is what I'm using),
    // because in clang Microsoft extensions (like __uuidof) give warnings,
    // as they are not standard C++ and I have -Werror on, which turns warnings
    // into errors.
    // NOTE2: Pragma commented out in favour of IID_PPV_ARGS but leaving it here
    // for future reference...
    // #pragma clang diagnostic push
    // #pragma clang diagnostic ignored "-Wlanguage-extension-token"
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pBackBuffer;
    hr = renderer->pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr)) {
        LOG("Renderer error: Couldn't get the backbuffer texture");
        return false;
    }
    // #pragma clang diagnostic pop

    // Now get the render target view (we only need one as we'll only have a single window)
    hr = renderer->pDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, renderer->pRenderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create RenderTargetView");
        return false;
    }

    // Create Depth Stencil Texture
    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = pWindow->width;
    depthStencilDesc.Height = pWindow->height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    hr = renderer->pDevice->CreateTexture2D(&depthStencilDesc, nullptr, renderer->pDepthStencilBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create depth stencil texture");
        return false;
    }

    // Create Depth Stencil View from Texture
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthStencilDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = renderer->pDevice->CreateDepthStencilView(renderer->pDepthStencilBuffer.Get(), &dsvDesc, renderer->pDepthStencilView.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create depth stencil view");
        return false;
    }

    // Create pipeline states
    if (!create_pipeline_states(renderer, renderer->pDevice.Get())) {
        LOG("%s: Failed to create pipeline states for renderer", __func__);
        return false;
    }

    // Create Depth Stencil State
    D3D11_DEPTH_STENCIL_DESC dsStateDesc = {};
    dsStateDesc.DepthEnable = TRUE;
    dsStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
    hr = renderer->pDevice->CreateDepthStencilState(&dsStateDesc, renderer->pDepthStencilState.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create depth stencil state");
        return false;
    }

    D3D11_DEPTH_STENCIL_DESC skybox_depth_desc = {};
    skybox_depth_desc.DepthEnable = TRUE;
    skybox_depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    skybox_depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    skybox_depth_desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    skybox_depth_desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    hr = renderer->pDevice->CreateDepthStencilState(&skybox_depth_desc, renderer->skybox_depth_state.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create depth stencil state");
        return false;
    }

    // Create default sampler(s)
    D3D11_SAMPLER_DESC samp_desc = {};
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = renderer->pDevice->CreateSamplerState(&samp_desc, renderer->sampler.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create sampler state");
        return false;
    }

    D3D11_SAMPLER_DESC skybox_samp_desc = {};
    skybox_samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    skybox_samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    skybox_samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    skybox_samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    skybox_samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    skybox_samp_desc.MaxAnisotropy = 1;
    skybox_samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = renderer->pDevice->CreateSamplerState(&skybox_samp_desc, renderer->skybox_sampler.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create sampler state");
        return false;
    }

    // Creating blend states
    D3D11_BLEND_DESC default_blend_desc = {};
    default_blend_desc.RenderTarget[0].BlendEnable = FALSE;
    default_blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = renderer->pDevice->CreateBlendState(&default_blend_desc, renderer->pDefaultBS.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create default blend state");
        return false;
    }

    D3D11_BLEND_DESC additive_blend_desc = {};
    additive_blend_desc.RenderTarget[0].BlendEnable = TRUE;
    additive_blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    additive_blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    additive_blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    additive_blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    additive_blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    additive_blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    additive_blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = renderer->pDevice->CreateBlendState(&additive_blend_desc, renderer->pAdditiveBS.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create additive blend state");
        return false;
    }

    // Creating raster states
    D3D11_RASTERIZER_DESC default_raster_desc = {};
    default_raster_desc.FillMode = D3D11_FILL_SOLID;
    default_raster_desc.CullMode = D3D11_CULL_BACK;
    default_raster_desc.DepthClipEnable = TRUE;
    hr = renderer->pDevice->CreateRasterizerState(&default_raster_desc, renderer->default_raster.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Failed to create default rasterizer state state", __func__);
        return false;
    }

    D3D11_RASTERIZER_DESC skybox_raster_desc = {};
    skybox_raster_desc.FillMode = D3D11_FILL_SOLID;
    skybox_raster_desc.CullMode = D3D11_CULL_FRONT;
    skybox_raster_desc.DepthClipEnable = TRUE;
    hr = renderer->pDevice->CreateRasterizerState(&skybox_raster_desc, renderer->skybox_raster.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Failed to create skybox rasterizer state state", __func__);
        return false;
    }

    // --- Create Constant Buffers --- //
    // PerObject Constant Buffer
    D3D11_BUFFER_DESC CBDesc = {};
    CBDesc.Usage = D3D11_USAGE_DYNAMIC;
    CBDesc.ByteWidth = sizeof(CBPerObject);
    CBDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    CBDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = renderer->pDevice->CreateBuffer(&CBDesc, nullptr, renderer->pCBPerObject.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for per object data");
        return false;
    }

    // PerFrame Constant Buffer
    CBDesc.ByteWidth = sizeof(CBPerFrame);
    hr = renderer->pDevice->CreateBuffer(&CBDesc, nullptr, renderer->pCBPerFrame.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for per frame data");
        return false;
    }

    // PerMaterial Constant Buffer
    CBDesc.ByteWidth = sizeof(CBPerMaterial);
    hr = renderer->pDevice->CreateBuffer(&CBDesc, nullptr, renderer->pCBPerMaterial.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for per material data");
        return false;
    }

    // Invalidate all meshes
    for (uint8_t i = 0; i < MAX_MESHES; ++i) {
        id::invalidate(&renderer->meshes[i].id);
    }

    // Invalidate all materials
    for (uint8_t i = 0; i < MAX_MATERIALS; ++i) {
        id::invalidate(&renderer->materials[i].id);
    }

    // Invalidate all textures
    for (uint8_t i = 0; i < MAX_TEXTURES; ++i) {
        id::invalidate(&renderer->textures[i].id);
    }

    // Initialize the Shader System
    if (!shader::system_initialize(&renderer->shader_system)) {
        LOG("%s: Failed to initialize shader system", __func__);
        return false;
    }

    // Create default shaders
    if (!create_default_shaders(renderer)) {
        LOG("%s: Failed to create default shaders", __func__);
        return false;
    }

    // Create shader pipeline for tonemapping
    renderer->tonemap_shader = create_tonemap_shader_pipeline(renderer);
    if (id::is_invalid(renderer->tonemap_shader)) {
        LOG("%s: Couldn't create tonemap shader pipeline", __func__);
        return false;
    }

    // Create shader pipelines for bloom pass
    if (!create_bloom_shader_pipeline(renderer, &renderer->bloom_threshold_shader, &renderer->bloom_downsample_shader, &renderer->bloom_upsample_shader)) {
        LOG("%s: Couldnt create pipeline for the bloom pass", __func__);
        return false;
    }

    // Create shader pipeline for FXAA pass
    renderer->fxaa_shader = create_fxaa_pipeline(renderer);
    if (id::is_invalid(renderer->fxaa_shader)) {
        LOG("%s: Couldn't create fxaa shader pipeline", __func__);
        return false;
    }

    // Create pipeline for skybox
    renderer->skybox_shader = create_skybox_pipeline(renderer);
    if (id::is_invalid(renderer->skybox_shader)) {
        LOG("%s: Couldn't create skybox shader pipeline", __func__);
        return false;
    }

#if (RENDERING_METHOD == RENDERING_METHOD_DEFERRED)
    // Create pipeline for G-Buffer
    renderer->gbuffer_pipeline = create_gbuffer_pipeline(renderer);
    if (id::is_invalid(renderer->gbuffer_pipeline)) {
        LOG("%s: Couldn't create G-buffer shader pipeline", __func__);
        return false;
    }

    // Create pipeline for Lighting Pass
    renderer->lighting_pass_pipeline = create_lighting_pass_pipeline(renderer);
    if (id::is_invalid(renderer->lighting_pass_pipeline)) {
        LOG("%s: Couldn't create Lighting Pass shader pipeline", __func__);
        return false;
    }
#endif

#if (RENDERING_METHOD == RENDERING_METHOD_FORWARD_PLUS)
    // Create Depth Prepass for Forward+ rendering
    renderer->zpass_pipeline = create_depth_prepass(renderer);
    if (id::is_invalid(renderer->zpass_pipeline)) {
        LOG("%s: Couldn't create depth prepass shader pipeline", __func__);
        return false;
    }

    // Create Opaque Shading pass for Forward+ rendering
    renderer->fp_opaque_pipeline = create_forward_plus_opaque(renderer);
    if (id::is_invalid(renderer->fp_opaque_pipeline)) {
        LOG("%s: Couldn't create Opaque shader pipeline for Forward+", __func__);
        return false;
    }
#endif

    // Create pipeline for Post Pass
    if (!create_post_process_pipeline(renderer, &renderer->post_shader)) {
        LOG("%s: Couldn't create post shader pipeline", __func__);
        return false;
    }

    // Set the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)pWindow->width;
    vp.Height = (FLOAT)pWindow->height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    renderer->pContext->RSSetViewports(1, &vp);

    // Create color and depth buffers for scene
    renderer->scene_color = texture::create(
        pWindow->width, pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);
    renderer->scene_depth = texture::create(
        pWindow->width, pWindow->height,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);

    // Texture to resolve MSAA into
    renderer->resolved_color = texture::create(
        pWindow->width, pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);

    // Another resolved texture to ping-pong between
    renderer->ping_pong_color1 = texture::create(
        pWindow->width, pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);

    // Bind the sampler
    renderer->pContext->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());

    // Convert 360 HDRi to Cubemap
    convert_equirectangular_to_cubemap(renderer);
    // Generate Irradiance map from the cubemap
    generate_irradiance_cubemap(renderer);
    // Create the IBL Prefilter map for Specular
    generate_IBL_prefilter(renderer, 5);
    // Generate the BRDF LUT
    generate_BRDF_LUT(renderer);

    UNUSED(resolve_msaa_texture);

    return true;
}

void renderer::shutdown(Renderer *renderer) {
    (void)renderer;
}

PipelineId renderer::create_tonemap_shader_pipeline(Renderer *renderer) {
    ShaderId tonemap_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/tonemap.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    ShaderId tonemap_modules[] = {renderer->fullscreen_triangle_vs, tonemap_ps};

    PipelineId tonemap_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        tonemap_modules,
        ARRAYSIZE(tonemap_modules),
        nullptr, 0);

    return tonemap_pipeline;
}

bool renderer::create_bloom_shader_pipeline(Renderer *renderer, PipelineId *threshold_pipeline, PipelineId *downsample_pipeline, PipelineId *upsample_pipeline) {
    // Create the threshold module
    ShaderId threshold_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/bloom.ps.hlsl",
        SHADER_STAGE_PS,
        "threshold_main");

    if (id::is_invalid(threshold_ps)) {
        LOG("%s: Couldn't create shader module for bloom threshold", __func__);
        return false;
    }

    // Create the downsample pixel shader module
    ShaderId downsample_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/bloom.ps.hlsl",
        SHADER_STAGE_PS,
        "downsample_main");

    if (id::is_invalid(downsample_ps)) {
        LOG("%s: Couldn't create shader module for bloom downsample", __func__);
        return false;
    }

    // Create the upsample pixel shader module
    ShaderId upsample_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/bloom.ps.hlsl",
        SHADER_STAGE_PS,
        "upsample_main");

    if (id::is_invalid(upsample_ps)) {
        LOG("%s: Couldn't create shader module for bloom upsample", __func__);
        return false;
    }

    // This holds the vs and ps modules for bloom
    ShaderId bloom_modules[2];

    // Threshold pipeline
    bloom_modules[0] = renderer->fullscreen_triangle_vs;
    bloom_modules[1] = threshold_ps;

    *threshold_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        bloom_modules,
        ARRAYSIZE(bloom_modules),
        nullptr, 0);

    if (id::is_invalid(*threshold_pipeline)) {
        LOG("%s: Couldn't create shader pipeline for bloom's threshold stage", __func__);
        return false;
    }

    // Downsample pipeline
    bloom_modules[1] = downsample_ps;
    *downsample_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        bloom_modules,
        ARRAYSIZE(bloom_modules),
        nullptr, 0);

    if (id::is_invalid(*downsample_pipeline)) {
        LOG("%s: Couldn't create shader pipeline for bloom's downsample stage", __func__);
        return false;
    }

    // Downsample pipeline
    bloom_modules[1] = upsample_ps;
    *upsample_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        bloom_modules,
        ARRAYSIZE(bloom_modules),
        nullptr, 0);

    if (id::is_invalid(*upsample_pipeline)) {
        LOG("%s: Couldn't create shader pipeline for bloom's upsample stage", __func__);
        return false;
    }

    // Create textures for bloom pass
    UINT mip_width = renderer->pWindow->width / 2;
    UINT mip_height = renderer->pWindow->height / 2;
    renderer->mip_count = 0;
    for (int i = 0; i < 5; ++i) {
        renderer->bloom_mips[i] = texture::create(
            mip_width, mip_height,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
            true,
            nullptr, 0,
            1, 1, 1, false);

        if (id::is_invalid(renderer->bloom_mips[i])) {
            LOG("Renderer error: Couldn't create bloomp mip texture %d", i);
            return false;
        }

        renderer->mip_count++;
        mip_width = MAX(mip_width / 2, 1);
        mip_height = MAX(mip_height / 2, 1);
    }

    // Create CB for bloom pass
    D3D11_BUFFER_DESC bloom_cb_desc = {};
    bloom_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    bloom_cb_desc.ByteWidth = sizeof(BloomConstants);
    bloom_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bloom_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->pDevice->CreateBuffer(&bloom_cb_desc, nullptr, renderer->bloom_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for bloom pass");
        return false;
    }

    return true;
}

PipelineId renderer::create_fxaa_pipeline(Renderer *renderer) {
    renderer->fxaa_color = texture::create(
        renderer->pWindow->width, renderer->pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);
    if (id::is_invalid(renderer->fxaa_color)) {
        LOG("Renderer error: Couldn't initialize texture for FXAA pass");
        return id::invalid();
    }

    D3D11_BUFFER_DESC fxaa_cb_desc = {};
    fxaa_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    fxaa_cb_desc.ByteWidth = sizeof(FXAAConstants);
    fxaa_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    fxaa_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->pDevice->CreateBuffer(&fxaa_cb_desc, nullptr, renderer->fxaa_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for bloom pass");
        return id::invalid();
    }

    ShaderId fxaa_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/fxaa.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    ShaderId fxaa_modules[] = {renderer->fullscreen_triangle_vs, fxaa_ps};

    PipelineId fxaa_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        fxaa_modules,
        ARRAYSIZE(fxaa_modules),
        nullptr, 0);

    return fxaa_pipeline;
}

PipelineId renderer::create_skybox_pipeline(Renderer *renderer) {
    ShaderId skybox_vs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/skybox.vs.hlsl",
        SHADER_STAGE_VS,
        "main");

    if (id::is_invalid(skybox_vs)) {
        LOG("%s: Couldn't create shader module for skybox vertex shader", __func__);
        return id::invalid();
    }

    ShaderId skybox_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/skybox.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    if (id::is_invalid(skybox_ps)) {
        LOG("%s: Couldn't create shader module for skybox pixel shader", __func__);
        return id::invalid();
    }

    ShaderId skybox_modules[] = {skybox_vs, skybox_ps};

    PipelineId skybox_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        skybox_modules,
        ARRAYSIZE(skybox_modules),
        nullptr, 0);

    if (id::is_invalid(skybox_pipeline)) {
        LOG("%s: Couldn't create shader pipeline for bloom's skybox stage", __func__);
        return id::invalid();
    }

    // Create constant buffer for skybox
    // TODO: Might want to merge this with regular camera/view/projection
    D3D11_BUFFER_DESC skybox_cb_desc = {};
    skybox_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    skybox_cb_desc.ByteWidth = sizeof(CBSkybox);
    skybox_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    skybox_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->pDevice->CreateBuffer(&skybox_cb_desc, nullptr, renderer->skybox_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for skybox");
        return id::invalid();
    }

    return skybox_pipeline;
}

PipelineId renderer::create_gbuffer_pipeline(Renderer *renderer) {
    ShaderId gbuffer_vs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/gbuffer.vs.hlsl",
        SHADER_STAGE_VS,
        "main");

    if (id::is_invalid(gbuffer_vs)) {
        LOG("%s: Couldn't create shader module for G-buffer vertex shader", __func__);
        return id::invalid();
    }

    ShaderId gbuffer_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/gbuffer.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    if (id::is_invalid(gbuffer_ps)) {
        LOG("%s: Couldn't create shader module for G-buffer pixel shader", __func__);
        return id::invalid();
    }

    // Define the pbr input layout
    D3D11_INPUT_ELEMENT_DESC pbr_input_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    ShaderId gbuffer_modules[] = {gbuffer_vs, gbuffer_ps};

    PipelineId gbuffer_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        gbuffer_modules,
        ARRAYSIZE(gbuffer_modules),
        pbr_input_desc, ARRAYSIZE(pbr_input_desc));

    if (id::is_invalid(gbuffer_pipeline)) {
        LOG("%s: Couldn't create shader pipeline for G-buffer", __func__);
        return id::invalid();
    }

    // Create RTV's for the G-Buffer.
    // Should these just be part of the pipeline and be bound with it?
    // Albedo (RGB) + Roughness (A)
    renderer->gbuffer_rt0 = texture::create(
        renderer->pWindow->width, renderer->pWindow->height,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);
    // World-space normal (RGB)
    renderer->gbuffer_rt1 = texture::create(
        renderer->pWindow->width, renderer->pWindow->height,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);
    // Emission color (RGB) + Metallic (A)
    renderer->gbuffer_rt2 = texture::create(
        renderer->pWindow->width, renderer->pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);

    return gbuffer_pipeline;
}

PipelineId renderer::create_lighting_pass_pipeline(Renderer *renderer) {
    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.ByteWidth = sizeof(CBLighting);
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->pDevice->CreateBuffer(&cb_desc, nullptr, renderer->lp_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for lighting pass");
        return id::invalid();
    }

    ShaderId lighting_pass_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/lighting_pass.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    ShaderId lighting_pass_modules[] = {renderer->fullscreen_triangle_vs, lighting_pass_ps};

    PipelineId lighting_pass_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        lighting_pass_modules,
        ARRAYSIZE(lighting_pass_modules),
        nullptr, 0);

    renderer->lighting_rt = texture::create(
        renderer->pWindow->width, renderer->pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);

    return lighting_pass_pipeline;
}

PipelineId renderer::create_depth_prepass(Renderer *renderer) {
    ShaderId zpass_vs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/zpass.vs.hlsl",
        SHADER_STAGE_VS,
        "main");

    if (id::is_invalid(zpass_vs)) {
        LOG("%s: Couldn't create shader module for Z-pass vertex shader", __func__);
        return id::invalid();
    }

    // Define the pbr input layout
    D3D11_INPUT_ELEMENT_DESC zpass_input_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    ShaderId zpass_modules[] = {zpass_vs};

    PipelineId zpass_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        zpass_modules,
        ARRAYSIZE(zpass_modules),
        zpass_input_desc, ARRAYSIZE(zpass_input_desc));

    if (id::is_invalid(zpass_pipeline)) {
        LOG("%s: Couldn't create shader pipeline for Depth Prepass", __func__);
        return id::invalid();
    }

    // Create Depth Texture for Z-prepass
    renderer->z_depth = texture::create(
        renderer->pWindow->width, renderer->pWindow->height,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
        D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 4, false);

    return zpass_pipeline;
}

PipelineId renderer::create_forward_plus_opaque(Renderer *renderer) {
    ShaderId fp_opaque_vs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/fp_opaque.vs.hlsl",
        SHADER_STAGE_VS,
        "main");
    ShaderId fp_opaque_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/fp_opaque.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    // Define the fp_opaque input layout
    D3D11_INPUT_ELEMENT_DESC fp_opaque_input_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    // Modules in the PBR pipeline
    ShaderId fp_opaque_modules[] = {fp_opaque_vs, fp_opaque_ps};

    PipelineId fp_opaque_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        fp_opaque_modules,
        ARRAYSIZE(fp_opaque_modules),
        fp_opaque_input_desc,
        ARRAYSIZE(fp_opaque_input_desc));

    return fp_opaque_pipeline;
}

bool renderer::create_post_process_pipeline(Renderer *renderer, PipelineId *out_pipeline) {
    ShaderId post_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/post.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    ShaderId post_modules[] = {renderer->fullscreen_triangle_vs, post_ps};

    *out_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        post_modules,
        ARRAYSIZE(post_modules),
        nullptr, 0);

    return true;
}

void renderer::begin_frame(Renderer *renderer, Scene *scene) {
    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Clear backbuffer RTV
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->ClearRenderTargetView(renderer->pRenderTargetView.Get(), clear_color);

    // Update per frame constants -------------------------------------------------- */
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = renderer->pContext->Map(renderer->pCBPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to map per object constant buffer");
        return;
    }

    CBPerFrame *perFramePtr = (CBPerFrame *)mappedResource.pData;
    perFramePtr->view_matrix = scene::camera_get_view_matrix(scene->active_cam);
    perFramePtr->projection_matrix = scene::camera_get_projection_matrix(scene->active_cam);
    perFramePtr->view_projection_matrix = scene::camera_get_view_projection_matrix(scene->active_cam);
    DirectX::XMMATRIX inv_view_projection = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&perFramePtr->view_projection_matrix));
    DirectX::XMStoreFloat4x4(&perFramePtr->inv_view_projection_matrix, inv_view_projection);
    perFramePtr->camera_position = scene->active_cam->position;

    context->Unmap(renderer->pCBPerFrame.Get(), 0);

    // Bind the per frame constants to both the vertex and pixel stages
    context->VSSetConstantBuffers(0, 1, renderer->pCBPerFrame.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, renderer->pCBPerFrame.GetAddressOf());
    /* ------------------------------------------------------------------------------- */

    // Set viewport to window size
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(renderer->pWindow->width);
    viewport.Height = static_cast<float>(renderer->pWindow->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    renderer->pContext->RSSetViewports(1, &viewport);

    // TEMP: Leaving this here for now, as I'm not changing this ever...?
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void renderer::end_frame(Renderer *renderer) {
    renderer->pSwapChain->Present(1, 0);
}

void renderer::render(Renderer *renderer, Scene *scene) {
#if (RENDERING_METHOD == RENDERING_METHOD_FORWARD_PLUS)
    // Forward+ rendering
    render_depth_prepass(renderer, scene);
    render_forward_plus_opaque(renderer, scene);
#endif

#if (RENDERING_METHOD == RENDERING_METHOD_DEFERRED)
    // Deferred rendering
    Texture *gbuffer_a = texture::get(renderer, renderer->gbuffer_rt0);
    Texture *gbuffer_b = texture::get(renderer, renderer->gbuffer_rt1);
    Texture *gbuffer_c = texture::get(renderer, renderer->gbuffer_rt2);
    Texture *depth = texture::get(renderer, renderer->scene_depth);
    Texture *scene_color = texture::get(renderer, renderer->scene_color);
    Texture *irradiance_map = texture::get(renderer, renderer->irradiance_cubemap);
    Texture *prefilter_map = texture::get(renderer, renderer->prefilter_map);
    Texture *brdf_lut = texture::get(renderer, renderer->brdf_lut);

    render_gbuffer(renderer, scene, gbuffer_a, gbuffer_b, gbuffer_c, depth);
    render_lighting_pass(renderer, gbuffer_a, gbuffer_b, gbuffer_c, depth, irradiance_map, prefilter_map, brdf_lut, scene_color);
#endif

    // Render the environment map
    Texture *skybox = texture::get(renderer, renderer->cubemap_id);
    render_skybox(renderer, skybox, depth, scene_color);

#if (RENDERING_METHOD == RENDERING_METHOD_FORWARD_PLUS)
    // Resolve MSAA textures
    Texture *scene_color_tex = &renderer->textures[renderer->scene_color.id];
    Texture *resolved = texture::get(renderer, renderer->resolved_color);
    resolve_msaa_texture(renderer->pContext.Get(), scene_color_tex, resolved);
#endif

    // Render bloom pass
    Texture *bloom_mips[] = {
        texture::get(renderer, renderer->bloom_mips[0]),
        texture::get(renderer, renderer->bloom_mips[1]),
        texture::get(renderer, renderer->bloom_mips[2]),
        texture::get(renderer, renderer->bloom_mips[3]),
        texture::get(renderer, renderer->bloom_mips[4]),
        texture::get(renderer, renderer->bloom_mips[5]),
    };
    render_bloom_pass(renderer, scene_color, bloom_mips, ARRAYSIZE(bloom_mips));

    // Render the tonemap pass
    Texture *pp0 = texture::get(renderer, renderer->ping_pong_color1);
    render_tonemap_pass(renderer, scene_color, bloom_mips[0], pp0);

    // Render extra post
    // TODO: Look into if I can capture the swapchain's rendertarget in a texture, as well
    // then modify this so it takes in a texture not a render target
    render_post_process(renderer, pp0, renderer->pRenderTargetView.Get());
}

void renderer::render_gbuffer(Renderer *renderer, Scene *scene,
                              Texture *rt0, Texture *rt1, Texture *rt2, Texture *depth) {
    BEGIN_D3D11_EVENT(renderer, L"G-buffer Pass (Deferred)");

    ID3D11DeviceContext *context = renderer->pContext.Get();
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Bind pipeline states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_DEFAULT].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_BACKFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);

    // Bind the the render targets and depth and clear them
    ID3D11RenderTargetView *rtvs[] = {rt0->rtv[0].Get(), rt1->rtv[0].Get(), rt2->rtv[0].Get()};
    context->ClearRenderTargetView(rtvs[0], clear_color);
    context->ClearRenderTargetView(rtvs[1], clear_color);
    context->ClearRenderTargetView(rtvs[2], clear_color);
    context->ClearDepthStencilView(depth->dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, depth->dsv.Get());

    // Bind the shader
    ShaderPipeline *gbuffer_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->gbuffer_pipeline);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), gbuffer_pipeline);

    // Bind the samplers
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // Render meshes
    MaterialId current_material_bound = id::invalid();
    for (int i = 0; i < MAX_SCENE_MESHES; ++i) {
        SceneMesh *mesh = &scene->meshes[i];

        if (id::is_invalid(mesh->id))
            continue;

        // If the material id is different from the currently bound
        // bind the new one.
        if (mesh->material_id.id != current_material_bound.id) {
            // Get the material
            Material *mat = material::get(renderer, mesh->material_id);
            if (!mat) {
                LOG("%s: Warning! Material couldn't be fetched", __func__);
                continue;
            }

            // Bind the material's values and textures
            material::bind(renderer, mat, 1, 0);

            // Update the currently bound material
            current_material_bound = mat->id;
        }

        // Lookup the mesh gpu resource through the mesh_id
        Mesh *gpu_mesh = mesh::get(renderer, mesh->mesh_id);
        if (!gpu_mesh) {
            continue;
        }

        // Bind mesh instance
        scene::bind_mesh_instance(renderer, scene, mesh->id, 1);

        // Draw the mesh
        mesh::draw(renderer->pContext.Get(), gpu_mesh);
    }

    // Unbind RTV's as the output of the Gbuffer will definitely
    // be used as shader resources
    ID3D11RenderTargetView *nullRTVs[ARRAYSIZE(rtvs)] = {nullptr};
    renderer->pContext->OMSetRenderTargets(_countof(nullRTVs), nullRTVs, nullptr);

    END_D3D11_EVENT(renderer);
}

void renderer::render_lighting_pass(Renderer *renderer, Texture *gbuffer_a, Texture *gbuffer_b, Texture *gbuffer_c, Texture *depth,
                                    Texture *irradiance_map, Texture *prefilter_map, Texture *brdf_lut,
                                    Texture *rt) {
    BEGIN_D3D11_EVENT(renderer, L"Lighting Pass (Deferred)");

    ID3D11DeviceContext *context = renderer->pContext.Get();
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Bind pipeline states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_NONE].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_NONE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);

    // Clear and Bind the RTV
    context->ClearRenderTargetView(rt->rtv[0].Get(), clear_color);
    context->OMSetRenderTargets(1, rt->rtv[0].GetAddressOf(), nullptr);

    // Bind the shader pipeline
    ShaderPipeline *lighting_pass_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->lighting_pass_pipeline);
    shader::bind_pipeline(&renderer->shader_system, context, lighting_pass_pipeline);

    // Bind the samplers
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // Bind the SRV's including the gbuffer outputs and the IBL textures
    ID3D11ShaderResourceView *srvs[] = {
        gbuffer_a->srv.Get(),
        gbuffer_b->srv.Get(),
        gbuffer_c->srv.Get(),
        depth->srv.Get(),
        irradiance_map->srv.Get(),
        prefilter_map->srv.Get(),
        brdf_lut->srv.Get(),
    };
    context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    context->Draw(3, 0);

    // Unbind SRVs
    ID3D11ShaderResourceView *nullSRVs[1] = {nullptr};
    context->PSSetShaderResources(0, 1, nullSRVs);

    END_D3D11_EVENT(renderer);
}

void renderer::render_bloom_pass(Renderer *renderer, Texture *color_buffer, Texture **bloom_mips, uint32_t mip_count) {
    BEGIN_D3D11_EVENT(renderer, L"Bloom Pass");

    ID3D11DeviceContext *context = renderer->pContext.Get();
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Bind the states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_NONE].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_FRONTFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);

    // Set up constant buffer for bloom
    BloomConstants bloom_constants = {};
    bloom_constants.bloom_threshold = 1.0f;
    bloom_constants.bloom_intensity = 1.0f;
    bloom_constants.bloom_knee = 0.2f;

    // Map and bind the constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(renderer->bloom_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &bloom_constants, sizeof(BloomConstants));
    context->Unmap(renderer->bloom_cb_ptr.Get(), 0);
    context->PSSetConstantBuffers(1, 1, renderer->bloom_cb_ptr.GetAddressOf());

    // Bind sampler state
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    D3D11_VIEWPORT viewport = {};

    // --- Threshold pass ---
    {
        // Bind the render target and clear it
        context->ClearRenderTargetView(bloom_mips[0]->rtv[0].Get(), clear_color);
        context->OMSetRenderTargets(1, bloom_mips[0]->rtv[0].GetAddressOf(), nullptr);

        // Bind shader pipeline for threshold
        ShaderPipeline *threshold_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->bloom_threshold_shader);
        shader::bind_pipeline(&renderer->shader_system, context, threshold_pipeline);

        // Set the viewport as we are dealing with something smaller
        viewport.Width = (float)bloom_mips[0]->width;
        viewport.Height = (float)bloom_mips[0]->height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);

        // Bind the scene color buffer as the starting point
        context->PSSetShaderResources(0, 1, color_buffer->srv.GetAddressOf());

        context->Draw(3, 0);
    }

    // --- Downsample chain ---
    {
        // Bind shader pipeline for downsample chain
        ShaderPipeline *downsample_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->bloom_downsample_shader);
        shader::bind_pipeline(&renderer->shader_system, context, downsample_pipeline);

        for (uint32_t i = 1; i < mip_count; ++i) {
            Texture *current_mip = bloom_mips[i];

            // Update texel size for the current target mip
            bloom_constants.texel_size[0] = 1.0f / current_mip->width;
            bloom_constants.texel_size[1] = 1.0f / current_mip->height;
            context->Map(renderer->bloom_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            memcpy(mapped.pData, &bloom_constants, sizeof(BloomConstants));
            context->Unmap(renderer->bloom_cb_ptr.Get(), 0);

            // Update the bound render target and clear it
            context->ClearRenderTargetView(current_mip->rtv[0].Get(), clear_color);
            context->OMSetRenderTargets(1, current_mip->rtv[0].GetAddressOf(), nullptr);

            // Update the viewport to the new size
            viewport.Width = (float)current_mip->width;
            viewport.Height = (float)current_mip->height;
            context->RSSetViewports(1, &viewport);

            // Bind the previous mip as the input
            context->PSSetShaderResources(0, 1, bloom_mips[i - 1]->srv.GetAddressOf());

            context->Draw(3, 0);
        }
    }

    // --- Upsample chain ---
    {
        // Switch to additive blending here for the upsample chain
        context->OMSetBlendState(renderer->blend_states[BLEND_ADDITIVE].Get(), nullptr, 0xFFFFFFFF);

        // Bind the appropriate shader pipeline for the upsample
        ShaderPipeline *upsample_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->bloom_upsample_shader);
        shader::bind_pipeline(&renderer->shader_system, context, upsample_pipeline);

        for (int i = (int)mip_count - 2; i >= 0; --i) {
            Texture *current_mip = bloom_mips[i];

            // Update texel size for the current target mip
            bloom_constants.texel_size[0] = 1.0f / current_mip->width;
            bloom_constants.texel_size[1] = 1.0f / current_mip->height;

            // Also modulate the strength of the current "layer"
            int upsample_idx = mip_count - 2 - i;
            float t = upsample_idx / float(mip_count - 2);
            float smoothstep = t * t * (3.0f - 2.0f * t);
            bloom_constants.bloom_mip_strength = std::lerp(1.0f, 0.2f, smoothstep);

            context->Map(renderer->bloom_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            memcpy(mapped.pData, &bloom_constants, sizeof(BloomConstants));
            context->Unmap(renderer->bloom_cb_ptr.Get(), 0);

            // Update the bound render target and DON'T clear it, so it can blend additively
            context->OMSetRenderTargets(1, current_mip->rtv[0].GetAddressOf(), nullptr);

            // Update the viewport to the new size
            viewport.Width = (float)current_mip->width;
            viewport.Height = (float)current_mip->height;
            context->RSSetViewports(1, &viewport);

            // Bind the previous mip as the input
            context->PSSetShaderResources(0, 1, bloom_mips[i + 1]->srv.GetAddressOf());

            context->Draw(3, 0);
        }
    }

    END_D3D11_EVENT(renderer);
}

void renderer::render_fxaa_pass(Renderer *renderer) {
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // For convenience but could be useful to bypass some dereferencing as well
    ID3D11DeviceContext *context = renderer->pContext.Get();
    ID3D11ShaderResourceView *scene_srv = renderer->textures[renderer->scene_color.id].srv.Get();

    Texture *fxaa_texture = &renderer->textures[renderer->fxaa_color.id];

    FXAAConstants fxaa_cb = {};
    fxaa_cb.texel_size[0] = 1.0f / fxaa_texture->width;
    fxaa_cb.texel_size[1] = 1.0f / fxaa_texture->height;

    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(renderer->fxaa_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &fxaa_cb, sizeof(FXAAConstants));
    context->Unmap(renderer->fxaa_cb_ptr.Get(), 0);

    context->ClearRenderTargetView(fxaa_texture->rtv[0].Get(), clear_color);
    context->OMSetRenderTargets(1, fxaa_texture->rtv[0].GetAddressOf(), nullptr);

    ShaderPipeline *fxaa_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->fxaa_shader);
    shader::bind_pipeline(&renderer->shader_system, context, fxaa_pipeline);

    // Unbind the depth stencil state
    renderer->pContext->OMSetDepthStencilState(nullptr, 0);

    ID3D11ShaderResourceView *srvs[] = {scene_srv};
    renderer->pContext->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    context->PSSetConstantBuffers(1, 1, renderer->fxaa_cb_ptr.GetAddressOf());

    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)fxaa_texture->width;
    viewport.Height = (float)fxaa_texture->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    context->Draw(3, 0);
}

void renderer::render_tonemap_pass(Renderer *renderer, Texture *scene_color, Texture *bloom_texture, Texture *out_rt) {
    BEGIN_D3D11_EVENT(renderer, L"Tonemap Pass");

    ID3D11DeviceContext *context = renderer->pContext.Get();
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // Bind the states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_NONE].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_BACKFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);

    // Clear and bind the the render target, no depth 
    context->ClearRenderTargetView(out_rt->rtv[0].Get(), clear_color);
    context->OMSetRenderTargets(1, out_rt->rtv[0].GetAddressOf(), nullptr); 

    // Bind the shader for the tonemap pass
    ShaderPipeline *tonemap_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->tonemap_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), tonemap_pipeline);

    // Bind the sampler state
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // Set the viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)out_rt->width;
    viewport.Height = (float)out_rt->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    renderer->pContext->RSSetViewports(1, &viewport);

    // Bind the SRV's for the scene buffer and the bloom pass output 
    ID3D11ShaderResourceView *srvs[] = {
        scene_color->srv.Get(),
        bloom_texture->srv.Get(),
    };
    context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    // Draw the triangle
    context->Draw(3, 0);

    END_D3D11_EVENT(renderer)
}

void renderer::render_skybox(Renderer *renderer, Texture *skybox, Texture *depth, Texture *rt) {
    BEGIN_D3D11_EVENT(renderer, L"Skybox");

    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Bind the states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_LESS_EQUAL_NO_WRITE].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_FRONTFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);

    // Bind the render target and depth without clearing
    context->OMSetRenderTargets(1, rt->rtv[0].GetAddressOf(), depth->dsv.Get());

    // Bind the skybox shader
    ShaderPipeline *skybox_shader = shader::get_pipeline(&renderer->shader_system, renderer->skybox_shader);
    shader::bind_pipeline(&renderer->shader_system, context, skybox_shader);

    // Bind the samplers
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // Bind the environment map as a texture
    context->PSSetShaderResources(0, 1, skybox->srv.GetAddressOf());

    // Draw cube hardcoded into vertex shader
    context->Draw(36, 0);

    END_D3D11_EVENT(renderer)
}

void renderer::render_depth_prepass(Renderer *renderer, Scene *scene) {
    BEGIN_D3D11_EVENT(renderer, L"Depth Prepass (Forward+)");

    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Bind the states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_DEFAULT].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_BACKFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_DISABLE_WRITE].Get(), nullptr, 0xFFFFFFFF);

    // Bind and clear the depth buffer (no color for this one)
    Texture *depth = texture::get(renderer, renderer->z_depth);
    renderer->pContext->OMSetRenderTargets(0, nullptr, depth->dsv.Get());
    renderer->pContext->ClearDepthStencilView(depth->dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // Bind the shader pipeline
    ShaderPipeline *zpass_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->zpass_pipeline);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), zpass_pipeline);

    // Render meshes
    for (int i = 0; i < MAX_SCENE_MESHES; ++i) {
        SceneMesh *mesh = &scene->meshes[i];

        if (id::is_invalid(mesh->id))
            continue;

        // Lookup the mesh gpu resource through the mesh_id
        Mesh *gpu_mesh = mesh::get(renderer, mesh->mesh_id);
        if (!gpu_mesh) {
            continue;
        }

        // Bind mesh instance
        scene::bind_mesh_instance(renderer, scene, mesh->id, 1);

        // Draw the mesh
        mesh::draw(renderer->pContext.Get(), gpu_mesh);
    }

    END_D3D11_EVENT(renderer)
}

void renderer::render_forward_plus_opaque(Renderer *renderer, Scene *scene) {
    BEGIN_D3D11_EVENT(renderer, L"Opaque Pass (Forward+)");

    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Bind pipeline states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_READ_ONLY].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_BACKFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);

    // Bind the the render target and depth
    // also clear the color only
    Texture *scene_rt = texture::get(renderer, renderer->scene_color);
    Texture *depth = texture::get(renderer, renderer->z_depth);
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context->OMSetRenderTargets(1, scene_rt->rtv[0].GetAddressOf(), depth->dsv.Get());
    context->ClearRenderTargetView(scene_rt->rtv[0].Get(), clear_color);

    // Bind the shader
    ShaderPipeline *fp_opaque_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->fp_opaque_pipeline);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), fp_opaque_pipeline);

    // Bind the samplers
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // Fetch the environment map textures and bind them
    Texture *irradiance_tex = texture::get(renderer, renderer->irradiance_cubemap);
    Texture *prefilter_tex = texture::get(renderer, renderer->prefilter_map);
    Texture *brdf_lut_tex = texture::get(renderer, renderer->brdf_lut);
    ID3D11ShaderResourceView *env_srvs[] = {irradiance_tex->srv.Get(), prefilter_tex->srv.Get(), brdf_lut_tex->srv.Get()};
    if (prefilter_tex && irradiance_tex && brdf_lut_tex) {
        renderer->pContext->PSSetShaderResources(0, ARRAYSIZE(env_srvs), env_srvs);
    }

    // Loop through our meshes from our selected scene
    MaterialId current_material_bound = id::invalid();
    for (int i = 0; i < MAX_SCENE_MESHES; ++i) {
        SceneMesh *mesh = &scene->meshes[i];
        if (id::is_invalid(mesh->id))
            continue;

        // If the material id is different from the currently bound
        // bind the new one.
        if (mesh->material_id.id != current_material_bound.id) {
            // Get the material
            Material *mat = material::get(renderer, mesh->material_id);
            if (!mat) {
                LOG("%s: Warning! Material couldn't be fetched", __func__);
                continue;
            }

            // Bind the material's values and textures
            material::bind(renderer, mat, 1, ARRAYSIZE(env_srvs));

            // Update the currently bound material
            current_material_bound = mat->id;
        }

        // Lookup the mesh gpu resource through the mesh_id
        Mesh *gpu_mesh = mesh::get(renderer, mesh->mesh_id);
        if (!gpu_mesh) {
            continue;
        }

        // Bind mesh instance
        scene::bind_mesh_instance(renderer, scene, mesh->id, 1);

        // Draw the mesh
        mesh::draw(renderer->pContext.Get(), gpu_mesh);
    }

    END_D3D11_EVENT(renderer);
}

void renderer::render_post_process(Renderer *renderer, Texture *in_tex, ID3D11RenderTargetView *out) {
    BEGIN_D3D11_EVENT(renderer, L"Post Pass");

    ID3D11DeviceContext *context = renderer->pContext.Get();
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // Bind pipeline states
    context->OMSetDepthStencilState(renderer->depth_states[DEPTH_NONE].Get(), 0);
    context->RSSetState(renderer->rasterizer_states[RASTER_SOLID_BACKFACE].Get());
    context->OMSetBlendState(renderer->blend_states[BLEND_OPAQUE].Get(), nullptr, 0xFFFFFFFF);
    
    // Clear and bind the the render target, no depth 
    context->ClearRenderTargetView(out, clear_color);
    context->OMSetRenderTargets(1, &out, nullptr); 

    // Bind shader pipeline
    ShaderPipeline *post_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->post_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), post_pipeline);

    // Bind the samplers
    context->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // Bind the the input map as a texture
    context->PSSetShaderResources(0, 1, in_tex->srv.GetAddressOf());

    // Draw the triangle
    context->Draw(3, 0);

    END_D3D11_EVENT(renderer);
}

void renderer::bind_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv) {
    renderer->pContext->OMSetRenderTargets(1, &rtv, dsv);
}

void renderer::clear_render_target(Renderer *renderer, ID3D11RenderTargetView *rtv, ID3D11DepthStencilView *dsv, float *clear_color) {
    if (rtv) {
        renderer->pContext->ClearRenderTargetView(rtv, clear_color);
    }
    if (dsv) {
        renderer->pContext->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

bool renderer::convert_equirectangular_to_cubemap(Renderer *renderer) {
    // TODO: Not my favourite way of just tacking this here as well, but it's ok for now?
    renderer->pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind default sampler
    renderer->pContext->PSSetSamplers(0, 1, renderer->sampler_states[SAMPLER_LINEAR_CLAMP].GetAddressOf());

    // TextureId hdri = texture::load_hdr("assets/photo_studio_loft_hall_4k.hdr");
    // TextureId hdri = texture::load_hdr("assets/metal_studio_23.hdr");
    TextureId hdri = texture::load_hdr("assets/autoshop_01_4k.hdr");
    Texture *hdri_tex = &renderer->textures[hdri.id];

    renderer->cubemap_id = texture::create(
        512, 512,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
        true,
        nullptr, 0,
        6, 1, 1, true);

    // Create Constant Buffer for conversion
    D3D11_BUFFER_DESC cb_desc = {};
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.ByteWidth = sizeof(CBEquirectToCube);
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->pDevice->CreateBuffer(&cb_desc, nullptr, renderer->face_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for bloom pass");
        return false;
    }

    // Create the pipeline for conversion
    ShaderId conversion_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/equirect_to_cube.ps.hlsl",
        SHADER_STAGE_PS,
        "main");
    if (id::is_invalid(conversion_ps)) {
        LOG("%s: Couldn't create pixel shader module for Equirectangular to Cubemap conversion", __func__);
        return false;
    }

    ShaderId conversion_modules[] = {renderer->fullscreen_triangle_vs, conversion_ps};
    PipelineId conversion_shader = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        conversion_modules,
        ARRAYSIZE(conversion_modules),
        nullptr, 0);
    if (id::is_invalid(conversion_shader)) {
        LOG("%s: Couldn't create shader pipeline for Equirectangular to Cubemap conversion", __func__);
        return false;
    }

    // Bind shaders
    ShaderPipeline *conversion_pipeline = shader::get_pipeline(&renderer->shader_system, conversion_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), conversion_pipeline);

    const float clear_color[4] = {0, 0, 0, 1};

    Texture *cubemap_tex = &renderer->textures[renderer->cubemap_id.id];

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(cubemap_tex->width);
    viewport.Height = static_cast<float>(cubemap_tex->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    renderer->pContext->RSSetViewports(1, &viewport);

    ID3D11ShaderResourceView *srvs[] = {hdri_tex->srv.Get()};
    renderer->pContext->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    for (int face = 0; face < 6; ++face) {
        // Update constant buffer with face index
        D3D11_MAPPED_SUBRESOURCE mapped;
        renderer->pContext->Map(renderer->face_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        ((CBEquirectToCube *)mapped.pData)->face_index = face;
        renderer->pContext->Unmap(renderer->face_cb_ptr.Get(), 0);

        // Set the face index cb on ps shader
        renderer->pContext->PSSetConstantBuffers(0, 1, renderer->face_cb_ptr.GetAddressOf());

        renderer->pContext->OMSetRenderTargets(1, cubemap_tex->rtv[face].GetAddressOf(), nullptr);
        renderer->pContext->ClearRenderTargetView(cubemap_tex->rtv[face].Get(), clear_color);

        renderer->pContext->Draw(3, 0);
    }

    return true;
}

bool renderer::generate_irradiance_cubemap(Renderer *renderer) {
    // TODO: Currently this is hardcoded here and in the shader
    const uint16_t irradiance_size = 32;

    renderer->irradiance_cubemap = texture::create(
        irradiance_size,
        irradiance_size,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
        true,
        nullptr,
        0,
        6,
        1,
        1, true);
    Texture *irradiance_tex = &renderer->textures[renderer->irradiance_cubemap.id];

    ShaderId irradiance_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/irradiance_conv.ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    if (id::is_invalid(irradiance_ps)) {
        LOG("%s: Couldn't create pixel shader module for Irradiance Convolution", __func__);
        return false;
    }

    ShaderId irradiance_modules[] = {renderer->fullscreen_triangle_vs, irradiance_ps};
    PipelineId irradiance_shader = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        irradiance_modules,
        ARRAYSIZE(irradiance_modules),
        nullptr, 0);

    if (id::is_invalid(irradiance_shader)) {
        LOG("%s: Couldn't create shader pipeline for Irradiance Convolution", __func__);
        return false;
    }

    // Bind shaders
    ShaderPipeline *irradiance_pipeline = shader::get_pipeline(&renderer->shader_system, irradiance_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), irradiance_pipeline);

    // const float clear_color[4] = {0, 0, 0, 1};
    // renderer->pContext->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());

    Texture *env_map = &renderer->textures[renderer->cubemap_id.id];

    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)irradiance_size;
    viewport.Height = (float)irradiance_size;
    viewport.MaxDepth = 1.0f;
    renderer->pContext->RSSetViewports(1, &viewport);

    for (int face = 0; face < 6; ++face) {
        // Update constant buffer with face index
        D3D11_MAPPED_SUBRESOURCE mapped;
        renderer->pContext->Map(renderer->face_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        ((CBEquirectToCube *)mapped.pData)->face_index = face;
        renderer->pContext->Unmap(renderer->face_cb_ptr.Get(), 0);

        // Set the face index cb on ps shader
        renderer->pContext->PSSetConstantBuffers(0, 1, renderer->face_cb_ptr.GetAddressOf());

        // Set target
        renderer->pContext->OMSetRenderTargets(1, irradiance_tex->rtv[face].GetAddressOf(), nullptr);

        // Bind environment map SRV
        renderer->pContext->PSSetShaderResources(0, 1, env_map->srv.GetAddressOf());

        // Draw
        renderer->pContext->Draw(3, 0);
    }

    return true;
}

bool renderer::generate_IBL_prefilter(Renderer *renderer, uint32_t total_mips) {
    ShaderId prefilter_cs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/ibl_prefilter.cs.hlsl",
        SHADER_STAGE_CS,
        "main");

    if (id::is_invalid(prefilter_cs)) {
        LOG("%s: Failed to create compute shader for IBL prefilter", __func__);
        return false;
    }

    ShaderId prefilter_modules[] = {prefilter_cs};
    PipelineId prefilter_shader = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        prefilter_modules,
        ARRAYSIZE(prefilter_modules),
        nullptr, 0);

    if (id::is_invalid(prefilter_shader)) {
        LOG("%s: Couldn't create shader pipeline for IBL prefilter", __func__);
        return false;
    }

    renderer->prefilter_map = texture::create(
        256, 256,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
        true,
        nullptr, 0,
        6, total_mips,
        1, true);

    // Create constant buffer
    struct CBIBLPrefilter {
        uint32_t current_mip_level;
        uint32_t total_mip_levels;
        float roughness;
        uint32_t num_samples;
    };

    Microsoft::WRL::ComPtr<ID3D11Buffer> iblpre_cb_ptr;

    D3D11_BUFFER_DESC prefilter_cb_desc = {};
    prefilter_cb_desc.ByteWidth = sizeof(CBIBLPrefilter);
    prefilter_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    prefilter_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    prefilter_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = renderer->pDevice->CreateBuffer(&prefilter_cb_desc, nullptr, iblpre_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Failed to create constant buffer for IBL prefilter", __func__);
        return false;
    }

    // Bind shaders
    ShaderPipeline *prefilter_pipeline = shader::get_pipeline(&renderer->shader_system, prefilter_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), prefilter_pipeline);

    // Bind input environment map
    Texture *env_map = &renderer->textures[renderer->cubemap_id.id];
    renderer->pContext->CSSetShaderResources(0, 1, env_map->srv.GetAddressOf());

    // Fetch the output texture
    Texture *out_tex = &renderer->textures[renderer->prefilter_map.id];

    // UAV nullptr for reuse
    ID3D11UnorderedAccessView *nulluav = nullptr;

    // Process each mip level
    for (uint32_t mip = 0; mip < total_mips; ++mip) {
        // Calculate roughness for the mip level
        float roughness = mip / static_cast<float>(total_mips - 1);

        // Update constant buffer
        D3D11_MAPPED_SUBRESOURCE mapped;
        renderer->pContext->Map(iblpre_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        CBIBLPrefilter *constants = (CBIBLPrefilter *)mapped.pData;
        constants->current_mip_level = mip;
        constants->total_mip_levels = total_mips;
        constants->roughness = roughness;
        constants->num_samples = (mip == 0) ? 1 : 1024; // Mirror reflection only needs 1 sample
        renderer->pContext->Unmap(iblpre_cb_ptr.Get(), 0);

        // Set the cb on the shader
        renderer->pContext->CSSetConstantBuffers(0, 1, iblpre_cb_ptr.GetAddressOf());

        // Bind output UAV for the mip
        renderer->pContext->CSSetUnorderedAccessViews(0, 1, out_tex->uav[mip].GetAddressOf(), nullptr);

        // Calculate dispatch size
        uint32_t mip_size = MAX(1u, 256u >> mip);
        uint32_t dispatch_x = (mip_size + 7) / 8;
        uint32_t dispatch_y = (mip_size + 7) / 8;
        uint32_t dispatch_z = 6; // 6 faces for cubemap

        // Dispatch compute shader
        renderer->pContext->Dispatch(dispatch_x, dispatch_y, dispatch_z);

        // Clear UAV binding
        renderer->pContext->CSSetUnorderedAccessViews(0, 1, &nulluav, nullptr);
    }

    // Clear all bindings
    ID3D11ShaderResourceView *nullsrv = nullptr;
    renderer->pContext->CSSetShaderResources(0, 1, &nullsrv);
    shader::unbind_pipeline(renderer->pContext.Get());

    return true;
}

bool renderer::generate_BRDF_LUT(Renderer *renderer) {
    ShaderId brdf_lut_cs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/brdf_lut.cs.hlsl",
        SHADER_STAGE_CS,
        "main");

    if (id::is_invalid(brdf_lut_cs)) {
        LOG("%s: Failed to create compute shader for BRDF LUT", __func__);
        return false;
    }

    ShaderId brdf_lut_modules[] = {brdf_lut_cs};
    PipelineId brdf_lut_shader = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        brdf_lut_modules,
        ARRAYSIZE(brdf_lut_modules),
        nullptr, 0);

    if (id::is_invalid(brdf_lut_shader)) {
        LOG("%s: Couldn't create shader pipeline for BRDF LUT", __func__);
        return false;
    }

    renderer->brdf_lut = texture::create(
        512, 512,
        DXGI_FORMAT_R16G16_FLOAT,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
        true,
        nullptr, 0,
        1, 1,
        1, false);

    // Bind shaders
    ShaderPipeline *brdf_lut_pipeline = shader::get_pipeline(&renderer->shader_system, brdf_lut_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), brdf_lut_pipeline);

    // Bind input environment map
    Texture *brdf_tex = &renderer->textures[renderer->brdf_lut.id];
    renderer->pContext->CSSetUnorderedAccessViews(0, 1, brdf_tex->uav[0].GetAddressOf(), nullptr);

    // Dispatch compute shader (512x512 with 8x8 thread groups)
    uint32_t dispatch_x = (512 + 7) / 8;
    uint32_t dispatch_y = (512 + 7) / 8;
    renderer->pContext->Dispatch(dispatch_x, dispatch_y, 1);

    // Cleanup
    ID3D11UnorderedAccessView *nulluav = nullptr;
    renderer->pContext->CSSetUnorderedAccessViews(0, 1, &nulluav, nullptr);
    shader::unbind_pipeline(renderer->pContext.Get());

    return true;
}

void renderer::on_window_resize(Renderer *renderer, uint16_t width, uint16_t height) {
    // Get the renderer state from the application
    // RendererState *state = Application::GetRenderer();

    // TODO: I think main context pointers should be hoisted here
    // like device, swapchain, context... anything we use in this block

    // Early return if we don't have a swapchain
    // TODO: Check if WLR ComPtr work ctx way
    if (!renderer->pSwapChain) {
        return;
    }

    // Unbind render targets from the Output Merger stage
    renderer->pContext->OMSetRenderTargets(0, nullptr, nullptr);

    // Release existing render target view
    // This is safe even if it's null
    renderer->pRenderTargetView.Reset();
    // Release old DSV and buffer texture
    renderer->pDepthStencilView.Reset();
    renderer->pDepthStencilBuffer.Reset();

    // Resize swap chain buffers
    HRESULT hr = renderer->pSwapChain->ResizeBuffers(
        0, // Keep existing buffer count
        width, height,
        DXGI_FORMAT_UNKNOWN, // Keep existing format
        0);

    if (FAILED(hr)) {
        LOG("Renderer error: resource resizing failed");
        return;
    }

    // Recreate size dependent resources
    // TODO: Move to a separate private function so I can use it
    // when initializing everything, as well.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> pBackBuffer;
    hr = renderer->pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr)) {
        LOG("Renderer error: Couldn't get the backbuffer texture");
        return;
    }

    // Now get the render target view (we only need one as we'll only have a single window)
    hr = renderer->pDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, renderer->pRenderTargetView.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create RenderTargetView");
        return;
    }

    // Create Depth Stencil Texture
    D3D11_TEXTURE2D_DESC depthStencilDesc = {};
    depthStencilDesc.Width = width;
    depthStencilDesc.Height = height;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.SampleDesc.Quality = 0;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    hr = renderer->pDevice->CreateTexture2D(&depthStencilDesc, nullptr, renderer->pDepthStencilBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create depth stencil texture");
        return;
    }

    // Create Depth Stencil View from Texture
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = depthStencilDesc.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = renderer->pDevice->CreateDepthStencilView(renderer->pDepthStencilBuffer.Get(), &dsvDesc, renderer->pDepthStencilView.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create depth stencil view");
        return;
    }

    // Resize scene color and depth buffers
    texture::resize(renderer->scene_color, width, height);
    texture::resize(renderer->scene_depth, width, height);
    texture::resize(renderer->z_depth, width, height);

    // Resize FXAA pass
    texture::resize(renderer->fxaa_color, width, height);

    // Resize bloom mips
    uint32_t bmw = width;
    uint32_t bmh = height;
    for (uint8_t i = 0; i < renderer->mip_count; ++i) {
        bmw = MAX(bmw / 2, 1);
        bmh = MAX(bmh / 2, 1);
        texture::resize(renderer->bloom_mips[i], bmw, bmh);
    }

    // Set the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    renderer->pContext->RSSetViewports(1, &vp);

    // Tell the camera about the new aspect ratio
    Scene *scenes = application::get_scenes();
    scene::camera_set_active_aspect_ratio(&scenes[0], width / (float)height);
}

ID3D11Device *renderer::get_device(Renderer *renderer) {
    return renderer->pDevice.Get();
}

static bool create_default_shaders(Renderer *renderer) {
    // Create default fullscreen triangle shader module
    renderer->fullscreen_triangle_vs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"src/shaders/triangle.vs.hlsl",
        SHADER_STAGE_VS,
        "main");

    if (id::is_invalid(renderer->fullscreen_triangle_vs)) {
        LOG("%s: Failed to create shader module for fullscreen triangle", __func__);
        return false;
    }

    return true;
}

static bool create_pipeline_states(Renderer *renderer, ID3D11Device *device) {
    // --- Rasterizer States ---
    {
        CD3D11_RASTERIZER_DESC base(D3D11_DEFAULT);
        {

            CD3D11_RASTERIZER_DESC desc = base;
            device->CreateRasterizerState(&desc, &renderer->rasterizer_states[RASTER_SOLID_BACKFACE]);
        }

        {
            CD3D11_RASTERIZER_DESC desc = base;
            desc.CullMode = D3D11_CULL_FRONT;
            device->CreateRasterizerState(&desc, &renderer->rasterizer_states[RASTER_SOLID_FRONTFACE]);
        }

        {
            CD3D11_RASTERIZER_DESC desc = base;
            desc.CullMode = D3D11_CULL_NONE;
            device->CreateRasterizerState(&desc, &renderer->rasterizer_states[RASTER_SOLID_NONE]);
        }

        {
            CD3D11_RASTERIZER_DESC desc = base;
            desc.FillMode = D3D11_FILL_WIREFRAME;
            desc.CullMode = D3D11_CULL_NONE;
            device->CreateRasterizerState(&desc, &renderer->rasterizer_states[RASTER_WIREFRAME]);
        }

        {
            CD3D11_RASTERIZER_DESC desc = base;
            desc.DepthBias = 1000;
            desc.SlopeScaledDepthBias = 2.0f;
            desc.DepthBiasClamp = 0.0f;
            device->CreateRasterizerState(&desc, &renderer->rasterizer_states[RASTER_SHADOW_DEPTH_BIAS]);
        }

        {
            CD3D11_RASTERIZER_DESC desc = base;
            desc.FrontCounterClockwise = TRUE;
            device->CreateRasterizerState(&desc, &renderer->rasterizer_states[RASTER_REVERSE_Z]);
        }
    }

    // --- Depth-Stencil States ---
    {
        CD3D11_DEPTH_STENCIL_DESC base(D3D11_DEFAULT);
        {
            CD3D11_DEPTH_STENCIL_DESC desc = base;
            device->CreateDepthStencilState(&desc, &renderer->depth_states[DEPTH_DEFAULT]);
        }

        {
            CD3D11_DEPTH_STENCIL_DESC desc = base;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            device->CreateDepthStencilState(&desc, &renderer->depth_states[DEPTH_READ_ONLY]);
        }

        {
            CD3D11_DEPTH_STENCIL_DESC desc = base;
            desc.DepthEnable = FALSE;
            device->CreateDepthStencilState(&desc, &renderer->depth_states[DEPTH_NONE]);
        }

        {
            CD3D11_DEPTH_STENCIL_DESC desc = base;
            desc.DepthFunc = D3D11_COMPARISON_GREATER;
            device->CreateDepthStencilState(&desc, &renderer->depth_states[DEPTH_REVERSE_Z]);
        }

        {
            CD3D11_DEPTH_STENCIL_DESC desc = base;
            desc.DepthFunc = D3D11_COMPARISON_EQUAL;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            device->CreateDepthStencilState(&desc, &renderer->depth_states[DEPTH_EQUAL_ONLY]);
        }

        {
            CD3D11_DEPTH_STENCIL_DESC desc = base;
            desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            device->CreateDepthStencilState(&desc, &renderer->depth_states[DEPTH_LESS_EQUAL_NO_WRITE]);
        }
    }

    // --- Blend States ---
    {
        CD3D11_BLEND_DESC base(D3D11_DEFAULT);
        {
            CD3D11_BLEND_DESC desc = base;
            device->CreateBlendState(&desc, &renderer->blend_states[BLEND_OPAQUE]);
        }

        {
            CD3D11_BLEND_DESC desc = base;
            desc.RenderTarget[0].BlendEnable = TRUE;
            desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            device->CreateBlendState(&desc, &renderer->blend_states[BLEND_ALPHA]);
        }

        {
            CD3D11_BLEND_DESC desc = base;
            desc.RenderTarget[0].BlendEnable = TRUE;
            desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            device->CreateBlendState(&desc, &renderer->blend_states[BLEND_ADDITIVE]);
        }

        {
            CD3D11_BLEND_DESC desc = base;
            desc.RenderTarget[0].BlendEnable = TRUE;
            desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            device->CreateBlendState(&desc, &renderer->blend_states[BLEND_PREMULTIPLIED_ALPHA]);
        }

        {
            CD3D11_BLEND_DESC desc = base;
            desc.RenderTarget[0].BlendEnable = FALSE;
            desc.RenderTarget[0].RenderTargetWriteMask = 0;
            device->CreateBlendState(&desc, &renderer->blend_states[BLEND_DISABLE_WRITE]);
        }
    }

    // --- Sampler States ---
    {
        CD3D11_SAMPLER_DESC base(D3D11_DEFAULT);
        {
            CD3D11_SAMPLER_DESC desc = base;
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            device->CreateSamplerState(&desc, &renderer->sampler_states[SAMPLER_LINEAR_WRAP]);
        }

        {
            CD3D11_SAMPLER_DESC desc = base;
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            device->CreateSamplerState(&desc, &renderer->sampler_states[SAMPLER_LINEAR_CLAMP]);
        }

        {
            CD3D11_SAMPLER_DESC desc = base;
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            device->CreateSamplerState(&desc, &renderer->sampler_states[SAMPLER_POINT_WRAP]);
        }

        {
            CD3D11_SAMPLER_DESC desc = base;
            desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            device->CreateSamplerState(&desc, &renderer->sampler_states[SAMPLER_POINT_CLAMP]);
        }

        {
            CD3D11_SAMPLER_DESC desc = base;
            desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            desc.BorderColor[0] = 1.0f;
            desc.BorderColor[1] = 1.0f;
            desc.BorderColor[2] = 1.0f;
            desc.BorderColor[3] = 1.0f;
            device->CreateSamplerState(&desc, &renderer->sampler_states[SAMPLER_SHADOW_COMPARISON]);
        }

        {
            CD3D11_SAMPLER_DESC desc = base;
            desc.Filter = D3D11_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = 16;
            desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            device->CreateSamplerState(&desc, &renderer->sampler_states[SAMPLER_ANISOTROPIC_WRAP]);
        }
    }

    return true;
}

static bool resolve_msaa_texture(ID3D11DeviceContext *context, Texture *src, Texture *dst) {
    context->ResolveSubresource(
        dst->texture.Get(), 0,
        src->texture.Get(), 0,
        src->format);

    return true;
}
