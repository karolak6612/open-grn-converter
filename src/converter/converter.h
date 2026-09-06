/**
 * @file converter.h
 * @brief High-level conversion pipeline orchestrator for single files and directory batches.
 */

#pragma once

#include "options.h"
#include <filesystem>
#include <vector>

namespace grn {

/**
 * @brief Summary statistics returned after completing a conversion job.
 */
struct ConversionResult {
    size_t files_attempted = 0;
    size_t files_succeeded = 0;
    size_t files_failed = 0;
    std::vector<std::string> log_messages;
};

/**
 * @brief Converts a single file between GRN and GLB according to options.
 * @param input Path to source file.
 * @param output Destination path (auto-derived if empty).
 * @param options Conversion configuration settings.
 * @param callback Optional progress and logging callback.
 * @return True if conversion succeeded, false otherwise.
 */
bool convert_file(const std::filesystem::path& input,
                  const std::filesystem::path& output,
                  const ConversionOptions& options,
                  ProgressCallback callback = nullptr);

/**
 * @brief Recursively converts all compatible files in a directory batch.
 * @param input_dir Source folder to scan for models.
 * @param output_dir Destination folder for converted files.
 * @param options Conversion configuration settings.
 * @param callback Optional progress and logging callback.
 * @return ConversionResult containing summary counts and diagnostics.
 */
ConversionResult convert_directory(const std::filesystem::path& input_dir,
                                   const std::filesystem::path& output_dir,
                                   const ConversionOptions& options,
                                   ProgressCallback callback = nullptr);

} // namespace grn
