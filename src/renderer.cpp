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

#ifndef MIN
#define MIN(a, b) (a < b ? a : b)
#endif
#ifndef MAX
#define MAX(a, b) (a > b ? a : b)
#endif

// Static functions
static bool create_default_shaders(Renderer *renderer);

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

    // Create shader pipeline for PBR rendering
    renderer->pbr_shader = create_pbr_shader_pipeline(renderer);
    if (id::is_invalid(renderer->pbr_shader)) {
        LOG("%s: Couldn't create PBR shader pipeline", __func__);
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

    // Create textures for bloom pass
    UINT mip_width = pWindow->width / 2;
    UINT mip_height = pWindow->height / 2;
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

    hr = renderer->pDevice->CreateBuffer(&bloom_cb_desc, nullptr, renderer->bloom_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for bloom pass");
        return false;
    }

    renderer->fxaa_color = texture::create(
        pWindow->width, pWindow->height,
        DXGI_FORMAT_R16G16B16A16_FLOAT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
        true,
        nullptr, 0,
        1, 1, 1, false);
    if (id::is_invalid(renderer->fxaa_color)) {
        LOG("Renderer error: Couldn't initialize texture for FXAA pass");
        return false;
    }

    D3D11_BUFFER_DESC fxaa_cb_desc = {};
    fxaa_cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    fxaa_cb_desc.ByteWidth = sizeof(FXAAConstants);
    fxaa_cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    fxaa_cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = renderer->pDevice->CreateBuffer(&fxaa_cb_desc, nullptr, renderer->fxaa_cb_ptr.GetAddressOf());
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to create constant buffer for bloom pass");
        return false;
    }

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

    return true;
}

void renderer::shutdown(Renderer *renderer) {
    (void)renderer;
}

PipelineId renderer::create_pbr_shader_pipeline(Renderer *renderer) {
    ShaderId pbr_vs = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"assets/default_vs.hlsl",
        SHADER_STAGE_VS,
        "main");
    ShaderId pbr_ps = shader::create_module_from_file(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        L"assets/default_ps.hlsl",
        SHADER_STAGE_PS,
        "main");

    // Define the pbr input layout
    D3D11_INPUT_ELEMENT_DESC pbr_input_desc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    // Modules in the PBR pipeline
    ShaderId pbr_modules[] = {pbr_vs, pbr_ps};

    PipelineId pbr_pipeline = shader::create_pipeline(
        &renderer->shader_system,
        renderer->pDevice.Get(),
        pbr_modules,
        ARRAYSIZE(pbr_modules),
        pbr_input_desc,
        ARRAYSIZE(pbr_input_desc));

    return pbr_pipeline;
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

    return true;
}

PipelineId renderer::create_fxaa_pipeline(Renderer *renderer) {
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
        1, 1, 1, false);

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

void renderer::begin_frame(Renderer *renderer) {
    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Bind the sampler
    context->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());
    // Bind the default blend state
    context->OMSetBlendState(renderer->pDefaultBS.Get(), nullptr, 0xFFFFFFFF);

    context->OMSetDepthStencilState(renderer->pDepthStencilState.Get(), 0);
    context->RSSetState(renderer->default_raster.Get());
}

void renderer::end_frame(Renderer *renderer) {
    renderer->pSwapChain->Present(1, 0);
}

