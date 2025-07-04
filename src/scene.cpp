#include "scene.hpp"

#include "application.hpp"
#include "light.hpp"
#include "logger.hpp"
#include "renderer.hpp"

#include <DirectXMath.h>
#include <cassert>

bool scene::initialize(Scene *out_scene) {
    assert(out_scene && "scene::initialize: out_scene CANNOT be NULL");

    out_scene->active_cam = nullptr;
    for (int i = 0; i < MAX_SCENE_CAMERAS; ++i) {
        out_scene->cameras[i].id = id::invalid();
    }

    for (int i = 0; i < MAX_SCENE_MESHES; ++i) {
        out_scene->meshes[i].id = id::invalid();
    }

    for (int i = 0; i < MAX_SCENE_LIGHTS; ++i) {
        out_scene->lights[i].id = id::invalid();
    }

    return true;
}

Id scene::add_mesh(Scene *scene, Id mesh_id, Id material_id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 rotation, DirectX::XMFLOAT3 scale) {
    assert(scene && "scene::add_mesh: scene pointer cannot be NULL");

    // Find an empty slot in scene with linear search
    SceneMesh *sm = nullptr;
    for (int i = 0; i < MAX_SCENE_MESHES; ++i) {
        if (id::is_invalid(scene->meshes[i].id)) {
            sm = &scene->meshes[i];
            sm->id.id = i;
            break;
        }
    }

    // If we still don't have a SceneMesh pointer, then we are full
    if (!sm) {
        LOG("scene::add_mesh: No more empty slots found");
        return id::invalid();
    }

    // Assign the mesh
    sm->mesh_id = mesh_id;
    sm->material_id = material_id;
    sm->position = position;
    sm->rotation = rotation;
    sm->scale = scale;
    sm->is_dirty = true;

    // Compute the world matrix so we have it in cache based on defaults
    mesh_get_world_matrix(scene, mesh_id);

    // This returns a SceneMesh ID!
    return sm->id;
}

SceneId scene::add_camera(Scene *scene, float fov, float znear, float zfar, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target) {
    assert(scene && "scene::add_camera: scene pointer cannot be NULL");

    // Camera's are not instanced so we just create a new camera based
    // on the passed in parameters

    // Find an empty slot in scene with linear search
    SceneCamera *cam = nullptr;
    for (int i = 0; i < MAX_SCENE_CAMERAS; ++i) {
        if (id::is_invalid(scene->cameras[i].id)) {
            cam = &scene->cameras[i];
            cam->id.id = i;
            break;
        }
    }

    // If we still don't have a SceneCamera pointer, then we are full
    if (!cam) {
        LOG("scene::add_camera: No more empty slots found");
        return id::invalid();
    }

    // Check if active camera is null and if so, set this as the
    // currently active camera in scene.
    if (!scene->active_cam) {
        scene->active_cam = cam;
    }

    // Assign the appropriate values with sensible defaults?
    cam->base.fov = fov;
    cam->base.znear = znear;
    cam->base.zfar = zfar;
    // TODO: Badly hardcoded value?
    cam->base.aspect_ratio = 16 / 9.0f;

    cam->position = position;
    cam->target = target;
    cam->up = {0.0f, 1.0f, 0.0f};

    // Calculate yaw, pitch, and distance
    // TODO: Separate reusable function
    DirectX::XMVECTOR pos_vec = DirectX::XMLoadFloat3(&cam->position);
    DirectX::XMVECTOR tar_vec = DirectX::XMLoadFloat3(&cam->target);
    DirectX::XMVECTOR offset = DirectX::XMVectorSubtract(pos_vec, tar_vec);

    // Store the distance
    cam->distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(offset));

    // Direction vector
    DirectX::XMVECTOR direction = DirectX::XMVector3Normalize(offset);
    cam->yaw = atan2f(DirectX::XMVectorGetX(direction), DirectX::XMVectorGetZ(direction));
    cam->pitch = atan2f(DirectX::XMVectorGetY(direction), sqrtf(DirectX::XMVectorGetX(direction) * DirectX::XMVectorGetX(direction) + DirectX::XMVectorGetZ(direction) * DirectX::XMVectorGetZ(direction)));

    // Set matrices to dirty explicitly
    cam->is_view_dirty = true;
    cam->is_projection_dirty = true;
    cam->is_view_projection_dirty = true;

    return cam->id;
}

