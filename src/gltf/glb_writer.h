/**
 * @file glb_writer.h
 * @brief Exporter serializing GrnModel representations into standard glTF 2.0 / GLB files.
 */

#pragma once

#include "../core/grn_types.h"
#include <filesystem>
#include <vector>
#include <string>

namespace grn {

/**
 * @struct GlbExportOptions
 * @brief Configuration parameters for exporting to glTF 2.0 / GLB.
 */
struct GlbExportOptions {
    bool y_up = true;                         /**< Convert native Z-up coordinates to standard glTF Y-up. */
    bool embed_textures = true;               /**< Embed textures inside the GLB binary container. */
    std::filesystem::path loose_texture_dir;  /**< Target directory for loose textures if not embedded. */
    std::string loose_texture_format = "tga"; /**< Format for loose textures ("tga", "png", "vtex"). */
    std::string model_stem;                   /**< Base name for output texture files when saved loose. */
};

/**
 * @brief Exports a GrnModel to a standalone .GLB file on disk.
 * @param path Destination path for the .glb file.
 * @param model Source GrnModel.
 * @param options Export configuration options.
 * @return True on success, false on failure.
 */
bool export_grn_to_glb_file(const std::filesystem::path& path, const GrnModel& model, const GlbExportOptions& options);

/**
 * @brief Exports a GrnModel to a standalone .GLB binary byte buffer in memory.
 * @param model Source GrnModel.
 * @param options Export configuration options.
 * @return Byte vector containing complete GLB binary file data.
 */
std::vector<uint8_t> export_grn_to_glb_memory(const GrnModel& model, const GlbExportOptions& options);

} // namespace grn
