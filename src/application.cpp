#include "application.hpp"

#include "id.hpp"
#include "idmap.hpp"
#include "input.hpp"
#include "light.hpp"
#include "logger.hpp"
#include "material.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "scene.hpp"
#include "texture.hpp"

#include <DirectXMath.h>
#include <cJSON.h>

static AppState *pState = nullptr;

// TEMP: Temp storage of some reused id's...
SceneId main_mesh = id::invalid();
SceneId default_cam = id::invalid();

bool application::initialize(ApplicationConfig config) {
    // Dynamically allocating the application state here,
    // so the user doesn't have to pass in anything.
    // (Though, I'm the user... anyway)
    pState = new AppState;

    // Copy the config to Application State
    pState->config = config;

    // Invalidate all scenes
    for (int i = 0; i < MAX_SCENES; ++i) {
        pState->scenes[i].id = id::invalid();
    }

    // Initialize the Window
    if (!window::create(config.window_title, config.window_width, config.window_height, &pState->window)) {
        LOG("Application error: Couldn't create window");
        return false;
    }

    // Initialize the Input system
    if (!input::initialize(&pState->input)) {
        LOG("Application error: Couldn't initialize input system");
        return false;
    }

    // Initialize the renderer
    if (!renderer::initialize(&pState->renderer, &pState->window)) {
        LOG("Application error: Couldn't initialize renderer");
        return false;
    }

    deserialize_config();

    // HACK: Adding a directional light here to test
    LightId dir_light = light::create(LIGHT_TYPE_DIRECTIONAL, DirectX::XMFLOAT3(1, 1, 1), 1.0);
    scene::add_light(&pState->scenes[0], dir_light, DirectX::XMFLOAT3(55, 100, 0), DirectX::XMFLOAT3(0, 0, 0), true);

    return true;
}

void application::shutdown() {
    if (pState) {
        window::destroy(&pState->window);

        delete pState;
    }
    pState = nullptr;
}

// TODO: move these away or something
#define DEG2RAD (DirectX::XM_PI / 180.0f)
static bool should_rotate = false;
void application::update() {
    if (input::is_key_pressed(KEY_R)) {
        should_rotate = should_rotate ? false : true;
    }

    // Orbit - Right Mouse Button
    if (input::is_mouse_button_down(MOUSE_BUTTON_RIGHT)) {
        int dx = input::mouse_get_delta_x();
        int dy = input::mouse_get_delta_y();

        float yaw = scene::camera_get_yaw(&pState->scenes[0], pState->scenes[0].active_cam->id);
        float pitch = scene::camera_get_pitch(&pState->scenes[0], pState->scenes[0].active_cam->id);

        // Apply input
        yaw += dx * 0.01f;
        pitch += dy * 0.01;

        // Optional: Clamp pitch to prevent flipping
        float max_pitch = DirectX::XM_PIDIV2 - 0.01f;
        float min_pitch = -DirectX::XM_PIDIV2 + 0.01f;
        pitch = std::clamp(pitch, min_pitch, max_pitch);

        // Set the new yaw and pitch
        scene::camera_set_yaw_pitch(&pState->scenes[0], pState->scenes[0].active_cam->id, yaw, pitch);
    }

    // Zoom - Mouse Scroll Wheel
    if (input::mouse_get_wheel() != 0) {
        float dist = scene::camera_get_distance(&pState->scenes[0], pState->scenes[0].active_cam->id);
        float zoom_speed = dist * 0.1f;
        dist -= input::mouse_get_wheel() * zoom_speed;
        scene::camera_set_distance(&pState->scenes[0], pState->scenes[0].active_cam->id, dist);
    }

    // Panning - Left Mouse Button
    if (input::is_mouse_button_down(MOUSE_BUTTON_LEFT)) {
        float dx = (float)input::mouse_get_delta_x();
        float dy = (float)input::mouse_get_delta_y();

        float dist = scene::camera_get_distance(&pState->scenes[0], pState->scenes[0].active_cam->id);
        float pan_speed = dist * 0.001f;

        scene::camera_pan(&pState->scenes[0], pState->scenes[0].active_cam->id, dx * pan_speed, dy * pan_speed);
    }

    if (should_rotate) {
        float yaw = scene::camera_get_yaw(&pState->scenes[0], pState->scenes[0].active_cam->id);
        float pitch = scene::camera_get_pitch(&pState->scenes[0], pState->scenes[0].active_cam->id);

        // Apply yaw
        yaw += 0.8f * 0.01f;

        // Set the new yaw and pitch
        scene::camera_set_yaw_pitch(&pState->scenes[0], pState->scenes[0].active_cam->id, yaw, pitch);
    }
}