InstanceId scene::add_light(Scene *scene, Id light_id, DirectX::XMFLOAT3 position, DirectX::XMFLOAT3 target, bool cast_shadows) {
    assert(scene && "scene::add_light: scene pointer cannot be NULL");

    // Find an empty slot in scene with linear search
    LightInstance *light = nullptr;
    for (int i = 0; i < MAX_SCENE_LIGHTS; ++i) {
        if (id::is_invalid(scene->lights[i].id)) {
            light = &scene->lights[i];
            light->id.id = i;
            break;
        }
    }

    // If we still don't have a SceneMesh pointer, then we are full
    if (!light) {
        LOG("scene::add_light: No more empty slots found");
        return id::invalid();
    }

    // Set up the instance
    light->light_id = light_id;
    light->enabled = true;
    light->position = position;
    light->target = target;
    light->shadowmap_index = 1;
    light->cast_shadows = cast_shadows;

    light->is_view_dirty = true;
    light->is_projection_dirty = true;
    light->is_view_projection_dirty = true;

    return light->id;
}

void scene::bind_mesh_instance(Renderer *renderer, Scene *scene, Id mesh_instance_id, uint8_t start_slot) {
    D3D11_MAPPED_SUBRESOURCE map;

    // Update per object constant buffer
    HRESULT hr = renderer->context->Map(renderer->pCBPerObject.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    if (FAILED(hr)) {
        LOG("%s: Failed to map per object constant buffer", __func__);
        return;
    }

    // Update the per object buffer on gpu
    CBPerObject *perObjectPtr = (CBPerObject *)map.pData;
    perObjectPtr->worldMatrix = scene::mesh_get_world_matrix(scene, mesh_instance_id);
    perObjectPtr->worldInvTrans = scene::mesh_get_world_inv_transpose_matrix(scene, mesh_instance_id);

    renderer->context->Unmap(renderer->pCBPerObject.Get(), 0);
    renderer->context->VSSetConstantBuffers((UINT)start_slot, 1, renderer->pCBPerObject.GetAddressOf());
}

DirectX::XMFLOAT3 scene::mesh_get_rotation(Scene *scene, SceneId scene_mesh_id) {
    assert(scene && "scene::mesh_get_rotation: Scene pointer cannot be NULL");

    // Fetch the right mesh based on whether it is stale or not
    SceneMesh *sm = &scene->meshes[scene_mesh_id.id];
    if (id::is_fresh(sm->id, scene_mesh_id)) {
        return sm->rotation;
    }

    return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
}

DirectX::XMFLOAT4X4 scene::mesh_get_world_matrix(Scene *scene, SceneId scene_mesh_id) {
    assert(scene && "scene::mesh_get_world_matrix: Scene pointer cannot be NULL");

    // Fetch the right mesh based on whether it is stale or not
    SceneMesh *sm = &scene->meshes[scene_mesh_id.id];
    if (!sm || id::is_stale(sm->id, scene_mesh_id)) {
        return DirectX::XMFLOAT4X4{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1};
    }

    if (sm->is_dirty) {
        DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(sm->rotation.x),
            DirectX::XMConvertToRadians(sm->rotation.y),
            DirectX::XMConvertToRadians(sm->rotation.z));

        DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(quat);
        DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(sm->position.x, sm->position.y, sm->position.z);
        DirectX::XMMATRIX S = DirectX::XMMatrixScaling(sm->scale.x, sm->scale.y, sm->scale.z);

        DirectX::XMMATRIX world_matrix = S * R * T;

        // Calculate inverse transpose world matrix as well here
        DirectX::XMMATRIX no_translate_matrix = world_matrix;
        no_translate_matrix.r[3] = DirectX::XMVectorSet(0, 0, 0, 1);
        DirectX::XMMATRIX inv_transpose_3x3 = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, no_translate_matrix));

        DirectX::XMStoreFloat4x4(&sm->world_matrix, world_matrix);
        DirectX::XMStoreFloat4x4(&sm->world_inv_transpose, inv_transpose_3x3);

        sm->is_dirty = false;
    }

    return sm->world_matrix;
}

