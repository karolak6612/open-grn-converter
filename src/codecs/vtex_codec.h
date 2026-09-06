/**
 * @file vtex_codec.h
 * @brief Compressed video texture stream (VTex format 4/5) parser and codecs.
 */

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include "tga_png.h"

namespace grn {

/**
 * @struct VTexHeader
 * @brief Metadata header for VTex video texture bitstreams.
 */
struct VTexHeader {
    char magic[4] = {};             /**< 4-byte stream identifier signature. */
    uint32_t file_size = 0;         /**< Total stream file size in bytes. */
    uint32_t frame_count = 0;       /**< Total number of encoded video frames. */
    uint32_t max_frame_size = 0;    /**< Maximum frame buffer size in bytes. */
    uint32_t frame_rate_num = 0;    /**< Framerate numerator. */
    uint32_t frame_rate_den = 0;    /**< Framerate denominator. */
    uint32_t flags = 0;             /**< Codec flags. */
    uint32_t width = 0;             /**< Frame width in pixels. */
    uint32_t height = 0;            /**< Frame height in pixels. */
    uint32_t audio_tracks = 0;      /**< Audio track count. */
    bool has_alpha = false;         /**< Whether alpha plane is present. */
};

/**
 * @brief Inspects and parses a VTex video texture stream header.
 * @param data Pointer to stream buffer.
 * @param size Size of the stream buffer in bytes.
 * @return Parsed VTexHeader if valid, std::nullopt otherwise.
 */
std::optional<VTexHeader> parse_vtex_header(const uint8_t* data, size_t size);

/**
 * @brief Decompresses a VTex video texture stream (Format 4 opaque, Format 5 alpha) into 32-bit RGBA.
 * @param data Pointer to video stream buffer.
 * @param size Size of video stream buffer in bytes.
 * @param width Width of image.
 * @param height Height of image.
 * @param format_code Format code (4 for opaque DXT1, 5 for DXT1 with alpha plane).
 * @return Decoded ImageData with 32-bit RGBA pixels.
 */
std::optional<ImageData> decode_vtex(const uint8_t* data, size_t size, uint32_t width, uint32_t height, uint32_t format_code);

/**
 * @brief Compresses 32-bit RGBA pixels into a native VTex video texture bitstream.
 * @param rgba Pointer to 32-bit RGBA pixel buffer.
 * @param width Width of image in pixels.
 * @param height Height of image in pixels.
 * @return Pair of format code (4=opaque, 5=alpha) and compressed stream bytes.
 */
std::pair<uint32_t, std::vector<uint8_t>> encode_vtex(const uint8_t* rgba, uint32_t width, uint32_t height);

} // namespace grn
