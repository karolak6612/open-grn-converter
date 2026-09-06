/**
 * @file ui_panels.h
 * @brief GUI panels, widgets, and state management for the Dear ImGui interface.
 */

#pragma once

#include "../converter/options.h"
#include <string>
#include <vector>

namespace grn {

struct LogEntry {
    std::string text;
    enum class Level { Info, Success, Warning, Error } level = Level::Info;
};

class UiPanels {
public:
    UiPanels();

    // Renders the full 2-panel interface
    void render();

    // Adds a message to the live GUI console
    void add_log(const std::string& msg, LogEntry::Level level = LogEntry::Level::Info);

    // Sets input path (e.g. from drag-and-drop or file dialog)
    void set_input_path(const std::string& path);

private:
    bool is_batch_mode = false;
    char input_path_buf[1024] = {};
    char output_path_buf[1024] = {};
    char anim_path_buf[1024] = {};

    ConversionOptions options;
    int direction_item = 0; // 0=Auto, 1=GRN->GLB, 2=GLB->GRN
    int texture_format_item = 0; // 0=tga, 1=png, 2=vtex
    int scale_mode = 0; // 0=Multiplier, 1=Target Height
    float scale_factor = 1.0f;
    float target_height = 0.0f;

    float current_progress = 0.0f;
    std::string status_text = "Ready";
    std::vector<LogEntry> logs;
    bool auto_scroll_log = true;
    bool is_converting = false;
    bool show_about_modal = false;

    void render_controls_panel();
    void render_log_panel();
    void render_about_modal();
    void execute_conversion();
};

} // namespace grn