DirectX::XMFLOAT4X4 scene::mesh_get_world_inv_transpose_matrix(Scene *scene, SceneId scene_mesh_id) {
    assert(scene && "scene::mesh_get_world_inv_transpose_matrix: Scene pointer cannot be NULL");

    // Fetch the right mesh based on whether it is stale or not
    SceneMesh *sm = &scene->meshes[scene_mesh_id.id];
    if (!sm || id::is_stale(sm->id, scene_mesh_id)) {
        return DirectX::XMFLOAT4X4{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1};
    }

    if (sm->is_dirty) {
        DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(sm->rotation.x),
            DirectX::XMConvertToRadians(sm->rotation.y),
            DirectX::XMConvertToRadians(sm->rotation.z));

        DirectX::XMMATRIX R = DirectX::XMMatrixRotationQuaternion(quat);
        DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(sm->position.x, sm->position.y, sm->position.z);
        DirectX::XMMATRIX S = DirectX::XMMatrixScaling(sm->scale.x, sm->scale.y, sm->scale.z);

        DirectX::XMMATRIX world_matrix = S * R * T;

        // Calculate inverse transpose world matrix as well here
        DirectX::XMMATRIX no_translate_matrix = world_matrix;
        no_translate_matrix.r[3] = DirectX::XMVectorSet(0, 0, 0, 1);
        DirectX::XMMATRIX inv_transpose_3x3 = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, no_translate_matrix));

        DirectX::XMStoreFloat4x4(&sm->world_matrix, world_matrix);
        DirectX::XMStoreFloat4x4(&sm->world_inv_transpose, inv_transpose_3x3);

        sm->is_dirty = false;
    }

    return sm->world_inv_transpose;
}

DirectX::XMFLOAT4X4 scene::camera_get_view_projection_matrix(SceneCamera *camera) {
    assert(camera && "scene::camera_get_view_projection_matrix: Camera pointer cannot be NULL");

    // TODO: First part could be refactored into a separate camera_update_matrices(SceneCamera *camera);
    // function, so it is separate from the getter.
    if (camera->is_view_dirty) {
        DirectX::XMMATRIX view_matrix;
        view_matrix = DirectX::XMMatrixLookAtLH(DirectX::XMLoadFloat3(&camera->position), DirectX::XMLoadFloat3(&camera->target), DirectX::XMLoadFloat3(&camera->up));
        DirectX::XMStoreFloat4x4(&camera->view_matrix, view_matrix);

        camera->is_view_dirty = false;
        camera->is_view_projection_dirty = true;
    }

    if (camera->is_projection_dirty) {
        DirectX::XMMATRIX projection_matrix = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(camera->base.fov),
            camera->base.aspect_ratio,
            camera->base.znear, camera->base.zfar);
        DirectX::XMStoreFloat4x4(&camera->projection_matrix, projection_matrix);

        camera->is_projection_dirty = false;
        camera->is_view_projection_dirty = true;
    }

    if (camera->is_view_projection_dirty) {
        DirectX::XMMATRIX vp_matrix = DirectX::XMLoadFloat4x4(&camera->view_matrix) * DirectX::XMLoadFloat4x4(&camera->projection_matrix);
        DirectX::XMStoreFloat4x4(&camera->view_projection_matrix, vp_matrix);
        camera->is_view_projection_dirty = false;
    }

    return camera->view_projection_matrix;
}

DirectX::XMFLOAT4X4 scene::camera_get_view_matrix(SceneCamera *camera) {
    assert(camera && "scene::camera_get_view_projection_matrix: Camera pointer cannot be NULL");

    if (camera->is_view_dirty) {
        DirectX::XMMATRIX view_matrix = DirectX::XMMatrixLookAtLH(DirectX::XMLoadFloat3(&camera->position), DirectX::XMLoadFloat3(&camera->target), DirectX::XMLoadFloat3(&camera->up));
        DirectX::XMStoreFloat4x4(&camera->view_matrix, view_matrix);

        camera->is_view_dirty = false;
        camera->is_view_projection_dirty = true;
    }

    return camera->view_matrix;
}

DirectX::XMFLOAT4X4 scene::camera_get_projection_matrix(SceneCamera *camera) {
    assert(camera && "scene::camera_get_view_projection_matrix: Camera pointer cannot be NULL");

    if (camera->is_projection_dirty) {
        DirectX::XMMATRIX projection_matrix = DirectX::XMMatrixPerspectiveFovLH(
            DirectX::XMConvertToRadians(camera->base.fov),
            camera->base.aspect_ratio,
            camera->base.znear, camera->base.zfar);
        DirectX::XMStoreFloat4x4(&camera->projection_matrix, projection_matrix);

        camera->is_projection_dirty = false;
        camera->is_view_projection_dirty = true;
    }

    return camera->projection_matrix;
}

