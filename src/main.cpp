/**
 * @file main.cpp
 * @brief Main entry point for grn_converter, dispatching between CLI and GUI modes.
 */

#include "cli/cli_main.h"
#include "gui/app.h"
#include <string>

int main(int argc, char* argv[]) {
    // Check if GUI mode should be launched
    bool force_gui = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--gui") {
            force_gui = true;
            break;
        }
    }

    if (argc == 1 || force_gui) {
        // Launch Dear ImGui GUI mode
        grn::App app;
        return app.run();
    }

    // Launch headless CLI mode
    return grn::run_cli(argc, argv);
}