void renderer::render(Renderer *renderer, Scene *scene) {
    // Bind and clear the main pass' targets
    // float clearColor[4] = {0.36f, 0.36f, 0.36f, 1.0f};
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Texture *scene_color_tex = &renderer->textures[renderer->scene_color.id];
    Texture *scene_depth_tex = &renderer->textures[renderer->scene_depth.id];
    bind_render_target(renderer, scene_color_tex->rtv[0].Get(), scene_depth_tex->dsv.Get());
    clear_render_target(renderer, scene_color_tex->rtv[0].Get(), scene_depth_tex->dsv.Get(), clearColor);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(scene_color_tex->width);
    viewport.Height = static_cast<float>(scene_color_tex->height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    renderer->pContext->RSSetViewports(1, &viewport);

    // Bind the depth stencil state
    renderer->pContext->OMSetDepthStencilState(renderer->pDepthStencilState.Get(), 1);

    // Rendering scene - meshes, materials...etc.
    // render_scene(renderer, scene);

    // Forward+ rendering
    render_depth_prepass(renderer, scene);
    render_forward_plus_opaque(renderer, scene);

    // Deferred rendering
    render_gbuffer(renderer, scene);
    render_lighting_pass(renderer, scene);

    // Render the environment map
    render_skybox(renderer, scene->active_cam);

    // Render FXAA pass
    // render_fxaa_pass(renderer);

    // Render bloom pass
    render_bloom_pass(renderer);

    // Bind and clear the tonemap pass' targets (which are the swapchain's backbuffer)
    bind_render_target(renderer, renderer->pRenderTargetView.Get(), nullptr);
    clear_render_target(renderer, renderer->pRenderTargetView.Get(), nullptr, clearColor);

    // Render the tonemap pass
    render_tonemap_pass(renderer);
}

void renderer::render_scene(Renderer *renderer, Scene *scene) {
    // Update per frame constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = renderer->pContext->Map(renderer->pCBPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to map per object constant buffer");
        return;
    }
    CBPerFrame *perFramePtr = (CBPerFrame *)mappedResource.pData;
    perFramePtr->viewProjectionMatrix = scene::camera_get_view_projection_matrix(scene->active_cam);
    perFramePtr->camera_position = scene->active_cam->position;
    renderer->pContext->Unmap(renderer->pCBPerFrame.Get(), 0);
    renderer->pContext->VSSetConstantBuffers(0, 1, renderer->pCBPerFrame.GetAddressOf());

    // Bind the PBR shader
    ShaderPipeline *pbr_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->pbr_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), pbr_pipeline);

    // Grab the irradiance map
    Texture *irradiance_tex = &renderer->textures[renderer->irradiance_cubemap.id];
    // Grab the prefilter map
    Texture *prefilter_tex = &renderer->textures[renderer->prefilter_map.id];
    // Grab the BRDF LUT
    Texture *brdf_lut_tex = &renderer->textures[renderer->brdf_lut.id];
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
            material::bind(renderer, mat, ARRAYSIZE(env_srvs));

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
}

void renderer::render_gbuffer(Renderer *renderer, Scene *scene) {
    // Bind the pipeline
    ShaderPipeline *gbuffer_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->gbuffer_pipeline);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), gbuffer_pipeline);

    // Set some of the states...
    renderer->pContext->OMSetDepthStencilState(renderer->pDepthStencilState.Get(), 0);
    renderer->pContext->RSSetState(renderer->default_raster.Get());
    renderer->pContext->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());

    // Update per frame constants
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = renderer->pContext->Map(renderer->pCBPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to map per object constant buffer");
        return;
    }
    CBPerFrame *perFramePtr = (CBPerFrame *)mappedResource.pData;
    perFramePtr->viewProjectionMatrix = scene::camera_get_view_projection_matrix(scene->active_cam);
    // TODO: This won't be needed for the gbuffer
    perFramePtr->camera_position = scene->active_cam->position;

    renderer->pContext->Unmap(renderer->pCBPerFrame.Get(), 0);
    renderer->pContext->VSSetConstantBuffers(0, 1, renderer->pCBPerFrame.GetAddressOf());

    // Bind render targets
    Texture *rtv0 = texture::get(renderer, renderer->gbuffer_rt0);
    Texture *rtv1 = texture::get(renderer, renderer->gbuffer_rt1);
    Texture *rtv2 = texture::get(renderer, renderer->gbuffer_rt2);
    Texture *depth = texture::get(renderer, renderer->scene_depth);

    ID3D11RenderTargetView *rtvs[] = {rtv0->rtv[0].Get(), rtv1->rtv[0].Get(), rtv2->rtv[0].Get()};

    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    renderer->pContext->ClearRenderTargetView(rtvs[0], clear_color);
    renderer->pContext->ClearRenderTargetView(rtvs[1], clear_color);
    renderer->pContext->ClearRenderTargetView(rtvs[2], clear_color);
    renderer->pContext->ClearDepthStencilView(depth->dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    renderer->pContext->OMSetRenderTargets(ARRAYSIZE(rtvs), rtvs, depth->dsv.Get());

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
            material::bind(renderer, mat, 0);

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

    // Unbind RTV's
    ID3D11RenderTargetView *nullRTVs[8] = {nullptr};
    renderer->pContext->OMSetRenderTargets(_countof(nullRTVs), nullRTVs, nullptr);
}

