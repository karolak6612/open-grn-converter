/**
 * @file file_dialog.h
 * @brief Native file and folder open/save dialogs.
 */

#pragma once

#include <string>
#include <optional>
#include <vector>

namespace grn {

/**
 * @brief Opens a native file selection dialog.
 * @param filter File filter description (e.g. "Model Files (*.grn;*.glb)\0*.grn;*.glb\0All Files (*.*)\0*.*\0").
 * @param title Dialog window title.
 * @return Selected file path string, or std::nullopt if cancelled.
 */
std::optional<std::string> open_file_dialog(const char* filter = "All Supported Files (*.grn;*.glb;*.gltf)\0*.grn;*.glb;*.gltf\0All Files (*.*)\0*.*\0",
                                           const char* title = "Select Model File");

/**
 * @brief Opens a native folder selection dialog.
 * @param title Dialog window title.
 * @return Selected folder path string, or std::nullopt if cancelled.
 */
std::optional<std::string> open_folder_dialog(const char* title = "Select Folder");

} // namespace grn
