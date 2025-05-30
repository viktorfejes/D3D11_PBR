#pragma once

#include "id.hpp"

#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

using TextureId = Id;
#define MAX_MIP_LEVELS 16

struct Texture {
    TextureId id;
    int16_t width;
    int16_t height;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv[6];
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav[MAX_MIP_LEVELS];
};

namespace texture {

TextureId load(const char *filename, bool is_srgb);
TextureId load_hdr(const char *filename);
TextureId load_from_data(uint8_t *image_data, uint16_t width, uint16_t height);
TextureId create(uint16_t width,
                 uint16_t height,
                 DXGI_FORMAT format,
                 uint32_t bind_flags,
                 bool generate_srv,
                 const void *initial_data,
                 uint32_t row_pitch,
                 uint32_t array_size,
                 uint32_t mip_levels,
                 bool is_cubemap);
Texture *get(TextureId id);

} // namespace texture
