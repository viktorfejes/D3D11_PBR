#pragma once

#include "id.hpp"
#include <DirectXMath.h>

struct Renderer;

using MaterialId = Id;

enum MRAO_Bits {
    METALLIC_BIT = 1 << 0,
    ROUGHNESS_BIT = 1 << 1,
    AMBIENT_OCCLUSION_BIT = 1 << 2,
};

struct Material {
    MaterialId id;

    DirectX::XMFLOAT3 albedo_color;
    float emission_intensity;
    float metallic_value;
    float roughness_value;
    float coat_value;

    Id albedo_texture;
    Id metallic_texture;
    Id roughness_texture;
    Id normal_texture;
    Id coat_texture;

    // Controlling emission by potentially having an 8-bit emission_texture texture
    // the emission_color can take it into HDR territory by going over 1.0 on
    // any component, serving as a tint and multiplier
    Id emission_texture;
};

namespace material {

MaterialId create(DirectX::XMFLOAT3 albedo_color, Id albedo_texture, float metallic_value, Id metallic_texture, float roughness_value, Id roughness_texture, float coat_value, Id coat_texture, Id normal_texture, float emission_intensity, Id emission_texture);
Material *get(Renderer *renderer, MaterialId material_id);
void bind(Renderer *renderer, Material *material, uint8_t start_cb, uint8_t start_tex);

} // namespace material
