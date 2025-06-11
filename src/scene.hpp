#pragma once

#include "camera.hpp"
#include "id.hpp"

#include <DirectXMath.h>

#define MAX_SCENE_LIGHTS 8
#define MAX_SCENE_MESHES 6
#define MAX_SCENE_CAMERAS 4

struct Renderer;

using SceneId = Id;
using InstanceId = Id;

struct SceneMesh {
    SceneId id;
    Id mesh_id;
    Id material_id;

    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    DirectX::XMFLOAT3 scale;

    DirectX::XMFLOAT4X4 world_matrix;
    DirectX::XMFLOAT4X4 world_inv_transpose;
    bool is_dirty;
};

struct SceneCamera {
    SceneId id;
    Camera base;

    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 target;
    DirectX::XMFLOAT3 up;

    float distance;
    float yaw;
    float pitch;

    DirectX::XMFLOAT4X4 view_matrix;
    DirectX::XMFLOAT4X4 projection_matrix;
    DirectX::XMFLOAT4X4 view_projection_matrix;
    bool is_view_dirty;
    bool is_projection_dirty;
};

struct LightInstance {
    InstanceId id;
    Id light_id;

    DirectX::XMFLOAT4X4 view_projection_matrix;
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 rotation;
    uint32_t shadowmap_index;
    bool enabled;
};

struct Scene {
    Id id;

    LightInstance lights[MAX_SCENE_LIGHTS];
    SceneMesh meshes[MAX_SCENE_MESHES];
    SceneCamera cameras[MAX_SCENE_CAMERAS];
    SceneCamera *active_cam;
};

// TODO: SceneId should refer to the scene's id, and what I use as SceneId now should
// be specified as xxInstanceId, like MeshInstanceId

namespace scene {

bool initialize(Scene *out_scene);
SceneId add_mesh(Scene *scene, Id mesh_id, Id material_id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 rotation, DirectX::XMFLOAT3 scale);
SceneId add_camera(Scene *scene, float fov, float znear, float zfar, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target);
InstanceId add_light(Scene *scene, Id light_id);

void bind_mesh_instance(Renderer *renderer, Scene *scene, SceneId mesh_instance_id, uint8_t start_slot);
DirectX::XMFLOAT3 mesh_get_rotation(Scene *scene, SceneId scene_mesh_id);
DirectX::XMFLOAT4X4 mesh_get_world_matrix(Scene *scene, SceneId scene_mesh_id);
DirectX::XMFLOAT4X4 mesh_get_world_inv_transpose_matrix(Scene *scene, SceneId scene_mesh_id);

DirectX::XMFLOAT4X4 camera_get_view_projection_matrix(SceneCamera *camera);
DirectX::XMFLOAT4X4 camera_get_view_matrix(SceneCamera *camera);
DirectX::XMFLOAT4X4 camera_get_projection_matrix(SceneCamera *camera);
float camera_get_yaw(Scene *scene, Id scene_cam_id);
float camera_get_pitch(Scene *scene, Id scene_cam_id);
float camera_get_distance(Scene *scene, Id scene_cam_id);
DirectX::XMFLOAT3 camera_get_position(Scene *scene, Id scene_cam_id);
DirectX::XMFLOAT3 camera_get_target(Scene *scene, Id scene_cam_id);

void mesh_set_position(Scene *scene, Id scene_mesh_id, DirectX::XMFLOAT3 position);
void mesh_set_rotation(Scene *scene, Id scene_mesh_id, DirectX::XMFLOAT3 rotation);
void mesh_set_scale(Scene *scene, Id scene_mesh_id, DirectX::XMFLOAT3 scale);

void camera_set_active(Scene *scene, Id scene_cam_id);
void camera_set_active_aspect_ratio(Scene *scene, float aspect_ratio);
void camera_set_position(Scene *scene, Id scene_cam_id, DirectX::XMFLOAT3 position);
void camera_set_target(Scene *scene, Id scene_cam_id, DirectX::XMFLOAT3 target);
void camera_set_up(Scene *scene, Id scene_cam_id, DirectX::XMFLOAT3 up);
void camera_set_yaw_pitch(Scene *scene, SceneId scene_cam_id, float yaw, float pitch);
void camera_set_distance(Scene *scene, SceneId scene_cam_id, float distance);
void camera_pan(Scene *scene, SceneId scene_cam_id, float dx, float dy);

} // namespace scene