void application::run() {
    if (!pState)
        return;

    while (!window::should_close(&pState->window)) {
        // TODO: Maybe this is better to be in a different "platform"
        // namespace, or just completely in a different unit...
        window::proc_messages();

        update();

        renderer::begin_frame(&pState->renderer, &pState->scenes[0]);
        renderer::render(&pState->renderer, &pState->scenes[0]);
        renderer::end_frame(&pState->renderer);

        input::swap_buffers(&pState->input);
    }

    // Shutdown here...
    shutdown();
}

bool application::deserialize_config() {
    // Load config file
    FILE *cfg_file = fopen("assets/config.json", "rb");
    if (!cfg_file) {
        LOG("application::deserialize_config: Couldn't open the config file: config.json");
        return false;
    }

    fseek(cfg_file, 0, SEEK_END);
    size_t length = ftell(cfg_file);
    fseek(cfg_file, 0, SEEK_SET);
    char *cfg = (char *)malloc(length + 1);
    if (!cfg) {
        LOG("application::deserialize_config: Something went wrong");
        return false;
    }

    fread(cfg, 1, length, cfg_file);
    fclose(cfg_file);
    cfg[length] = '\0';

    // Map to keep track of the json id -> asset id
    IdMap tex_map;
    IdMap mesh_map;
    IdMap mat_map;

    // Parse JSON string
    cJSON *root = cJSON_Parse(cfg);
    assert(root);

    // Parse textures
    cJSON *textures = cJSON_GetObjectItem(root, "textures");
    cJSON *tex = nullptr;
    cJSON_ArrayForEach(tex, textures) {
        int id = cJSON_GetObjectItem(tex, "id")->valueint;
        const char *path = cJSON_GetObjectItem(tex, "path")->valuestring;
        bool srgb = cJSON_GetObjectItem(tex, "srgb")->valueint;
        TextureId tex_id = texture::load(path, srgb);
        idmap::add(&tex_map, id, tex_id);
    }

    // Parse materials
    cJSON *materials = cJSON_GetObjectItem(root, "materials");
    cJSON *mat = nullptr;
    cJSON_ArrayForEach(mat, materials) {
        int id = cJSON_GetObjectItem(mat, "id")->valueint;
        cJSON *albedo_color = cJSON_GetObjectItem(mat, "albedo");
        float roughness = cJSON_GetObjectItem(mat, "roughness")->valuedouble;
        float metallic = cJSON_GetObjectItem(mat, "metallic")->valuedouble;
        float emission_intensity = cJSON_GetObjectItem(mat, "emission")->valuedouble;

        DirectX::XMFLOAT3 albedo(
            cJSON_GetArrayItem(albedo_color, 0)->valuedouble,
            cJSON_GetArrayItem(albedo_color, 1)->valuedouble,
            cJSON_GetArrayItem(albedo_color, 2)->valuedouble, );

        Id mat_id = material::create(
            albedo,
            idmap::get(&tex_map, cJSON_GetObjectItem(mat, "albedo_map")->valueint),
            metallic,
            idmap::get(&tex_map, cJSON_GetObjectItem(mat, "metallic_map")->valueint),
            roughness,
            idmap::get(&tex_map, cJSON_GetObjectItem(mat, "roughness_map")->valueint),
            idmap::get(&tex_map, cJSON_GetObjectItem(mat, "normal_map")->valueint),
            emission_intensity,
            idmap::get(&tex_map, cJSON_GetObjectItem(mat, "emission_map")->valueint));

        idmap::add(&mat_map, id, mat_id);
    }

    // Parse meshes
    cJSON *meshes = cJSON_GetObjectItem(root, "meshes");
    cJSON *mesh = nullptr;
    cJSON_ArrayForEach(mesh, meshes) {
        int id = cJSON_GetObjectItem(mesh, "id")->valueint;
        const char *path = cJSON_GetObjectItem(mesh, "path")->valuestring;
        Id mesh_id = mesh::load(path);
        idmap::add(&mesh_map, id, mesh_id);
    }

    // Parse scene with their instances
    cJSON *scenes = cJSON_GetObjectItem(root, "scenes");
    cJSON *scene = nullptr;
    cJSON_ArrayForEach(scene, scenes) {
        Id new_scene = add_scene();
        if (id::is_invalid(new_scene)) {
            LOG("application::deserialize_config: Couldn't create new scene");
            free(cfg);
            return false;
        }

        // Parse cameras
        cJSON *cameras = cJSON_GetObjectItem(scene, "cameras");
        cJSON *cam = nullptr;
        cJSON_ArrayForEach(cam, cameras) {
            cJSON *cam_pos = cJSON_GetObjectItem(cam, "position");
            cJSON *cam_tar = cJSON_GetObjectItem(cam, "target");
            float fov = cJSON_GetObjectItem(cam, "fov")->valuedouble;
            float znear = cJSON_GetObjectItem(cam, "znear")->valuedouble;
            float zfar = cJSON_GetObjectItem(cam, "zfar")->valuedouble;

            DirectX::XMFLOAT3 cam_pos_vec(
                (float)cJSON_GetArrayItem(cam_pos, 0)->valuedouble,
                (float)cJSON_GetArrayItem(cam_pos, 1)->valuedouble,
                (float)cJSON_GetArrayItem(cam_pos, 2)->valuedouble);
            DirectX::XMFLOAT3 cam_pos_tar(
                (float)cJSON_GetArrayItem(cam_tar, 0)->valuedouble,
                (float)cJSON_GetArrayItem(cam_tar, 1)->valuedouble,
                (float)cJSON_GetArrayItem(cam_tar, 2)->valuedouble);

            scene::add_camera(&pState->scenes[new_scene.id], fov, znear, zfar, cam_pos_vec, cam_pos_tar);
        }

        // Parse mesh instances
        cJSON *mesh_instances = cJSON_GetObjectItem(scene, "meshes");
        cJSON *mi = nullptr;
        cJSON_ArrayForEach(mi, mesh_instances) {
            cJSON *m_pos = cJSON_GetObjectItem(mi, "position");
            cJSON *m_rot = cJSON_GetObjectItem(mi, "rotation");
            cJSON *m_scl = cJSON_GetObjectItem(mi, "scale");

            DirectX::XMFLOAT3 mi_position(
                cJSON_GetArrayItem(m_pos, 0)->valuedouble,
                cJSON_GetArrayItem(m_pos, 1)->valuedouble,
                cJSON_GetArrayItem(m_pos, 2)->valuedouble);

            DirectX::XMFLOAT3 mi_rotation(
                cJSON_GetArrayItem(m_rot, 0)->valuedouble,
                cJSON_GetArrayItem(m_rot, 1)->valuedouble,
                cJSON_GetArrayItem(m_rot, 2)->valuedouble);

            DirectX::XMFLOAT3 mi_scale(
                cJSON_GetArrayItem(m_scl, 0)->valuedouble,
                cJSON_GetArrayItem(m_scl, 1)->valuedouble,
                cJSON_GetArrayItem(m_scl, 2)->valuedouble);

            scene::add_mesh(
                &pState->scenes[new_scene.id],
                idmap::get(&mesh_map, cJSON_GetObjectItem(mi, "mesh_id")->valueint),
                idmap::get(&mat_map, cJSON_GetObjectItem(mi, "material_id")->valueint),
                mi_position, mi_rotation, mi_scale);
        }
    }

    free(cfg);
    cJSON_Delete(root);

    return true;
}

