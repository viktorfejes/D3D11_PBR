#include "texture.hpp"

#include "application.hpp"
#include "id.hpp"
#include "logger.hpp"
#include "renderer.hpp"

#include <comdef.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

static bool create_texture_internal(ID3D11Device *device, Texture *texture, uint32_t width, uint32_t height, uint32_t mip_levels, uint32_t array_size, DXGI_FORMAT format, uint32_t bind_flags, bool is_cubemap, bool generate_srv, const void *data, uint32_t row_pitch);

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

    if (!create_texture_internal(renderer->pDevice.Get(), t,
                                 width, height,
                                 mip_levels, array_size,
                                 format,
                                 bind_flags,
                                 is_cubemap, generate_srv,
                                 initial_data, row_pitch)) {
        id::invalidate(&t->id);
        return id::invalid();
    }

    // Set the fields now that we know it's a valid texture
    t->width = width;
    t->height = height;
    t->format = format;
    t->mip_levels = mip_levels;
    t->array_size = array_size;
    t->is_cubemap = is_cubemap;
    t->bind_flags = bind_flags;
    t->has_srv = generate_srv;

    return t->id;
}

// TODO: For now, resize only concerns the size of the texture, nothing else...
bool texture::resize(TextureId id, uint16_t width, uint16_t height) {
    Renderer *renderer = application::get_renderer();

    Texture *t = get(renderer, id);
    if (!t) {
        return false;
    }

    // Recreate the texture. Since I'm using ComPtr's I don't need to release
    // it before recreating it -- the smart pointer will take care of it
    if (!create_texture_internal(renderer->pDevice.Get(), t,
                                 width, height,
                                 t->mip_levels, t->array_size,
                                 t->format,
                                 t->bind_flags,
                                 t->is_cubemap, t->has_srv,
                                 NULL, 0)) {
        return false;
    }

    // Update the width and height
    t->width = width;
    t->height = height;

    return true;
}

Texture *texture::get(Renderer *renderer, TextureId id) {
    if (id::is_invalid(id)) {
        return nullptr;
    }

    Texture *t = &renderer->textures[id.id];
    assert(t && "Texture should be present in renderer's storage");

    return t;
}

static bool create_texture_internal(ID3D11Device *device, Texture *texture, uint32_t width, uint32_t height, uint32_t mip_levels, uint32_t array_size, DXGI_FORMAT format, uint32_t bind_flags, bool is_cubemap, bool generate_srv, const void *data, uint32_t row_pitch) {
    // Upload to GPU straight away
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
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
    if (data && row_pitch > 0) {
        init_data.pSysMem = data;
        init_data.SysMemPitch = row_pitch;
        data_ptr = &init_data;
    }

    // Create the texture
    HRESULT hr = device->CreateTexture2D(&desc, data_ptr, texture->texture.GetAddressOf());
    if (FAILED(hr)) {
        _com_error err(hr);
        LPCTSTR err_msg = err.ErrorMessage();
        LOG("%s: Failed to create Texture2D. HRESULT: 0x%lX. Message: %s", __func__, hr, err_msg);
        return false;
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
        hr = device->CreateShaderResourceView((ID3D11Resource *)texture->texture.Get(), &srv_desc, texture->srv.GetAddressOf());
        if (FAILED(hr)) {
            LOG("texture::load: Failed to create Shader Resource View for texture.");
            return false;
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
            hr = device->CreateUnorderedAccessView((ID3D11Resource *)texture->texture.Get(), &uav_desc, texture->uav[mip].GetAddressOf());
            if (FAILED(hr)) {
                LOG("%s: Failed to create Unordered Access View for texture.", __func__);
                return false;
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
            hr = device->CreateRenderTargetView((ID3D11Resource *)texture->texture.Get(), &rtv_desc, texture->rtv[i].GetAddressOf());
            if (FAILED(hr)) {
                LOG("texture::load: Failed to create Render Target View for texture.");
                return false;
            }
        }
    }

    if (bind_flags & D3D11_BIND_DEPTH_STENCIL) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = format;
        dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

        // Create the DSV
        hr = device->CreateDepthStencilView((ID3D11Resource *)texture->texture.Get(), &dsv_desc, texture->dsv.GetAddressOf());
        if (FAILED(hr)) {
            LOG("texture::load: Failed to create Depth Stencil View for texture.");
            return false;
        }
    }

    return true;
}

bool texture::export_to_file(TextureId texture, const char *filename) {
    Renderer *renderer = application::get_renderer();
    ID3D11Device *device = renderer->pDevice.Get();
    ID3D11DeviceContext *context = renderer->pContext.Get();

    // Fetch the texture
    Texture *tex = get(renderer, texture);
    if (!tex) {
        return false;
    }

    // Create staging texture
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture;

    D3D11_TEXTURE2D_DESC staging_desc = {};
    tex->texture->GetDesc(&staging_desc);

    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.MiscFlags = 0;

    HRESULT hr = device->CreateTexture2D(&staging_desc, nullptr, staging_texture.GetAddressOf());
    if (FAILED(hr)) {
        LOG("%s: Couldn't create texture for staging buffer", __func__);
        return false;
    }

    const int width = (int)staging_desc.Width;
    const int height = (int)staging_desc.Height;
    const int comp = 3;

    float *rgb_data = (float *)malloc(sizeof(float) * width * height * comp);
    if (!rgb_data) {
        LOG("%s: Couldn't allocate data structure for texture", __func__);
        return false;
    }

    if (tex->is_cubemap) {
        static const char *face_names[6] = {"px", "nx", "py", "ny", "pz", "nz"};

        for (UINT face = 0; face < 6; ++face) {
            // Copy to staging
            UINT subresource = D3D11CalcSubresource(0, face, staging_desc.MipLevels);
            context->CopySubresourceRegion(staging_texture.Get(), 0, 0, 0, 0, tex->texture.Get(), subresource, NULL);

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            hr = context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                free(rgb_data);
                return false;
            }

            float *dst = rgb_data;
            for (int y = 0; y < height; ++y) {
                const float *src = (const float *)((uint8_t *)mapped.pData + y * mapped.RowPitch);
                for (int x = 0; x < width; ++x) {
                    dst[0] = src[x * 4 + 0];
                    dst[1] = src[x * 4 + 1];
                    dst[2] = src[x * 4 + 2];
                    dst += 3;
                }
            }

            context->Unmap(staging_texture.Get(), 0);

            char full_name[512];
            snprintf(full_name, sizeof(full_name), "%s_%s.hdr", filename, face_names[face]);
            stbi_write_hdr(full_name, width, height, comp, rgb_data);
        }
    } else {
        context->CopyResource(staging_texture.Get(), tex->texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = context->Map(staging_texture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) {
            LOG("%s: Failed to map staging texture", __func__);
            return false;
        }

        float *dst = rgb_data;
        for (int y = 0; y < height; ++y) {
            const float *src = (const float *)((uint8_t *)mapped.pData + y * mapped.RowPitch);
            for (int x = 0; x < width; ++x) {
                dst[0] = src[x * 4 + 0];
                dst[1] = src[x * 4 + 1];
                dst[2] = src[x * 4 + 2];
                dst += 3;
            }
        }

        context->Unmap(staging_texture.Get(), 0);

        char full_name[512];
        snprintf(full_name, sizeof(full_name), "%s.hdr", filename);
        stbi_write_hdr(full_name, width, height, comp, rgb_data);
    }

    free(rgb_data);

    return true;
}
