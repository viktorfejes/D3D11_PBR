#pragma once

#include "id.hpp"
#include <DirectXMath.h>

enum LightType {
    LIGHT_TYPE_DIRECTIONAL,
};

struct Lights {
    Id id;
    LightType type;
    DirectX::XMFLOAT3 color;
    float intensity;
    bool casts_shadows;
};
