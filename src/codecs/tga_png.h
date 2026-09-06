/**
 * @file tga_png.h
 * @brief Lossless image file loaders and savers for Truecolor 32-bit TGA and PNG.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace grn {

/**
 * @struct ImageData
 * @brief Container holding uncompressed 32-bit RGBA pixel buffers.
 */
struct ImageData {
    uint32_t width = 0;           /**< Image width in pixels. */
    uint32_t height = 0;          /**< Image height in pixels. */
    uint32_t channels = 4;        /**< Channel count (always 4 for 32-bit RGBA). */
    std::vector<uint8_t> pixels;  /**< Interleaved RGBA bytes. */
    bool has_alpha = false;       /**< True if image contains non-opaque alpha values. */
    bool is_placeholder = false;  /**< True if image contains fallback/neutral pixels rather than decoded art. */
};

/**
 * @brief Loads an image from disk (TGA, PNG, BMP, JPEG) into a 32-bit RGBA buffer.
 * @param path Path to image file on disk.
 * @return Populated ImageData if valid, std::nullopt otherwise.
 */
std::optional<ImageData> load_image_file(const std::filesystem::path& path);

/**
 * @brief Saves 32-bit RGBA data to an uncompressed Truecolor 32-bit TGA file.
 * @param path Destination path on disk.
 * @param rgba Pointer to 32-bit RGBA pixel buffer.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @param rle Optional run-length encoding (defaults to false for uncompressed).
 * @return True on success, false on failure.
 */
bool save_image_tga(const std::filesystem::path& path, const uint8_t* rgba, uint32_t width, uint32_t height, bool rle = false);

/**
 * @brief Saves 32-bit RGBA data to a PNG file.
 * @param path Destination path on disk.
 * @param rgba Pointer to 32-bit RGBA pixel buffer.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @return True on success, false on failure.
 */
bool save_image_png(const std::filesystem::path& path, const uint8_t* rgba, uint32_t width, uint32_t height);

/**
 * @brief Encodes 32-bit RGBA pixels into an in-memory PNG byte buffer.
 * @param rgba Pointer to 32-bit RGBA pixel buffer.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @return Encoded PNG binary bytes.
 */
std::vector<uint8_t> encode_png_memory(const uint8_t* rgba, uint32_t width, uint32_t height);

/**
 * @brief Decodes in-memory image bytes (PNG, TGA, BMP, JPEG) into 32-bit RGBA pixels.
 * @param data Pointer to raw image byte buffer.
 * @param size Size of the byte buffer.
 * @return Populated ImageData if valid, std::nullopt otherwise.
 */
std::optional<ImageData> decode_image_memory(const uint8_t* data, size_t size);

} // namespace grn