void renderer::render_lighting_pass(Renderer *renderer, Scene *scene) {
    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Bind the pipeline
    ShaderPipeline *lighting_pass_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->lighting_pass_pipeline);
    shader::bind_pipeline(&renderer->shader_system, context, lighting_pass_pipeline);

    // No depth testing
    renderer->pContext->OMSetDepthStencilState(nullptr, 0);

    // Fetch G-buffer SRV's
    Texture *albedo_roughness = texture::get(renderer, renderer->gbuffer_rt0);
    Texture *world_normal = texture::get(renderer, renderer->gbuffer_rt1);
    Texture *emission_metallic = texture::get(renderer, renderer->gbuffer_rt2);
    if (!albedo_roughness || !world_normal || !emission_metallic) {
        return;
    }

    // Fetch the depth texture
    Texture *depth_tex = texture::get(renderer, renderer->scene_depth);
    if (!depth_tex) return;

    // Fetch the IBL
    Texture *irradiance_tex = texture::get(renderer, renderer->irradiance_cubemap);
    Texture *prefilter_map = texture::get(renderer, renderer->prefilter_map);
    Texture *brdf_lut = texture::get(renderer, renderer->brdf_lut);
    if (!irradiance_tex || !prefilter_map || !brdf_lut) {
        return;
    }

    ID3D11ShaderResourceView *srvs[] = {
        albedo_roughness->srv.Get(),
        world_normal->srv.Get(),
        emission_metallic->srv.Get(),
        depth_tex->srv.Get(),
        irradiance_tex->srv.Get(),
        prefilter_map->srv.Get(),
        brdf_lut->srv.Get(),
    };

    // Bind the SRVs
    context->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    // Bind the RTV
    Texture *lighting_rt = texture::get(renderer, renderer->scene_color);
    if (!lighting_rt) return;
    context->OMSetRenderTargets(1, lighting_rt->rtv[0].GetAddressOf(), nullptr);

    // Bind the constant buffer with Camera Position and Inverse View Projection Matrix
    DirectX::XMFLOAT4X4 view_projection = scene::camera_get_view_projection_matrix(scene->active_cam);
    DirectX::XMMATRIX inv_view_projection = DirectX::XMMatrixInverse(nullptr, DirectX::XMLoadFloat4x4(&view_projection));

    D3D11_MAPPED_SUBRESOURCE map;
    context->Map(renderer->lp_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    ((CBLighting *)map.pData)->camera_position = scene->active_cam->position;
    DirectX::XMStoreFloat4x4(&((CBLighting *)map.pData)->inv_view_projection, inv_view_projection);
    context->Unmap(renderer->lp_cb_ptr.Get(), 0);
    context->PSSetConstantBuffers(0, 1, renderer->lp_cb_ptr.GetAddressOf());

    context->Draw(3, 0);

    // Unbind SRVs
    ID3D11ShaderResourceView *nullSRVs[8] = {nullptr};
    context->PSSetShaderResources(0, 8, nullSRVs);
}

