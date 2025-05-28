#include "application.hpp"
#include "logger.hpp"

int main(int argc, char *argv[]) {
    // Default mesh's path
    std::string meshpath = "assets/cube.obj";

    for (int i = 1; i < argc; ++i) {
        std::string current_arg = argv[i];

        if (current_arg == "-f" || current_arg == "--file") {
            if (i + 1 < argc) {
                meshpath = argv[i + 1];
                i++;
            } else {
                LOG("Error: %s option requires a filepath.", current_arg.c_str());
                return 1;
            }
        }
    }

    ApplicationConfig cfg = {};
    cfg.window_title = L"PBR Demo";
    cfg.window_width = 1920;
    cfg.window_height = 1080;
    cfg.mesh_path = &meshpath;

    application::initialize(cfg);
    application::run();

    return 0;
}
