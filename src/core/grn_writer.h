/**
 * @file grn_writer.h
 * @brief Binary chunk serializer for constructing valid GRN format files.
 */

#pragma once

#include "grn_types.h"
#include <filesystem>
#include <vector>

namespace grn {

/**
 * @brief Serializes a GrnModel into a complete binary GRN file on disk.
 * @param path Destination path for the .grn file.
 * @param model Model data to write.
 * @return True on success, false on failure.
 */
bool write_grn_file(const std::filesystem::path& path, const GrnModel& model);

/**
 * @brief Serializes a GrnModel into a binary memory buffer.
 * @param model Model data to write.
 * @return Vector containing binary GRN file bytes.
 */
std::vector<uint8_t> write_grn_memory(const GrnModel& model);

} // namespace grn