void renderer::render_bloom_pass(Renderer *renderer) {
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    BloomConstants bloom_constants = {};
    bloom_constants.bloom_threshold = 1.5f;
    bloom_constants.bloom_intensity = 1.0f;
    bloom_constants.bloom_knee = 0.2f;

    // For convenience but could be useful to bypass some dereferencing as well
    ID3D11DeviceContext *context = renderer->pContext.Get();

    ID3D11ShaderResourceView *scene_srv = renderer->textures[renderer->scene_color.id].srv.Get();
    // ID3D11ShaderResourceView *scene_srv = renderer->textures[renderer->fxaa_color.id].srv.Get();

    // 1. Threshold pass
    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(renderer->bloom_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &bloom_constants, sizeof(BloomConstants));
    context->Unmap(renderer->bloom_cb_ptr.Get(), 0);

    // Fetch the threshold pipeline from id
    ShaderPipeline *threshold_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->bloom_threshold_shader);
    shader::bind_pipeline(&renderer->shader_system, context, threshold_pipeline);

    Texture *bloom_mip_1 = &renderer->textures[renderer->bloom_mips[0].id];
    context->ClearRenderTargetView(bloom_mip_1->rtv[0].Get(), clear_color);
    context->OMSetRenderTargets(1, bloom_mip_1->rtv[0].GetAddressOf(), nullptr);
    context->PSSetShaderResources(0, 1, &scene_srv);
    context->PSSetConstantBuffers(0, 1, renderer->bloom_cb_ptr.GetAddressOf());

    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)bloom_mip_1->width;
    viewport.Height = (float)bloom_mip_1->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    context->Draw(3, 0);

    // 2. Downsample chain
    ShaderPipeline *downsample_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->bloom_downsample_shader);
    shader::bind_pipeline(&renderer->shader_system, context, downsample_pipeline);
    for (int i = 1; i < renderer->mip_count; ++i) {
        Texture *bloom_mip = &renderer->textures[renderer->bloom_mips[i].id];

        // Update texel size for current target mip
        bloom_constants.texel_size[0] = 1.0f / bloom_mip->width;
        bloom_constants.texel_size[1] = 1.0f / bloom_mip->height;

        context->Map(renderer->bloom_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &bloom_constants, sizeof(BloomConstants));
        context->Unmap(renderer->bloom_cb_ptr.Get(), 0);

        viewport.Width = (float)bloom_mip->width;
        viewport.Height = (float)bloom_mip->height;
        context->RSSetViewports(1, &viewport);

        context->ClearRenderTargetView(bloom_mip->rtv[0].Get(), clear_color);
        context->OMSetRenderTargets(1, bloom_mip->rtv[0].GetAddressOf(), nullptr);
        context->PSSetShaderResources(0, 1, renderer->textures[renderer->bloom_mips[i - 1].id].srv.GetAddressOf());
        context->Draw(3, 0);
    }

    // 3. Upsample chain -- with additive blend state
    context->OMSetBlendState(renderer->pAdditiveBS.Get(), nullptr, 0xFFFFFFFF);
    ShaderPipeline *upsample_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->bloom_upsample_shader);
    shader::bind_pipeline(&renderer->shader_system, context, upsample_pipeline);
    for (int i = renderer->mip_count - 2; i >= 0; --i) {
        Texture *bloom_mip = &renderer->textures[renderer->bloom_mips[i].id];

        // Update texel size for current target mip
        bloom_constants.texel_size[0] = 1.0f / bloom_mip->width;
        bloom_constants.texel_size[1] = 1.0f / bloom_mip->height;

        // Add weights into the mix so each layer can gradually be less intense
        int upsample_index = renderer->mip_count - 2 - i; // Goes 0 → N-2
        float t = upsample_index / float(renderer->mip_count - 2);
        // Smoothstep function
        float smooth = t * t * (3.0f - 2.0f * t);
        // Lerp from full (1.0) to low (0.5)
        bloom_constants.bloom_mip_strength = std::lerp(1.0f, 0.2f, smooth);
        // LOG("%f", bloom_constants.bloom_mip_strength);

        context->Map(renderer->bloom_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, &bloom_constants, sizeof(BloomConstants));
        context->Unmap(renderer->bloom_cb_ptr.Get(), 0);

        viewport.Width = (float)bloom_mip->width;
        viewport.Height = (float)bloom_mip->height;
        context->RSSetViewports(1, &viewport);

        // No clear, we are adding existing content
        context->OMSetRenderTargets(1, bloom_mip->rtv[0].GetAddressOf(), nullptr);
        context->PSSetShaderResources(0, 1, renderer->textures[renderer->bloom_mips[i + 1].id].srv.GetAddressOf());
        context->Draw(3, 0);
    }

    context->OMSetBlendState(renderer->pDefaultBS.Get(), nullptr, 0xFFFFFFFF);
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

    context->PSSetConstantBuffers(0, 1, renderer->fxaa_cb_ptr.GetAddressOf());

    D3D11_VIEWPORT viewport = {};
    viewport.Width = (float)fxaa_texture->width;
    viewport.Height = (float)fxaa_texture->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    context->Draw(3, 0);
}

