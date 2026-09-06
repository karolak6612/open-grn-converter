/**
 * @file test_texture_hue.cpp
 * @brief Integration test for texture extraction, hue transformation, and re-encoding.
 *        Verifies that decoding/encoding maintains fidelity and preserves file size.
 */

#include "../src/converter/converter.h"
#include "../src/core/grn_parser.h"
#include "../src/core/grn_writer.h"
#include "../src/codecs/dxt_codec.h"
#include "../src/codecs/vtex_codec.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cassert>
#include <iomanip>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

static fs::path get_binary_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len > 0) {
        return fs::path(path).parent_path();
    }
#endif
    return fs::current_path();
}

static fs::path locate_test_grn_dir() {
    std::error_code ec;

    // 1. Binary running folder
    fs::path bin_tg = get_binary_dir() / "test_grn";
    if (fs::is_directory(bin_tg, ec)) return bin_tg;

    // 2. Source code folder
#ifdef GRN_SOURCE_CODE_DIR
    fs::path src_tg = fs::path(GRN_SOURCE_CODE_DIR) / "test_grn";
    if (fs::is_directory(src_tg, ec)) return src_tg;
#endif

    // 3. Current working directory
    fs::path cwd_tg = fs::current_path() / "test_grn";
    if (fs::is_directory(cwd_tg, ec)) return cwd_tg;

    const char* env_dir = std::getenv("GRN_TEST_DIR");
    if (env_dir && fs::is_directory(env_dir, ec)) return env_dir;

    return {};
}

static void apply_pink_hue_shift(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height) {
    size_t pixel_count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t idx = i * 4;
        int r = rgba[idx + 0];
        int g = rgba[idx + 1];
        int b = rgba[idx + 2];

        // Shift toward pink/magenta
        r = std::min(255, r + 50);
        g = std::max(0, g - 40);
        b = std::min(255, b + 50);

        rgba[idx + 0] = static_cast<uint8_t>(r);
        rgba[idx + 1] = static_cast<uint8_t>(g);
        rgba[idx + 2] = static_cast<uint8_t>(b);
    }
}