Id application::add_scene() {
    assert(pState && "application::add_scene: Application has not been started properly, or is in a corrupted state");

    // Linear search for empty scene slot
    Scene *s = nullptr;
    for (int i = 0; i < MAX_SCENES; ++i) {
        if (id::is_invalid(pState->scenes[i].id)) {
            s = &pState->scenes[i];
            s->id.id = i;
            break;
        }
    }

    if (!s) {
        LOG("application::add_scene: Couldn't find empty slot for new scene. Adjust the configuration to allow for more.");
        return id::invalid();
    }

    // Initialize the scene
    if (!scene::initialize(s)) {
        LOG("application::add_scene: Scene couldn't be initialized at the given slot");
        return id::invalid();
    }

    // If an active scene hasn't been set we set it here automatically
    if (pState->active_scene == nullptr) {
        set_active_scene(s->id);
    }

    return s->id;
}

void application::set_active_scene(Id scene) {
    assert(pState && "application::set_active_scene: Application has not been started properly, or is in a corrupted state");

    Scene *s = &pState->scenes[scene.id];
    assert(s && "application::set_active_scene: Tried to select an invalid scene");

    // Check for staleness, as well to make sure it's really the scene we need
    if (id::is_fresh(s->id, scene)) {
        pState->active_scene = s;
    }
}

Renderer *application::get_renderer() {
    return &pState->renderer;
}

Scene *application::get_scenes() {
    return pState->scenes;
}
