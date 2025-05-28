#pragma once

#include <DirectXMath.h>

struct Camera {
    float fov = 45;
    float znear = 0.1f;
    float zfar = 10.0f;
    float aspect_ratio = 16.0f / 9.0f;
};
