/**
 * @file glb_reader.h
 * @brief Deserializer for glTF 2.0 / GLB files into GRN model representations.
 */

#pragma once

#include "../core/grn_types.h"
#include <filesystem>
#include <optional>

namespace grn {

/**
 * @brief Options for importing glTF 2.0 / GLB files.
 */
struct GlbImportOptions {
    bool y_up = true; /**< Converts glTF standard Y-up coordinates back to native GRN Z-up. */
    std::filesystem::path texture_dir; /**< Optional external directory to resolve loose texture files. */
    bool prefer_vtex = true; /**< Prefer VTex encoding for imported textures. */
    float scale = 1.0f;          /**< Uniform scale factor applied to positions and bone translations. */
    float target_height = 0.0f;  /**< Optional target height (if > 0, auto-calculates scale from model bounds). */
};

/**
 * @brief Loads a glTF 2.0 / GLB binary file and converts it into a GrnModel.
 * @param path Path to the .glb or .gltf file on disk.
 * @param options Import configuration options.
 * @return Populated GrnModel if successful, std::nullopt otherwise.
 */
std::optional<GrnModel> load_glb_file(const std::filesystem::path& path, const GlbImportOptions& options = {});

/**
 * @brief Loads glTF 2.0 / GLB binary data from a memory buffer.
 * @param data Pointer to raw GLB buffer.
 * @param size Size of the buffer in bytes.
 * @param options Import configuration options.
 * @return Populated GrnModel if successful, std::nullopt otherwise.
 */
std::optional<GrnModel> load_glb_memory(const uint8_t* data, size_t size, const GlbImportOptions& options = {});

} // namespace grn