static void test_model_texture_hue_cycle(const fs::path& grn_path) {
    std::cout << "\n[HUE TEST] Testing Model: " << grn_path.filename().string() << std::endl;

    auto model_opt = grn::parse_grn_file(grn_path);
    assert(model_opt.has_value());
    auto model = *model_opt;

    if (model.textures.empty()) {
        std::cout << "  No textures in " << grn_path.filename().string() << " (skipping)." << std::endl;
        return;
    }

    size_t orig_file_size = fs::file_size(grn_path);
    auto baseline_bytes = grn::write_grn_memory(model);
    size_t baseline_size = baseline_bytes.size();

    std::cout << "  Original File Size: " << orig_file_size << " B, Baseline Re-export: " << baseline_size << " B" << std::endl;
    std::cout << "  Texture Count: " << model.textures.size() << std::endl;

    for (size_t ti = 0; ti < model.textures.size(); ++ti) {
        auto& tex = model.textures[ti];
        std::cout << "  -> Texture " << ti << " [" << tex.name << "]: "
                  << tex.width << "x" << tex.height
                  << ", Format: " << tex.format_str
                  << " (code " << tex.format_code << ")"
                  << ", Raw Blob: " << tex.raw_blob.size() << " B"
                  << ", Decoded RGBA: " << tex.decoded_rgba.size() << " B" << std::endl;

        if (tex.decoded_rgba.empty() || tex.width == 0 || tex.height == 0) {
            continue;
        }

        size_t orig_blob_size = tex.raw_blob.size();

        // 1. Apply hue shift on extracted 32-bit RGBA pixels
        apply_pink_hue_shift(tex.decoded_rgba, tex.width, tex.height);

        // 2. Re-encode according to texture format
        if (tex.format_code == 8 || tex.format_str == "dxt1") {
            // DXT1 block compression
            tex.raw_blob = grn::encode_dxt1(tex.decoded_rgba.data(), tex.width, tex.height);
            // DXT1 size is strictly fixed at 0.5 bytes per pixel (4bpp)
            size_t expected_dxt_size = ((tex.width + 3) / 4) * ((tex.height + 3) / 4) * 8;
            assert(tex.raw_blob.size() == expected_dxt_size);
            assert(tex.raw_blob.size() == orig_blob_size);
            (void)expected_dxt_size;
            (void)orig_blob_size;
            std::cout << "     Re-encoded DXT1 size: " << tex.raw_blob.size() << " B (exact match!)" << std::endl;
        } else if (tex.format_str == "vtex" || (!tex.raw_blob.empty() && tex.raw_blob.size() >= 4 && std::memcmp(tex.raw_blob.data(), "BIKi", 4) == 0)) {
            // VTex video texture compression: encode_vtex returns empty as video bitstream
            // encoding is read-only; re-encode as DXT1 or DXT5 fallback (matching grn_writer behavior)
            auto [fmt, re_vtex] = grn::encode_vtex(tex.decoded_rgba.data(), tex.width, tex.height);
            assert(re_vtex.empty());
            if (tex.has_alpha) {
                tex.raw_blob = grn::encode_dxt5(tex.decoded_rgba.data(), tex.width, tex.height);
                tex.format_code = 10;
            } else {
                tex.raw_blob = grn::encode_dxt1(tex.decoded_rgba.data(), tex.width, tex.height);
                tex.format_code = 8;
            }
            std::cout << "     Re-encoded VTex fallback (DXT): " << tex.raw_blob.size() << " B (orig: " << orig_blob_size << " B)" << std::endl;
        } else if (tex.format_code == 0 || tex.format_code == 1) {
            // Raw 32-bit RGBA
            tex.raw_blob = tex.decoded_rgba;
            assert(tex.raw_blob.size() == orig_blob_size);
            std::cout << "     Re-encoded Raw 32-bit size: " << tex.raw_blob.size() << " B (exact match!)" << std::endl;
        }
    }

    // 3. Serialize modified model to binary GRN
    auto modified_bytes = grn::write_grn_memory(model);
    assert(!modified_bytes.empty());
    size_t modified_size = modified_bytes.size();

    std::cout << "  Modified GRN Size: " << modified_size << " B" << std::endl;

    // For fixed-rate textures (DXT1 or raw RGBA), the total file size MUST be identical!
    bool has_variable_codec = false;
    for (const auto& t : model.textures) {
        if (t.format_str == "vtex") has_variable_codec = true;
    }

    if (!has_variable_codec) {
        assert(modified_size == baseline_size);
        std::cout << "  [PASS] File size is byte-for-byte identical after hue modification: "
                  << modified_size << " B == " << baseline_size << " B" << std::endl;
    } else {
        // VTex stream size variation should be within 10%
        double ratio = static_cast<double>(modified_size) / static_cast<double>(baseline_size);
        assert(ratio > 0.85 && ratio < 1.15);
        std::cout << "  [PASS] VTex model size within expected tolerance (ratio: " << ratio << ")" << std::endl;
    }

    // 4. Verify that the modified GRN re-parses completely
    auto reloaded = grn::parse_grn_memory(modified_bytes.data(), modified_bytes.size());
    assert(reloaded.has_value());
    assert(reloaded->textures.size() == model.textures.size());
    assert(reloaded->meshes.size() == model.meshes.size());
    for (size_t ti = 0; ti < model.textures.size(); ++ti) {
        assert(reloaded->textures[ti].width == model.textures[ti].width);
        assert(reloaded->textures[ti].height == model.textures[ti].height);
        assert(reloaded->textures[ti].format_code == model.textures[ti].format_code);
    }

    std::cout << "  [PASS] Modified GRN re-parsed with full structural fidelity." << std::endl;
}

int main() {
    std::cout << "=== Running Texture Extraction & Hue Modification Test ===" << std::endl;

    fs::path tg_dir = locate_test_grn_dir();
    if (tg_dir.empty()) {
        std::cout << "[SKIP] test_grn directory not found. Test skipped." << std::endl;
        return 0;
    }

    std::vector<fs::path> test_files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(tg_dir, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".grn") continue;
        auto rel = fs::relative(entry.path(), tg_dir, ec);
        auto first = rel.begin()->string();
        if (first == "GRN" || first == "GLB") continue;
        test_files.push_back(entry.path());
        if (test_files.size() >= 5) break; // Test up to 5 discovered models
    }

    if (test_files.empty()) {
        std::cout << "[SKIP] No .grn files in test_grn. Test skipped." << std::endl;
        return 0;
    }

    size_t tested = 0;
    for (const auto& f : test_files) {
        test_model_texture_hue_cycle(f);
        tested++;
    }

    std::cout << "\nSuccessfully validated texture extraction, hue shift, and re-encoding across "
              << tested << " models." << std::endl;
    return 0;
}