void renderer::render_tonemap_pass(Renderer *renderer) {
    // Bind the shader for the tonemap pass
    ShaderPipeline *tonemap_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->tonemap_shader);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), tonemap_pipeline);

    // TODO: We are not setting the primitive topology anywhere visible, only in mesh::draw
    // which is fine for now as D3D11 is a statemachine so we are keeping that state
    // but we should probably set it somewhere explicitly and clearly.
    // context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind previous render target as texture
    // Texture *scene_color_tex = &renderer->textures[renderer->fxaa_color.id];
    Texture *scene_color_tex = &renderer->textures[renderer->scene_color.id];
    Texture *bloom_tex = &renderer->textures[renderer->bloom_mips[0].id];
    ID3D11ShaderResourceView *srvs[] = {scene_color_tex->srv.Get(), bloom_tex->srv.Get()};
    renderer->pContext->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    D3D11_VIEWPORT viewport = {};
    // TODO: These should be sizes not for INPUT but OUTPUT textures, but it's
    // basically the same for now
    viewport.Width = (float)scene_color_tex->width;
    viewport.Height = (float)scene_color_tex->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    renderer->pContext->RSSetViewports(1, &viewport);

    // Draw the triangle
    renderer->pContext->Draw(3, 0);

    // This might be required by D3D11 to unbind resources before we use it as
    // render targets again but so far there is no complaining so just leaving
    // it here as a reminder if something happens down the line...
    // ID3D11ShaderResourceView *nullSRVs[1] = {nullptr};
    // renderer->pContext->PSSetShaderResources(0, 1, nullSRVs);
}

void renderer::render_skybox(Renderer *renderer, SceneCamera *camera) {
    ShaderPipeline *skybox_shader = shader::get_pipeline(&renderer->shader_system, renderer->skybox_shader);
    if (!skybox_shader) {
        LOG("%s: Warning! Skybox shader couldn't be retrieved. Skipping.", __func__);
        return;
    }

    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Bind the skybox shader
    shader::bind_pipeline(&renderer->shader_system, context, skybox_shader);

    // Bind the RTV
    Texture *scene_rt = texture::get(renderer, renderer->scene_color);
    Texture *depth = texture::get(renderer, renderer->scene_depth);
    if (!scene_rt) return;
    context->OMSetRenderTargets(1, scene_rt->rtv[0].GetAddressOf(), depth->dsv.Get());

    // Bind the skybox states
    context->OMSetDepthStencilState(renderer->skybox_depth_state.Get(), 0);
    context->RSSetState(renderer->skybox_raster.Get());
    context->PSSetSamplers(0, 1, renderer->skybox_sampler.GetAddressOf());

    // Set the environment texture as an input
    Texture *env_map = &renderer->textures[renderer->cubemap_id.id];
    context->PSSetShaderResources(0, 1, env_map->srv.GetAddressOf());

    // Set the camera view and projections
    CBSkybox skybox_cb = {};
    skybox_cb.view_matrix = scene::camera_get_view_matrix(camera);
    skybox_cb.projection_matrix = scene::camera_get_projection_matrix(camera);

    D3D11_MAPPED_SUBRESOURCE mapped;
    context->Map(renderer->skybox_cb_ptr.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &skybox_cb, sizeof(CBSkybox));
    context->Unmap(renderer->skybox_cb_ptr.Get(), 0);
    context->VSSetConstantBuffers(0, 1, renderer->skybox_cb_ptr.GetAddressOf());

    // Draw cube hardcoded into vertex shader
    context->Draw(36, 0);

    // Reset states?
    renderer->pContext->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());
    renderer->pContext->RSSetState(renderer->default_raster.Get());
}

