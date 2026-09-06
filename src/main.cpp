/**
 * @file main.cpp
 * @brief Main entry point for grn_converter, dispatching between CLI and GUI modes.
 */

#include "cli/cli_main.h"
#include "gui/app.h"
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <iostream>
#endif

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

#ifdef _WIN32
    // If launched with CLI arguments, ensure output goes to console or pipe
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD outType = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE) ? GetFileType(hStdOut) : FILE_TYPE_UNKNOWN;

    if (outType == FILE_TYPE_PIPE || outType == FILE_TYPE_DISK) {
        // Output is piped or redirected to file (e.g. CI, scripts, automation)
        AttachConsole(ATTACH_PARENT_PROCESS);
    } else {
        // Running interactively in a console/terminal
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp = nullptr;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
        }
    }
    std::ios::sync_with_stdio(true);
#endif

    // Launch headless CLI mode
    return grn::run_cli(argc, argv);
}
