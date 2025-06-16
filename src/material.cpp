#include "material.hpp"

#include "application.hpp"
#include "id.hpp"
#include "logger.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "texture.hpp"
#include <cassert>

MaterialId material::create(DirectX::XMFLOAT3 albedo_color, Id albedo_texture, float metallic_value, Id metallic_texture, float roughness_value, Id roughness_texture, Id normal_texture, float emission_intensity, Id emission_texture) {
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

    // Material values
    mat->albedo_color = albedo_color;
    mat->metallic_value = metallic_value;
    mat->roughness_value = roughness_value;
    mat->emission_intensity = emission_intensity;

    // Material textures
    mat->albedo_texture = albedo_texture;
    mat->metallic_texture = metallic_texture;
    mat->roughness_texture = roughness_texture;
    mat->normal_texture = normal_texture;
    mat->emission_texture = emission_texture;

    return mat->id;
}

Material *material::get(Renderer *renderer, MaterialId material_id) {
    if (id::is_valid(material_id)) {
        Material *m = &renderer->materials[material_id.id];
        if (id::is_fresh(m->id, material_id)) {
            return m;
        }
    }

    return nullptr;
}

void material::bind(Renderer *renderer, Material *material, uint8_t start_cb, uint8_t start_tex) {
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = renderer->context->Map(renderer->pCBPerMaterial.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        LOG("%s: Failed to map per material constant buffer", __func__);
        return;
    }

    CBPerMaterial *cb_ptr = (CBPerMaterial *)mappedResource.pData;
    cb_ptr->albedo_color = material->albedo_color;
    cb_ptr->emission_intensity = material->emission_intensity;
    cb_ptr->metallic_value = material->metallic_value;
    cb_ptr->roughness_value = material->roughness_value;

    renderer->context->Unmap(renderer->pCBPerMaterial.Get(), 0);
    renderer->context->PSSetConstantBuffers((UINT)start_cb, 1, renderer->pCBPerMaterial.GetAddressOf());

    // Look up the texture based on the id
    Texture *albedo_tex = texture::get(renderer, material->albedo_texture);
    Texture *metallic_tex = texture::get(renderer, material->metallic_texture);
    Texture *roughness_tex = texture::get(renderer, material->roughness_texture);
    Texture *normal_tex = texture::get(renderer, material->normal_texture);
    Texture *emission_tex = texture::get(renderer, material->emission_texture);

    ID3D11ShaderResourceView *srvs[] = {
        albedo_tex->srv.Get(),
        metallic_tex->srv.Get(),
        roughness_tex->srv.Get(),
        normal_tex->srv.Get(),
        emission_tex->srv.Get(),
    };
    renderer->context->PSSetShaderResources((UINT)start_tex, ARRAYSIZE(srvs), srvs);
}
