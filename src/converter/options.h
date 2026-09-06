/**
 * @file options.h
 * @brief Configuration settings and options for the GRN <-> GLB converter pipeline.
 */

#pragma once

#include <string>
#include <filesystem>
#include <functional>

namespace grn {

/**
 * @brief Direction of format conversion.
 */
enum class ConversionDirection {
    Auto,      /**< Automatically determined by input file extension. */
    GrnToGlb,  /**< Convert from GRN container to glTF 2.0 / GLB. */
    GlbToGrn   /**< Convert from glTF 2.0 / GLB to GRN container. */
};

/**
 * @brief High-level settings governing file and batch conversion behavior.
 */
struct ConversionOptions {
    ConversionDirection direction = ConversionDirection::Auto; /**< Conversion direction. */
    bool embed_textures = true;       /**< Whether to embed texture streams directly inside the binary. */
    std::string texture_format = "tga"; /**< Target format for standalone textures: "tga", "png", or "vtex". */
    bool vtex_enabled = true;         /**< Enable VTex video texture encoding/decoding. */
    bool tint_pink = false;           /**< Test feature: applies pink tint filter to diffuse textures. */
    bool y_up = true;                 /**< Convert coordinate systems between Z-up (GRN) and Y-up (glTF). */
    float scale = 1.0f;               /**< Uniform scale factor applied during conversion. */
    float target_height = 0.0f;       /**< Optional target height in game units (auto-calculates scale). */
    std::filesystem::path anim_file;  /**< Optional path to external animation track to combine with model. */
    std::filesystem::path textures_dir; /**< Optional search directory for loose or pre-extracted textures. */
    std::filesystem::path output_path;/**< Optional explicit output path or directory. */
};

/**
 * @brief Progress report callback for batch and single-file operations.
 * @param current_item Name or relative path of the item currently being processed.
 * @param progress Value from 0.0 to 1.0 indicating completion percentage.
 * @param success True if the current step completed without errors.
 * @param message Diagnostic or informational log message.
 */
using ProgressCallback = std::function<void(const std::string& current_item, float progress, bool success, const std::string& message)>;

} // namespace grn