float scene::camera_get_yaw(Scene *scene, Id scene_cam_id) {
    assert(scene && "scene::camera_get_yaw: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_get_yaw: Incorrect Scene Camera Id");

    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        return cam->yaw;
    }
    return 0.0f;
}

float scene::camera_get_pitch(Scene *scene, Id scene_cam_id) {
    assert(scene && "scene::camera_get_pitch: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_get_pitch: Incorrect Scene Camera Id");

    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        return cam->pitch;
    }
    return 0.0f;
}

float scene::camera_get_distance(Scene *scene, Id scene_cam_id) {
    assert(scene && "scene::camera_get_distance: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_get_distance: Incorrect Scene Camera Id");

    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        return cam->distance;
    }
    return 0.0f;
}

DirectX::XMFLOAT3 scene::light_get_direction(LightInstance *light) {
    assert(light && "scene::light_get_direction: Light instance pointer cannot be NULL");
    
    DirectX::XMVECTOR direction = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&light->target), DirectX::XMLoadFloat3(&light->position)));
    DirectX::XMFLOAT3 out_dir;
    DirectX::XMStoreFloat3(&out_dir, direction);

    return out_dir;
}

DirectX::XMFLOAT4X4 scene::light_get_view_matrix(Scene *scene, Id light_id) {
    assert(scene && "scene::light_get_view_matrix: Scene pointer cannot be NULL");
    assert(light_id.id < MAX_SCENE_LIGHTS && "scene::light_get_view_matrix: Incorrect Scene Light Id");

    LightInstance *light_instance = &scene->lights[light_id.id];
    if (!light_instance || id::is_stale(light_instance->id, light_id)) {
        return DirectX::XMFLOAT4X4{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1};
    }

    if (light_instance->is_view_dirty) {
        DirectX::XMMATRIX view_matrix = DirectX::XMMatrixLookAtLH(
            DirectX::XMLoadFloat3(&light_instance->position),
            DirectX::XMLoadFloat3(&light_instance->target),
            DirectX::XMVectorSet(0, 1, 0, 0));
        DirectX::XMStoreFloat4x4(&light_instance->view_matrix, view_matrix);

        light_instance->is_view_dirty = false;
        light_instance->is_view_projection_dirty = true;
    }

    return light_instance->view_projection_matrix;
}

// TODO: One fetch, multiple use paradigm
DirectX::XMFLOAT4X4 scene::light_get_projection_matrix(Scene *scene, Id light_id) {
    assert(scene && "scene::light_get_projection_matrix: Scene pointer cannot be NULL");
    assert(light_id.id < MAX_SCENE_LIGHTS && "scene::light_get_projection_matrix: Incorrect Scene Light Id");

    LightInstance *light_instance = &scene->lights[light_id.id];
    if (!light_instance || id::is_stale(light_instance->id, light_id)) {
        return DirectX::XMFLOAT4X4{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1};
    }

    if (light_instance->is_projection_dirty) {
        Renderer *renderer = application::get_renderer();

        // Fetch the light
        Light *light = light::get(renderer, light_id);
        if (!light) {
            return DirectX::XMFLOAT4X4{
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1};
        }

        // Calculate light's view projection matrix based on type
        DirectX::XMMATRIX projection_matrix;
        switch (light->type) {
            case LIGHT_TYPE_DIRECTIONAL:
                projection_matrix = DirectX::XMMatrixOrthographicLH(
                    500.0f, 500.0f,
                    0.1f, 200.0f);
                break;
        }

        DirectX::XMStoreFloat4x4(&light_instance->projection_matrix, projection_matrix);

        light_instance->is_projection_dirty = false;
        light_instance->is_view_projection_dirty = true;
    }

    return light_instance->projection_matrix;
}

