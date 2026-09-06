/**
 * @file grn_parser.h
 * @brief Binary chunk parser and deserializer for GRN files.
 */

#pragma once

#include "grn_types.h"
#include <filesystem>
#include <optional>

namespace grn {

/**
 * @brief Parses a complete GRN file from disk into a GrnModel structure.
 * @param path Path to the .grn file on disk.
 * @return Populated GrnModel if file is valid, std::nullopt otherwise.
 */
std::optional<GrnModel> parse_grn_file(const std::filesystem::path& path);

/**
 * @brief Parses a complete GRN file from a memory buffer into a GrnModel structure.
 * @param data Pointer to the in-memory GRN byte buffer.
 * @param size Size of the buffer in bytes.
 * @return Populated GrnModel if buffer is valid, std::nullopt otherwise.
 */
std::optional<GrnModel> parse_grn_memory(const uint8_t* data, size_t size);

} // namespace grn
