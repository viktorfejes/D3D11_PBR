#pragma once

#include "id.hpp"

#include <DirectXMath.h>

struct Renderer;

enum LightType {
    LIGHT_TYPE_DIRECTIONAL,
};

using LightId = Id;

struct Light {
    LightId id;

    LightType type;
    DirectX::XMFLOAT3 color;
    float intensity;
};

namespace light {
    LightId create(LightType type, DirectX::XMFLOAT3 color, float intensity);
    Light *get(Renderer *renderer, LightId id);
};