void renderer::render_depth_prepass(Renderer *renderer, Scene *scene) {
    // TODO: Move this into the render context so can be called again and again for debug
    ID3DUserDefinedAnnotation *annotation = nullptr;
    renderer->pContext->QueryInterface(__uuidof(ID3DUserDefinedAnnotation), (void **)&annotation);
    annotation->BeginEvent(L"Depth Prepass");

    // Bind the pipeline
    ShaderPipeline *zpass_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->zpass_pipeline);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), zpass_pipeline);

    // Set some of the states...
    renderer->pContext->OMSetDepthStencilState(renderer->pDepthStencilState.Get(), 0);
    renderer->pContext->RSSetState(renderer->default_raster.Get());

    // Update per frame constants
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = renderer->pContext->Map(renderer->pCBPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to map per object constant buffer");
        return;
    }
    // TODO: At one point I will have to NOT repeat Per Frame CB's...!
    CBPerFrame *perFramePtr = (CBPerFrame *)mappedResource.pData;
    perFramePtr->viewProjectionMatrix = scene::camera_get_view_projection_matrix(scene->active_cam);
    perFramePtr->camera_position = scene->active_cam->position;
    renderer->pContext->Unmap(renderer->pCBPerFrame.Get(), 0);
    renderer->pContext->VSSetConstantBuffers(0, 1, renderer->pCBPerFrame.GetAddressOf());

    // Bind the depth texture
    Texture *depth = texture::get(renderer, renderer->z_depth);
    renderer->pContext->ClearDepthStencilView(depth->dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    renderer->pContext->OMSetRenderTargets(0, nullptr, depth->dsv.Get());

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

    // Unbind RTV's
    ID3D11RenderTargetView *nullRTVs[8] = {nullptr};
    renderer->pContext->OMSetRenderTargets(_countof(nullRTVs), nullRTVs, nullptr);

    annotation->EndEvent();
}

void renderer::render_forward_plus_opaque(Renderer *renderer, Scene *scene) {
    renderer->pContext->OMSetDepthStencilState(renderer->skybox_depth_state.Get(), 0);

    // Update per frame constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = renderer->pContext->Map(renderer->pCBPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        LOG("Renderer error: Failed to map per object constant buffer");
        return;
    }
    CBPerFrame *perFramePtr = (CBPerFrame *)mappedResource.pData;
    perFramePtr->viewProjectionMatrix = scene::camera_get_view_projection_matrix(scene->active_cam);
    perFramePtr->camera_position = scene->active_cam->position;
    renderer->pContext->Unmap(renderer->pCBPerFrame.Get(), 0);
    renderer->pContext->VSSetConstantBuffers(0, 1, renderer->pCBPerFrame.GetAddressOf());

    // Bind the PBR shader
    ShaderPipeline *fp_opaque_pipeline = shader::get_pipeline(&renderer->shader_system, renderer->fp_opaque_pipeline);
    shader::bind_pipeline(&renderer->shader_system, renderer->pContext.Get(), fp_opaque_pipeline);

    // Grab the irradiance map
    Texture *irradiance_tex = &renderer->textures[renderer->irradiance_cubemap.id];
    // Grab the prefilter map
    Texture *prefilter_tex = &renderer->textures[renderer->prefilter_map.id];
    // Grab the BRDF LUT
    Texture *brdf_lut_tex = &renderer->textures[renderer->brdf_lut.id];
    ID3D11ShaderResourceView *env_srvs[] = {irradiance_tex->srv.Get(), prefilter_tex->srv.Get(), brdf_lut_tex->srv.Get()};
    if (prefilter_tex && irradiance_tex && brdf_lut_tex) {
        renderer->pContext->PSSetShaderResources(0, ARRAYSIZE(env_srvs), env_srvs);
    }

    // Bind the RTV
    Texture *scene_rt = texture::get(renderer, renderer->scene_color);
    Texture *depth = texture::get(renderer, renderer->z_depth);
    if (!scene_rt) return;
    renderer->pContext->OMSetRenderTargets(1, scene_rt->rtv[0].GetAddressOf(), depth->dsv.Get());

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
            material::bind(renderer, mat, ARRAYSIZE(env_srvs));

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
    TextureId hdri = texture::load_hdr("assets/photo_studio_loft_hall_4k.hdr");
    // TextureId hdri = texture::load_hdr("assets/metal_studio_23.hdr");
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
