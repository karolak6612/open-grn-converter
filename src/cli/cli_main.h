/**
 * @file cli_main.h
 * @brief Command-line interface entry point for headless execution.
 */

#pragma once

#include <vector>
#include <string>

namespace grn {

/**
 * @brief Executes the headless CLI mode.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return 0 on success, non-zero on failure.
 */
int run_cli(int argc, char* argv[]);

} // namespace grn
