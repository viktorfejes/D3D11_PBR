#pragma once

#include "id.hpp"
#include <DirectXMath.h>

using MaterialId = Id;

enum MRAO_Bits {
    METALLIC_BIT = 1 << 0,
    ROUGHNESS_BIT = 1 << 1,
    AMBIENT_OCCLUSION_BIT = 1 << 2,
};

struct Material {
    MaterialId id;

    DirectX::XMFLOAT3 albedo;
    Id albedo_map;
    float metallic;
    // TODO: Merge metallic, roughness, and ao into a single map
    // uint8_t mrao_flags;
    // Id mrao_map;
    Id metallic_map;
    float rougness;
    Id roughness_map;
    Id normal_map;

    // Controlling emission by potentially having an 8-bit emission_map texture
    // the emission_color can take it into HDR territory by going over 1.0 on
    // any component, serving as a tint and multiplier
    DirectX::XMFLOAT3 emission_color;
    Id emission_map;
};

namespace material {
MaterialId create(DirectX::XMFLOAT3 albedo, Id albedo_map, float metallic, Id metallic_map, float roughness, Id roughness_map, Id normal_map, DirectX::XMFLOAT3 emission_color, Id emission_map);
void bind(Material *material);
} // namespace material
