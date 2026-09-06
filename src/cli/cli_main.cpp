/**
 * @file cli_main.cpp
 * @brief Implementation of headless CLI argument parsing and execution.
 */

#include "cli_main.h"
#include "../converter/converter.h"
#include <iostream>
#include <iomanip>

namespace grn {

static void print_help() {
    std::cout << "Usage: grn_converter [OPTIONS] <INPUT> [OUTPUT]\n\n"
              << "Arguments:\n"
              << "  INPUT                    Path to input file (.grn / .glb) or folder (with -b)\n"
              << "  OUTPUT                   Optional destination file path or folder\n\n"
              << "Options:\n"
              << "  -b, --batch              Batch process all compatible files in the input folder\n"
              << "  --no-embed-textures      Save textures as loose files next to model instead of embedding\n"
              << "  --texture-format <fmt>   Format for loose textures: 'tga' (default), 'png', or 'vtex'\n"
              << "  --no-vtex                Disable VTex video codec; fall back to uncompressed or loose files\n"
              << "  -t, --textures-dir <dir> Search path for external or cached textures\n"
              << "  --anim <path>            External animation .grn track to merge into output model\n"
              << "  --tint-pink              Test feature: apply pink tint filter to textures\n"
              << "  --scale <val>            Global scale factor applied to geometry/bones\n"
              << "  --target-height <val>    Target height in game units (e.g. 107 for post, 73 for human)\n"
              << "  --meters                 Standard glTF metric conversion (scale = 39.37 inches/meter)\n"
              << "  --z-up                   Preserve native Z-up coordinates (default converts to Y-up)\n"
              << "  --gui                    Force launching the Dear ImGui GUI window\n"
              << "  -h, --help               Display this help message and exit\n";
}

int run_cli(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 1;
    }

    std::filesystem::path input_path;
    std::filesystem::path output_path;
    ConversionOptions options;
    bool is_batch = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help();
            return 0;
        } else if (arg == "-b" || arg == "--batch") {
            is_batch = true;
        } else if (arg == "--no-embed-textures") {
            options.embed_textures = false;
        } else if (arg == "--texture-format" && i + 1 < argc) {
            options.texture_format = argv[++i];
        } else if (arg == "--no-vtex") {
            options.vtex_enabled = false;
        } else if ((arg == "--textures-dir" || arg == "-t") && i + 1 < argc) {
            options.textures_dir = argv[++i];
        } else if (arg == "--anim" && i + 1 < argc) {
            options.anim_file = argv[++i];
        } else if (arg == "--tint-pink") {
            options.tint_pink = true;
        } else if (arg == "--scale" && i + 1 < argc) {
            options.scale = std::stof(argv[++i]);
        } else if ((arg == "--target-height" || arg == "--height") && i + 1 < argc) {
            options.target_height = std::stof(argv[++i]);
        } else if (arg == "--meters") {
            options.scale = 39.37007874f;
        } else if (arg == "--z-up") {
            options.y_up = false;
        } else if (arg == "--gui") {
            // Handled in main
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << "\n";
            print_help();
            return 1;
        } else {
            if (input_path.empty()) {
                input_path = arg;
            } else if (output_path.empty()) {
                output_path = arg;
            }
        }
    }

    if (input_path.empty()) {
        std::cerr << "Error: No input path specified.\n";
        print_help();
        return 1;
    }

    auto cli_callback = [](const std::string& item, float progress, bool success, const std::string& message) {
        if (!message.empty()) {
            std::cout << "[" << std::setw(3) << static_cast<int>(progress * 100.0f) << "%] "
                      << (success ? "OK : " : "ERR: ") << message << "\n";
        }
    };

    if (is_batch || std::filesystem::is_directory(input_path)) {
        std::cout << "Starting batch conversion on: " << input_path << "\n";
        auto res = convert_directory(input_path, output_path, options, cli_callback);
        std::cout << "\nBatch finished. Succeeded: " << res.files_succeeded
                  << ", Failed: " << res.files_failed << "\n";
        return res.files_failed == 0 ? 0 : 1;
    } else {
        bool ok = convert_file(input_path, output_path, options, cli_callback);
        return ok ? 0 : 1;
    }
}

} // namespace grn
