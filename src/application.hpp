#pragma once

#include "input.hpp"
#include "renderer.hpp"
#include "window.hpp"

#define MAX_SCENES 6

struct ApplicationConfig {
    const wchar_t *window_title;
    uint16_t window_width;
    uint16_t window_height;
    std::string *mesh_path;
};

struct AppState {
    ApplicationConfig config;

    Input input;
    Window window;
    Renderer renderer;
    Scene scenes[MAX_SCENES];
    Scene *active_scene;
};

namespace application {

bool initialize(ApplicationConfig config);
void shutdown();
void update();
void run();

bool deserialize_config();

Id add_scene();
void set_active_scene(Id scene);

Window *get_window();
Renderer *get_renderer();
Scene *get_scenes();

} // namespace application
