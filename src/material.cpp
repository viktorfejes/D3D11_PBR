#include "material.hpp"

#include "application.hpp"
#include "id.hpp"
#include "logger.hpp"
#include "renderer.hpp"

MaterialId material::create(DirectX::XMFLOAT3 albedo, Id albedo_map, float metallic, Id metallic_map, float roughness, Id roughness_map, Id normal_map, DirectX::XMFLOAT3 emission_color, Id emission_map) {
    // Materials are kept in the renderer so fetching that here
    Renderer *renderer = application::get_renderer();

    // Check if we can find an empty slot for our mesh
    // by linear search (which for this size is probably the best)
    Material *mat = nullptr;
    Material *materials = renderer->materials;
    for (uint8_t i = 0; i < MAX_MESHES; ++i) {
        if (id::is_invalid(materials[i].id)) {
            mat = &materials[i];
            mat->id.id = i;
            break;
        }
    }

    if (mat == nullptr) {
        LOG("material::create: Max materials reached, adjust max material count.");
        return id::invalid();
    }

    // Set up the material
    mat->albedo = id::is_invalid(albedo_map) ? albedo : DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
    mat->albedo_map = albedo_map;
    mat->metallic = id::is_invalid(metallic_map) ? metallic : 1.0f;
    mat->metallic_map = metallic_map;
    mat->rougness = id::is_invalid(roughness_map) ? roughness : 1.0f;
    mat->roughness_map = roughness_map;
    mat->normal_map = normal_map;
    mat->emission_color = emission_color;
    mat->emission_map = emission_map;

    return mat->id;
}

void material::bind(Material *material) {
    (void)material;
}
