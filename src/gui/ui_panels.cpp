/**
 * @file ui_panels.cpp
 * @brief Implementation of the 2-panel non-docking Dear ImGui GUI.
 */

#include "ui_panels.h"
#include "file_dialog.h"
#include "../converter/converter.h"
#include <imgui.h>
#include <cstring>

namespace grn {

UiPanels::UiPanels() {
    add_log("GRN Converter initialized. Ready for conversion.", LogEntry::Level::Info);
}

void UiPanels::add_log(const std::string& msg, LogEntry::Level level) {
    logs.push_back({msg, level});
}

void UiPanels::set_input_path(const std::string& path) {
    std::strncpy(input_path_buf, path.c_str(), sizeof(input_path_buf) - 1);
    if (std::filesystem::is_directory(path)) {
        is_batch_mode = true;
    }
}

void UiPanels::render() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus |
                                    ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));

    if (ImGui::Begin("MainLayout", nullptr, window_flags)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::MenuItem("About")) {
                show_about_modal = true;
            }
            ImGui::EndMenuBar();
        }

        render_about_modal();

        float avail_w = ImGui::GetContentRegionAvail().x;
        float left_w = std::max(400.0f, avail_w * 0.48f);

        // Left Panel: Settings and Operations
        ImGui::BeginChild("LeftPanel", ImVec2(left_w, 0), true);
        render_controls_panel();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right Panel: Progress and Log
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
        render_log_panel();
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void UiPanels::render_about_modal() {
    if (show_about_modal) {
        ImGui::OpenPopup("About##AboutModal");
        show_about_modal = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);

    bool p_open = true;
    if (ImGui::BeginPopupModal("About##AboutModal", &p_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "open-grn Converter");
        ImGui::SameLine();
        ImGui::TextDisabled("v0.0.1 (Proof of Concept)");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("open-grn is an open-source bidirectional 3D model converter between glTF 2.0 (.glb / .gltf) and Granny 1.2b (.grn) container formats.");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Target Game Engine:");
        ImGui::TextWrapped("This program was specifically designed to work with Sacred Gold game.");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Status & Version:");
        ImGui::BulletText("Proof of Concept");
        ImGui::BulletText("Version: 0.0.1");
        ImGui::BulletText("Bidirectional mesh, skeleton, material, and texture conversion");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "License:");
        ImGui::TextWrapped("Released under the MIT License.");
        ImGui::TextDisabled("Free and open-source software.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(120, 0)) || !p_open) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

static void HelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void UiPanels::render_controls_panel() {
    ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "GRN <-> GLB Converter");
    ImGui::Separator();
    ImGui::Spacing();

    // Mode Selection
    ImGui::Text("Mode:");
    ImGui::SameLine();
    HelpMarker("Single File: Convert an individual model file.\nBatch Folder: Recursively convert all supported models (.grn / .glb) inside a directory.");
    ImGui::SameLine();
    if (ImGui::RadioButton("Single File##ModeRadio", !is_batch_mode)) {
        is_batch_mode = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Batch Folder##ModeRadio", is_batch_mode)) {
        is_batch_mode = true;
    }
    ImGui::Spacing();

    const float browse_btn_w = 75.0f;
    const float spacing_x = ImGui::GetStyle().ItemSpacing.x;

    // Input File / Folder
    ImGui::Text(is_batch_mode ? "Input Directory:" : "Input Model File:");
    ImGui::SameLine();
    HelpMarker("Path to the input file (.grn, .glb, or .gltf) or folder to convert.");
    float in_w = std::max(120.0f, ImGui::GetContentRegionAvail().x - browse_btn_w - spacing_x);
    ImGui::SetNextItemWidth(in_w);
    ImGui::InputText("##InputPath", input_path_buf, sizeof(input_path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##In", ImVec2(browse_btn_w, 0))) {
        if (is_batch_mode) {
            auto folder = open_folder_dialog("Select Input Directory");
            if (folder) set_input_path(*folder);
        } else {
            auto file = open_file_dialog();
            if (file) set_input_path(*file);
        }
    }

    // Output File / Folder
    ImGui::Text(is_batch_mode ? "Output Directory (Optional):" : "Output File (Optional):");
    ImGui::SameLine();
    HelpMarker("Optional destination path.\n- If left blank: The converter automatically outputs the converted file into the same directory as the input file, with the matching opposite extension (.grn <-> .glb).\n- If specified: Saves to the designated custom path or filename.");
    float out_w = std::max(120.0f, ImGui::GetContentRegionAvail().x - browse_btn_w - spacing_x);
    ImGui::SetNextItemWidth(out_w);
    ImGui::InputText("##OutputPath", output_path_buf, sizeof(output_path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Browse...##Out", ImVec2(browse_btn_w, 0))) {
        if (is_batch_mode) {
            auto folder = open_folder_dialog("Select Output Directory");
            if (folder) std::strncpy(output_path_buf, folder->c_str(), sizeof(output_path_buf) - 1);
        } else {
            auto file = open_file_dialog();
            if (file) std::strncpy(output_path_buf, file->c_str(), sizeof(output_path_buf) - 1);
        }
    }

    // Optional Animation File (Single file mode only)
    if (!is_batch_mode) {
        ImGui::Spacing();
        ImGui::Text("Animation File (.grn) (Optional):");
        ImGui::SameLine();
        HelpMarker("Optional external Granny 1.2b animation track (.grn) to merge into the exported GLB model (e.g. attack, idle, or run animation).");
        float anim_w = std::max(120.0f, ImGui::GetContentRegionAvail().x - browse_btn_w - spacing_x);
        ImGui::SetNextItemWidth(anim_w);
        ImGui::InputText("##AnimPath", anim_path_buf, sizeof(anim_path_buf));
        ImGui::SameLine();
        if (ImGui::Button("Browse...##Anim", ImVec2(browse_btn_w, 0))) {
            auto file = open_file_dialog("GRN Animation Files (*.grn)\0*.grn\0All Files (*.*)\0*.*\0", "Select Animation File");
            if (file) std::strncpy(anim_path_buf, file->c_str(), sizeof(anim_path_buf) - 1);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.28f, 1.0f), "Conversion Options");

    // Direction
    ImGui::Text("Direction:");
    ImGui::SameLine();
    HelpMarker("Auto-detect: Automatically selects direction based on input extension (.grn -> .glb, .glb -> .grn).\nGRN -> GLB: Force converting Granny 1.2b container into glTF 2.0 / GLB.\nGLB -> GRN: Force converting glTF 2.0 / GLB into Granny 1.2b container.");
    const char* dir_items[] = {"Auto-detect", "GRN -> GLB", "GLB -> GRN"};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##DirectionCombo", &direction_item, dir_items, 3);

    // Texture Embedding
    ImGui::Checkbox("Embed textures inside file", &options.embed_textures);
    ImGui::SameLine();
    HelpMarker("Enabled (Default): All texture bitmaps/codecs are embedded directly inside the single .glb or .grn binary container.\nDisabled: Textures are extracted and saved as loose external image files next to the model file.");

    // Loose Texture Format
    ImGui::BeginDisabled(options.embed_textures);
    ImGui::Text("Loose Texture Format:");
    ImGui::SameLine();
    HelpMarker("Selects the file format used when saving loose textures (TGA, PNG, or VTex).\nOnly active when 'Embed textures inside file' is unchecked.");
    const char* fmt_items[] = {"TGA (Truecolor 32-bit)", "PNG", "VTex"};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##LooseTextureFormatCombo", &texture_format_item, fmt_items, 3);
    if (texture_format_item == 0) options.texture_format = "tga";
    else if (texture_format_item == 1) options.texture_format = "png";
    else options.texture_format = "vtex";
    ImGui::EndDisabled();

    // VTex Codec Toggle
    ImGui::Checkbox("Enable VTex video texture codec", &options.vtex_enabled);
    ImGui::SameLine();
    HelpMarker("Enables the native VTex video texture compressor.\nVTex compresses embedded textures inside the GRN file into compact video streams.\nIf unchecked, embedded textures will fall back to uncompressed raw RGBA bitmaps.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.28f, 1.0f), "Coordinates & Scale");

    // Coordinate Conversion
    ImGui::Checkbox("Convert coordinates (Y-up glTF <-> Z-up GRN)", &options.y_up);
    ImGui::SameLine();
    HelpMarker("glTF uses a Y-up coordinate system, while Granny 1.2b / Sacred uses Z-up.\nChecking this ensures the model stands upright in Sacred.\n(Default: Enabled).");

    // Scale Controls
    ImGui::Text("Scale Mode:");
    ImGui::SameLine();
    HelpMarker("Multiplier: Multiplies all coordinates and bone positions by a uniform number.\nTarget Height: Automatically measures the model's bounding box and scales it so its total height equals the given value in game units.");
    ImGui::SameLine();
    ImGui::RadioButton("Multiplier##ScaleModeRadio", &scale_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Target Height##ScaleModeRadio", &scale_mode, 1);

    if (scale_mode == 0) {
        ImGui::Text("Scale Factor:");
        ImGui::SameLine();
        HelpMarker("Uniform scale multiplier applied to model geometry. Default is 1.0 (as-is).\nFor standard glTF models authored in meters, use 39.37 to convert meters to Sacred inches.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##ScaleFactorInput", &scale_factor, 0.1f, 1.0f, "%.4f");
        if (scale_factor < 0.0001f) scale_factor = 0.0001f;
    } else {
        ImGui::Text("Target Height (Game Units):");
        ImGui::SameLine();
        HelpMarker("Desired model height in Sacred game units.\nSacred references:\n- Player Character (Seraphim): ~73 units (1.85m)\n- Street Lamp / Signpost: ~107 units (2.7m)\n- Two-Handed Weapons: ~65-80 units (1.7-2.0m)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputFloat("##TargetHeightInput", &target_height, 1.0f, 10.0f, "%.2f");
        if (target_height < 0.0f) target_height = 0.0f;
    }

    if (ImGui::Button("Reset to 1.0x (Default)##ResetScale", ImVec2(-1, 0))) {
        scale_mode = 0;
        scale_factor = 1.0f;
        target_height = 0.0f;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Resets scaling to the default 1.0x multiplier.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Action Button (Warm Radiant Gold from the theme)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.38f, 0.16f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.54f, 0.20f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.68f, 0.26f, 1.00f));

    std::string btn_label = is_batch_mode ? "Start Batch Conversion" : "Convert Model";
    if (ImGui::Button(btn_label.c_str(), ImVec2(-1, 38))) {
        execute_conversion();
    }
    ImGui::PopStyleColor(3);
}

void UiPanels::render_log_panel() {
    ImGui::Text("Progress:");
    ImGui::ProgressBar(current_progress, ImVec2(-1, 20), (std::to_string(static_cast<int>(current_progress * 100.0f)) + "%").c_str());

    ImGui::Spacing();
    ImGui::Text("Operation Console:");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
    if (ImGui::SmallButton("Clear Log")) {
        logs.clear();
    }

    ImGui::Separator();
    ImGui::BeginChild("LogConsole", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& entry : logs) {
        ImVec4 col(0.85f, 0.85f, 0.85f, 1.0f);
        if (entry.level == LogEntry::Level::Success) col = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
        else if (entry.level == LogEntry::Level::Warning) col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        else if (entry.level == LogEntry::Level::Error) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(entry.text.c_str());
        ImGui::PopStyleColor();
    }

    if (auto_scroll_log && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

void UiPanels::execute_conversion() {
    std::string in_path = input_path_buf;
    if (in_path.empty()) {
        add_log("Error: Please select an input path.", LogEntry::Level::Error);
        return;
    }

    if (direction_item == 0) options.direction = ConversionDirection::Auto;
    else if (direction_item == 1) options.direction = ConversionDirection::GrnToGlb;
    else options.direction = ConversionDirection::GlbToGrn;

    if (scale_mode == 0) {
        options.scale = scale_factor;
        options.target_height = 0.0f;
    } else {
        options.scale = 1.0f;
        options.target_height = target_height;
    }

    std::string anim_p = anim_path_buf;
    options.anim_file = anim_p.empty() ? std::filesystem::path() : std::filesystem::path(anim_p);

    std::filesystem::path out_p = output_path_buf[0] ? std::filesystem::path(output_path_buf) : std::filesystem::path();

    current_progress = 0.0f;
    add_log("--- Starting Conversion Task ---", LogEntry::Level::Info);

    auto cb = [this](const std::string& item, float prog, bool ok, const std::string& msg) {
        (void)item;
        current_progress = prog;
        add_log(msg, ok ? (prog >= 1.0f ? LogEntry::Level::Success : LogEntry::Level::Info) : LogEntry::Level::Error);
    };

    if (is_batch_mode || std::filesystem::is_directory(in_path)) {
        auto res = convert_directory(in_path, out_p, options, cb);
        add_log("Batch complete. Succeeded: " + std::to_string(res.files_succeeded) + ", Failed: " + std::to_string(res.files_failed),
                res.files_failed == 0 ? LogEntry::Level::Success : LogEntry::Level::Warning);
    } else {
        bool ok = convert_file(in_path, out_p, options, cb);
        current_progress = ok ? 1.0f : 0.0f;
    }
}

} // namespace grn
