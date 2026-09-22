/**
 * @file main.cpp
 * @brief Main entry point for grn_converter, dispatching between CLI and Qt6 GUI modes.
 */

#include "cli/cli_main.h"
#include "gui/main_window.h"

#include <QApplication>
#include <oclero/qlementine/style/QlementineStyle.hpp>
#include <oclero/qlementine/style/ThemeManager.hpp>
#include <oclero/qlementine/icons/QlementineIcons.hpp>
#include <string>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <iostream>
#include <io.h>
#include <fcntl.h>
#endif

int main(int argc, char* argv[]) {
    // Check if GUI mode should be launched
    bool force_gui = false;
    std::string screenshot_path;
    std::string initial_theme = "Light";
    std::string preloaded_model;
    bool auto_convert = false;
    std::optional<bool> split_anims_opt;
    std::optional<bool> embed_anims_opt;
    std::optional<bool> embed_textures_opt;
    std::optional<int> active_tab_opt;
    bool preview_opt = false;
    std::string external_anim;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gui") {
            force_gui = true;
        } else if (arg == "--preview") {
            preview_opt = true;
            force_gui = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot_path = argv[++i];
            force_gui = true;
        } else if (arg == "--theme" && i + 1 < argc) {
            initial_theme = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            preloaded_model = argv[++i];
            force_gui = true;
        } else if (arg == "--anim" && i + 1 < argc) {
            external_anim = argv[++i];
            force_gui = true;
        } else if (arg == "--convert") {
            auto_convert = true;
            force_gui = true;
        } else if (arg == "--split-anims" && i + 1 < argc) {
            split_anims_opt = (std::string(argv[++i]) != "0");
        } else if (arg == "--embed-anims" && i + 1 < argc) {
            embed_anims_opt = (std::string(argv[++i]) != "0");
        } else if (arg == "--embed-textures" && i + 1 < argc) {
            embed_textures_opt = (std::string(argv[++i]) != "0");
        } else if (arg == "--tab" && i + 1 < argc) {
            active_tab_opt = std::stoi(argv[++i]);
        }
    }

    if (argc == 1 || force_gui) {
        QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
        QApplication app(argc, argv);

        QGuiApplication::setApplicationDisplayName("GRN <-> GLB Converter");
        QCoreApplication::setApplicationName("grn-converter");
        QCoreApplication::setOrganizationName("open-grn-converter");
        QCoreApplication::setApplicationVersion("1.0.0");

        // Custom Qlementine Style
        auto* style = new oclero::qlementine::QlementineStyle(&app);
        style->setAnimationsEnabled(true);
        style->setAutoIconColor(oclero::qlementine::AutoIconColor::TextColor);
        style->setIconPathGetter(oclero::qlementine::icons::fromFreeDesktop);
        app.setStyle(style);

        // Custom icon theme
        oclero::qlementine::icons::initializeIconTheme();
        QIcon::setThemeName("qlementine");

        // Theme manager
        auto* themeManager = new oclero::qlementine::ThemeManager(style);
        themeManager->loadDirectory(":/themes");
        themeManager->setCurrentTheme(QString::fromStdString(initial_theme));

        grn::MainWindow window(themeManager);
        if (!preloaded_model.empty()) {
            window.openPath(QString::fromStdString(preloaded_model));
        }
        if (split_anims_opt.has_value()) {
            window.setSplitAnims(*split_anims_opt);
        }
        if (embed_anims_opt.has_value()) {
            window.setEmbedAnims(*embed_anims_opt);
        }
        if (embed_textures_opt.has_value()) {
            window.setEmbedTextures(*embed_textures_opt);
        }
        if (active_tab_opt.has_value()) {
            window.setActiveTab(*active_tab_opt);
        }
        if (!external_anim.empty()) {
            window.addExternalAnimation(QString::fromStdString(external_anim));
            window.selectAnimationItem(0);
        }
        if (preview_opt) {
            window.setPreviewVisible(true);
        }
        window.show();
        app.processEvents();

        if (auto_convert) {
            window.executeConversion();
            while (window.isConverting()) {
                app.processEvents();
#ifdef _WIN32
                Sleep(20);
#endif
            }
            for (int f = 0; f < 10; ++f) {
                app.processEvents();
            }
        }

        if (!screenshot_path.empty()) {
            for (int f = 0; f < 30; ++f) {
                app.processEvents();
#ifdef _WIN32
                Sleep(10);
#endif
            }
            QPixmap pixmap = window.grab();
            bool ok = pixmap.save(QString::fromStdString(screenshot_path), "PNG");
            return ok ? 0 : 1;
        }

        return app.exec();
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
    int ret = grn::run_cli(argc, argv);
    std::cout.flush();
    std::cerr.flush();
    fflush(stdout);
    fflush(stderr);
    return ret;
}
