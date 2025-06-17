#include "light.hpp"

#include "application.hpp"
#include "logger.hpp"
#include "renderer.hpp"

LightId light::create(LightType type, DirectX::XMFLOAT3 color, float intensity) {
    Renderer *state = application::get_renderer();
    
    Light *l = nullptr;
    for (uint8_t i = 0; i < MAX_LIGHTS; ++i) {
        if (id::is_invalid(state->lights[i].id)) {
            l = &state->lights[i];
            l->id.id = i;
            break;
        }
    }

    if (l == nullptr) {
        LOG("%s: Max meshes reached, adjust max mesh count.", __func__);
        id::invalidate(&l->id);
        return id::invalid();
    }

    l->type = type;
    l->color = color;
    l->intensity = intensity;

    return l->id;
}

Light *light::get(Renderer *renderer, LightId id) {
    if (id::is_invalid(id)) {
        return nullptr;
    }

    Light *l = &renderer->lights[id.id];
    assert(l && "Light should be present in system's storage");

    return l;
}
