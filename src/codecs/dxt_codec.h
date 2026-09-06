/**
 * @file dxt_codec.h
 * @brief Block-compression (DXT1 / BC1) encoder and decoder utilities.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace grn {

/**
 * @brief Decodes a DXT1 (BC1) stream into 32-bit RGBA pixels.
 * @param data Pointer to raw DXT1 byte stream.
 * @param size Size of the DXT1 buffer in bytes.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @return Decoded 32-bit RGBA pixel array (width * height * 4 bytes).
 */
std::vector<uint8_t> decode_dxt1(const uint8_t* data, size_t size, uint32_t width, uint32_t height);

/**
 * @brief Encodes 32-bit RGBA pixels into a DXT1 (BC1) byte stream.
 * @param rgba Pointer to 32-bit RGBA image pixels.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @return Encoded DXT1 (BC1) compressed byte stream.
 */
std::vector<uint8_t> encode_dxt1(const uint8_t* rgba, uint32_t width, uint32_t height);

/**
 * @brief Decodes a DXT5 (BC3) stream into 32-bit RGBA pixels.
 * @param data Pointer to raw DXT5 byte stream.
 * @param size Size of the DXT5 buffer in bytes.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @return Decoded 32-bit RGBA pixel array (width * height * 4 bytes).
 */
std::vector<uint8_t> decode_dxt5(const uint8_t* data, size_t size, uint32_t width, uint32_t height);

/**
 * @brief Encodes 32-bit RGBA pixels into a DXT5 (BC3) byte stream.
 * @param rgba Pointer to 32-bit RGBA image pixels.
 * @param width Width of the image in pixels.
 * @param height Height of the image in pixels.
 * @return Encoded DXT5 (BC3) compressed byte stream.
 */
std::vector<uint8_t> encode_dxt5(const uint8_t* rgba, uint32_t width, uint32_t height);

} // namespace grn