DirectX::XMFLOAT4X4 scene::light_get_view_projection_matrix(Scene *scene, Id light_id) {
    assert(scene && "scene::light_get_view_projection_matrix: Scene pointer cannot be NULL");
    assert(light_id.id < MAX_SCENE_LIGHTS && "scene::light_get_view_projection_matrix: Incorrect Scene Light Id");

    LightInstance *light_instance = &scene->lights[light_id.id];
    if (!light_instance || id::is_stale(light_instance->id, light_id)) {
        return DirectX::XMFLOAT4X4{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1};
    }

    // If the light's view projection matrix needs to be recalculated
    if (light_instance->is_view_dirty) {
        DirectX::XMMATRIX view_matrix = DirectX::XMMatrixLookAtLH(
            DirectX::XMLoadFloat3(&light_instance->position),
            DirectX::XMLoadFloat3(&light_instance->target),
            DirectX::XMVectorSet(0, 1, 0, 0));
        DirectX::XMStoreFloat4x4(&light_instance->view_matrix, view_matrix);

        light_instance->is_view_dirty = false;
        light_instance->is_view_projection_dirty = true;
    }

    if (light_instance->is_projection_dirty) {
        Renderer *renderer = application::get_renderer();

        // Fetch the light
        Light *light = light::get(renderer, light_id);
        if (!light) {
            return DirectX::XMFLOAT4X4{
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1};
        }

        // Calculate light's view projection matrix based on type
        DirectX::XMMATRIX projection_matrix;
        switch (light->type) {
            case LIGHT_TYPE_DIRECTIONAL:
                projection_matrix = DirectX::XMMatrixOrthographicLH(
                    50.0f, 50.0f,
                    0.1f, 200.0f);
                break;
        }

        DirectX::XMStoreFloat4x4(&light_instance->projection_matrix, projection_matrix);

        light_instance->is_projection_dirty = false;
        light_instance->is_view_projection_dirty = true;
    }

    if (light_instance->is_view_projection_dirty) {
        DirectX::XMMATRIX vp_matrix = DirectX::XMLoadFloat4x4(&light_instance->view_matrix) * DirectX::XMLoadFloat4x4(&light_instance->projection_matrix);
        DirectX::XMStoreFloat4x4(&light_instance->view_projection_matrix, vp_matrix);
        light_instance->is_view_projection_dirty = false;
    }

    return light_instance->view_projection_matrix;
}

void scene::mesh_set_position(Scene *scene, Id scene_mesh_id, DirectX::XMFLOAT3 position) {
    assert(scene && "scene::mesh_set_position: Scene pointer cannot be NULL");
    assert(scene_mesh_id.id < MAX_SCENE_MESHES && "scene::mesh_set_position: Incorrect Scene Mesh Id");

    // Fetch the right mesh based on whether it is stale or not
    SceneMesh *sm = &scene->meshes[scene_mesh_id.id];
    if (id::is_fresh(sm->id, scene_mesh_id)) {
        sm->position = position;
        sm->is_dirty = true;
    }
}

void scene::mesh_set_rotation(Scene *scene, Id scene_mesh_id, DirectX::XMFLOAT3 rotation) {
    assert(scene && "scene::mesh_set_rotation: Scene pointer cannot be NULL");
    assert(scene_mesh_id.id < MAX_SCENE_MESHES && "scene::mesh_set_position: Incorrect Scene Mesh Id");

    // Fetch the right mesh based on whether it is stale or not
    SceneMesh *sm = &scene->meshes[scene_mesh_id.id];
    if (id::is_fresh(sm->id, scene_mesh_id)) {
        sm->rotation = rotation;
        sm->is_dirty = true;
    }
}

void scene::mesh_set_scale(Scene *scene, Id scene_mesh_id, DirectX::XMFLOAT3 scale) {
    assert(scene && "scene::mesh_set_scale: Scene pointer cannot be NULL");
    assert(scene_mesh_id.id < MAX_SCENE_MESHES && "scene::mesh_set_scale:: Incorrect Scene Mesh Id");

    // Fetch the right mesh based on whether it is stale or not
    SceneMesh *sm = &scene->meshes[scene_mesh_id.id];
    if (id::is_fresh(sm->id, scene_mesh_id)) {
        sm->scale = scale;
        sm->is_dirty = true;
    }
}

void scene::camera_set_position(Scene *scene, Id scene_cam_id, DirectX::XMFLOAT3 position) {
    assert(scene && "scene::camera_set_position: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_set_position: Incorrect Scene Camera Id");

    // Fetch the right mesh based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        cam->position = position;
        cam->is_view_dirty = true;
    }
}

void scene::camera_set_active(Scene *scene, Id scene_cam_id) {
    assert(scene && "scene::camera_set_active: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_set_active: Incorrect Scene Camera Id");

    // Fetch the right camera based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        scene->active_cam = cam;
    }
}

