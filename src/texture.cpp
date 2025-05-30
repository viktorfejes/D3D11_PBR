#include "texture.hpp"

#include "application.hpp"
#include "id.hpp"
#include "logger.hpp"
#include "renderer.hpp"

#include <comdef.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

TextureId texture::load(const char *filename, bool is_srgb) {

    // stbi_set_flip_vertically_on_load(1);

    int h, w, c;
    uint8_t *image_data = stbi_load(filename, &w, &h, &c, 4);
    if (!image_data) {
        LOG("texture::load: stbi_load didn't return with expected data");
        return id::invalid();
    }

    // Create the texture
    TextureId new_tex = create(w, h,
                               is_srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
                               D3D11_BIND_SHADER_RESOURCE,
                               true,
                               image_data,
                               w * 4,
                               1,
                               1,
                               false);

    stbi_image_free(image_data);

    return new_tex;
}

TextureId texture::load_hdr(const char *filename) {
    int h, w, c;
    float *hdr_data = stbi_loadf(filename, &w, &h, &c, 3);
    if (!hdr_data) {
        LOG("texture::load_hdr: stbi_load didn't return with expected data");
        return id::invalid();
    }

    // Create the texture
    TextureId new_tex = create(w, h,
                               DXGI_FORMAT_R32G32B32_FLOAT,
                               D3D11_BIND_SHADER_RESOURCE,
                               true,
                               hdr_data,
                               w * 3 * sizeof(float),
                               1,
                               1,
                               false);

    stbi_image_free(hdr_data);

    return new_tex;
    
}

TextureId texture::load_from_data(uint8_t *image_data, uint16_t width, uint16_t height) {
    Renderer *renderer = application::get_renderer();

    // Check if we can find an empty slot for our texture
    // by linear search (which for this size is probably the best)
    Texture *t = nullptr;
    for (uint8_t i = 0; i < MAX_TEXTURES; ++i) {
        if (id::is_invalid(renderer->textures[i].id)) {
            t = &renderer->textures[i];
            t->id.id = i;
            break;
        }
    }

    if (t == nullptr) {
        LOG("texture::load_from_data: Max textures reached, adjust max texture count.");
        return id::invalid();
    }

    t->width = width;
    t->height = height;

    // Upload to GPU straight away
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = t->width;
    desc.Height = t->height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Quality = 0;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    // Assume shader texture for now...
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    // Initial data
    D3D11_SUBRESOURCE_DATA data = {};
    data.pSysMem = image_data;
    data.SysMemPitch = t->width * 4;

    // Create the texture
    HRESULT hr = renderer->pDevice->CreateTexture2D(&desc, &data, t->texture.GetAddressOf());
    if (FAILED(hr)) {
        LOG("texture::load_from_data: Failed to create Texture2D on the gpu");
        stbi_image_free(image_data);
        return id::invalid();
    }

    // Create SRV for the texture as we are treating this
    // as shader resource only for now
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    srv_desc.Texture2D.MostDetailedMip = 0;

    // Create the SRV
    hr = renderer->pDevice->CreateShaderResourceView((ID3D11Resource *)t->texture.Get(), &srv_desc, t->srv.GetAddressOf());
    if (FAILED(hr)) {
        LOG("texture::load_from_data: Failed to create Shader Resource View for texture");
        return id::invalid();
    }

    return t->id;
}