void scene::camera_set_active_aspect_ratio(Scene *scene, float aspect_ratio) {
    assert(scene && "scene::camera_set_active_aspect_ratio: Scene pointer cannot be NULL");
    scene->active_cam->base.aspect_ratio = aspect_ratio;
    scene->active_cam->is_projection_dirty = true;
}

void scene::camera_set_target(Scene *scene, Id scene_cam_id, DirectX::XMFLOAT3 target) {
    assert(scene && "scene::camera_set_target: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_set_target: Incorrect Scene Camera Id");

    // Fetch the right camera based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        cam->target = target;
        cam->is_view_dirty = true;
    }
}

void scene::camera_set_up(Scene *scene, Id scene_cam_id, DirectX::XMFLOAT3 up) {
    assert(scene && "scene::camera_set_up: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_set_up: Incorrect Scene Camera Id");

    // Fetch the right camera based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        cam->up = up;
        cam->is_view_dirty = true;
    }
}

void scene::camera_set_yaw_pitch(Scene *scene, SceneId scene_cam_id, float yaw, float pitch) {
    assert(scene && "scene::camera_set_yaw_pitch: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_set_yaw_pitch: Incorrect Scene Camera Id");

    // Fetch the right camera based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        cam->yaw = yaw;
        cam->pitch = pitch;

        // Recalculate position based on new yaw/pitch
        DirectX::XMVECTOR direction = DirectX::XMVectorSet(cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw), 0.0f);

        // Scale by distance
        direction = DirectX::XMVectorScale(direction, cam->distance);

        // Add to target to get new position
        DirectX::XMVECTOR target = DirectX::XMLoadFloat3(&cam->target);
        DirectX::XMVECTOR position = DirectX::XMVectorAdd(target, direction);

        // Store result back to position
        DirectX::XMStoreFloat3(&cam->position, position);

        cam->is_view_dirty = true;
    }
}

void scene::camera_set_distance(Scene *scene, SceneId scene_cam_id, float distance) {
    assert(scene && "scene::camera_set_distance: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_set_distance: Incorrect Scene Camera Id");

    // Fetch the right camera based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        cam->distance = std::fmax(distance, 0.01f);

        // Update the the position based on the new distance to target
        DirectX::XMVECTOR pos_vec = DirectX::XMLoadFloat3(&cam->position);
        DirectX::XMVECTOR tar_vec = DirectX::XMLoadFloat3(&cam->target);
        DirectX::XMVECTOR view_dir = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(pos_vec, tar_vec));

        DirectX::XMVECTOR new_pos = DirectX::XMVectorAdd(DirectX::XMVectorScale(view_dir, cam->distance), tar_vec);
        DirectX::XMStoreFloat3(&cam->position, new_pos);

        cam->is_view_dirty = true;
    }
}

void scene::camera_pan(Scene *scene, SceneId scene_cam_id, float dx, float dy) {
    assert(scene && "scene::camera_pan: Scene pointer cannot be NULL");
    assert(scene_cam_id.id < MAX_SCENE_CAMERAS && "scene::camera_pan: Incorrect Scene Camera Id");

    // Fetch the right camera based on whether it is stale or not
    SceneCamera *cam = &scene->cameras[scene_cam_id.id];
    if (id::is_fresh(cam->id, scene_cam_id)) {
        DirectX::XMVECTOR pos_vec = DirectX::XMLoadFloat3(&cam->position);
        DirectX::XMVECTOR tar_vec = DirectX::XMLoadFloat3(&cam->target);
        DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        DirectX::XMVECTOR view = DirectX::XMVectorSubtract(tar_vec, pos_vec);
        view = DirectX::XMVector3Normalize(view);

        DirectX::XMVECTOR right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(up, view));
        DirectX::XMVECTOR cam_up = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(view, right));

        DirectX::XMVECTOR pan_offset = DirectX::XMVectorAdd(DirectX::XMVectorScale(right, -dx), DirectX::XMVectorScale(cam_up, dy));
        pos_vec = DirectX::XMVectorAdd(pos_vec, pan_offset);
        tar_vec = DirectX::XMVectorAdd(tar_vec, pan_offset);

        DirectX::XMStoreFloat3(&cam->position, pos_vec);
        DirectX::XMStoreFloat3(&cam->target, tar_vec);

        cam->is_view_dirty = true;
    }
}