TextureId texture::create(uint16_t width,
                          uint16_t height,
                          DXGI_FORMAT format,
                          uint32_t bind_flags,
                          bool generate_srv,
                          const void *initial_data,
                          uint32_t row_pitch,
                          uint32_t array_size,
                          uint32_t mip_levels,
                          bool is_cubemap) {
    // Get a pointer to the renderer as that is our registry for textures.
    // Textures currently only exist as GPU data, so it makes sense. For now
    Renderer *renderer = application::get_renderer();

    // Check if we can find an empty slot for our texture
    // by linear search (which for this size is probably the best)
    Texture *t = nullptr;
    for (uint8_t i = 0; i < MAX_TEXTURES; ++i) {
        if (id::is_invalid(renderer->textures[i].id)) {
            t = &renderer->textures[i];
            t->id.id = i;
            break;
        }
    }

    if (t == nullptr) {
        LOG("texture::load: Max textures reached, adjust max texture count.");
        return id::invalid();
    }

    // Fill out the out texture's width and height members
    // using the returned stb_image values
    t->width = width;
    t->height = height;

    // Upload to GPU straight away
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = t->width;
    desc.Height = t->height;
    desc.MipLevels = mip_levels;
    desc.ArraySize = array_size;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind_flags;
    if (is_cubemap) {
        desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
    }

    // Initial data
    D3D11_SUBRESOURCE_DATA init_data = {};
    D3D11_SUBRESOURCE_DATA *data_ptr = nullptr;
    if (initial_data && row_pitch > 0) {
        init_data.pSysMem = initial_data;
        init_data.SysMemPitch = row_pitch;
        data_ptr = &init_data;
    }

    // Create the texture
    HRESULT hr = renderer->pDevice->CreateTexture2D(&desc, data_ptr, t->texture.GetAddressOf());
    if (FAILED(hr)) {
        _com_error err(hr);
        LPCTSTR err_msg = err.ErrorMessage();
        LOG("%s: Failed to create Texture2D. HRESULT: 0x%lX. Message: %s", __func__, hr, err_msg);
        return id::invalid();
    }

    if (generate_srv && (bind_flags & D3D11_BIND_SHADER_RESOURCE)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = format;

        if (is_cubemap) {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srv_desc.TextureCube.MipLevels = mip_levels;
            srv_desc.TextureCube.MostDetailedMip = 0;
        } else if (array_size > 1) {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srv_desc.Texture2DArray.MipLevels = mip_levels;
            srv_desc.Texture2DArray.ArraySize = array_size;
            srv_desc.Texture2DArray.FirstArraySlice = 0;
            srv_desc.Texture2DArray.MostDetailedMip = 0;
        } else {
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = mip_levels;
            srv_desc.Texture2D.MostDetailedMip = 0;
        }

        // Create the SRV
        hr = renderer->pDevice->CreateShaderResourceView((ID3D11Resource *)t->texture.Get(), &srv_desc, t->srv.GetAddressOf());
        if (FAILED(hr)) {
            LOG("texture::load: Failed to create Shader Resource View for texture.");
            return id::invalid();
        }
    }

    if (bind_flags & D3D11_BIND_UNORDERED_ACCESS) {
        for (uint32_t mip = 0; mip < mip_levels; ++mip) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.Format = format;

            if (is_cubemap) {
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                uav_desc.Texture2DArray.MipSlice = mip;
                uav_desc.Texture2DArray.FirstArraySlice = 0;
                uav_desc.Texture2DArray.ArraySize = 6;
            } else if (array_size > 1) {
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
                uav_desc.Texture2DArray.MipSlice = mip;
                uav_desc.Texture2DArray.FirstArraySlice = 0;
                uav_desc.Texture2DArray.ArraySize = array_size;
            } else {
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Texture2D.MipSlice = mip;
            }

            // Create the UAV
            hr = renderer->pDevice->CreateUnorderedAccessView((ID3D11Resource *)t->texture.Get(), &uav_desc, t->uav[mip].GetAddressOf());
            if (FAILED(hr)) {
                LOG("%s: Failed to create Unordered Access View for texture.", __func__);
                return id::invalid();
            }
        }
    }

    if (bind_flags & D3D11_BIND_RENDER_TARGET) {
        for (uint32_t i = 0; i < array_size; ++i) {
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format = format;

            if (array_size > 1) {
                rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv_desc.Texture2DArray.MipSlice = 0;
                rtv_desc.Texture2DArray.ArraySize = 1;
                rtv_desc.Texture2DArray.FirstArraySlice = i;
            } else {
                rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                rtv_desc.Texture2D.MipSlice = 0;
            }

            // Create the RTV
            hr = renderer->pDevice->CreateRenderTargetView((ID3D11Resource *)t->texture.Get(), &rtv_desc, t->rtv[i].GetAddressOf());
            if (FAILED(hr)) {
                LOG("texture::load: Failed to create Render Target View for texture.");
                return id::invalid();
            }
        }
    }

    if (bind_flags & D3D11_BIND_DEPTH_STENCIL) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = format;
        dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

        // Create the DSV
        hr = renderer->pDevice->CreateDepthStencilView((ID3D11Resource *)t->texture.Get(), &dsv_desc, t->dsv.GetAddressOf());
        if (FAILED(hr)) {
            LOG("texture::load: Failed to create Depth Stencil View for texture.");
            return id::invalid();
        }
    }

    return t->id;
}

Texture *texture::get(TextureId id) {
    // TODO: Which would require me to create a texture system...
    (void)id;
    return nullptr;
}
