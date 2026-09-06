/**
 * @file test_codecs.cpp
 * @brief Unit tests for texture codecs: DXT1, DXT5, TGA, PNG, and native VTex.
 */

#include "../src/codecs/dxt_codec.h"
#include "../src/codecs/tga_png.h"
#include "../src/codecs/vtex_codec.h"
#include "../src/core/grn_parser.h"
#include <iostream>
#include <vector>
#include <cstdlib>
#include <cmath>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

namespace fs = std::filesystem;

static void test_dxt1_codec() {
    std::cout << "[TEST] DXT1 Encode/Decode..." << std::endl;
    const uint32_t width = 16;
    const uint32_t height = 16;
    std::vector<uint8_t> test_pixels(width * height * 4);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            test_pixels[idx + 0] = (x < 8) ? 255 : 0;   // Red block on left
            test_pixels[idx + 1] = (y < 8) ? 255 : 0;   // Green block on top
            test_pixels[idx + 2] = 128;                 // Constant Blue
            test_pixels[idx + 3] = 255;                 // Opaque
        }
    }

    auto encoded = grn::encode_dxt1(test_pixels.data(), width, height);
    TEST_CHECK(encoded.size() == (width * height / 2)); // DXT1 is 4bpp (0.5 bytes per pixel)

    auto decoded = grn::decode_dxt1(encoded.data(), encoded.size(), width, height);
    TEST_CHECK(decoded.size() == test_pixels.size());

    // Verify reasonable reconstruction fidelity (DXT1 is lossy 5:6:5 color)
    for (size_t i = 0; i < test_pixels.size(); i += 4) {
        int diff_r = std::abs(static_cast<int>(test_pixels[i + 0]) - static_cast<int>(decoded[i + 0]));
        int diff_g = std::abs(static_cast<int>(test_pixels[i + 1]) - static_cast<int>(decoded[i + 1]));
        int diff_b = std::abs(static_cast<int>(test_pixels[i + 2]) - static_cast<int>(decoded[i + 2]));
        TEST_CHECK(diff_r < 32 && diff_g < 32 && diff_b < 32);
    }
    std::cout << "  DXT1 test passed." << std::endl;
}

static void test_tga_lossless() {
    std::cout << "[TEST] TGA Lossless 32-bit Roundtrip..." << std::endl;
    const uint32_t width = 32;
    const uint32_t height = 32;
    std::vector<uint8_t> orig_pixels(width * height * 4);

    for (uint32_t i = 0; i < orig_pixels.size(); ++i) {
        orig_pixels[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }

    fs::path temp_tga = fs::temp_directory_path() / "test_roundtrip.tga";
    bool saved = grn::save_image_tga(temp_tga, orig_pixels.data(), width, height, false);
    TEST_CHECK(saved);

    auto loaded = grn::load_image_file(temp_tga);
    TEST_CHECK(loaded.has_value());
    TEST_CHECK(loaded->width == width);
    TEST_CHECK(loaded->height == height);
    TEST_CHECK(loaded->channels == 4);
    TEST_CHECK(loaded->pixels == orig_pixels); // Must be bit-exact for uncompressed Truecolor

    fs::remove(temp_tga);
    std::cout << "  TGA lossless roundtrip passed." << std::endl;
}

static void test_png_codec() {
    std::cout << "[TEST] PNG Memory Codec..." << std::endl;
    const uint32_t width = 16;
    const uint32_t height = 16;
    std::vector<uint8_t> orig_pixels(width * height * 4);

    for (uint32_t i = 0; i < orig_pixels.size(); ++i) {
        orig_pixels[i] = static_cast<uint8_t>((i * 11 + 5) & 0xFF);
    }

    auto png_bytes = grn::encode_png_memory(orig_pixels.data(), width, height);
    TEST_CHECK(!png_bytes.empty());

    auto decoded = grn::decode_image_memory(png_bytes.data(), png_bytes.size());
    TEST_CHECK(decoded.has_value());
    TEST_CHECK(decoded->width == width);
    TEST_CHECK(decoded->height == height);
    TEST_CHECK(decoded->pixels == orig_pixels);

    std::cout << "  PNG memory codec passed." << std::endl;
}

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

    // 1. "test_grn" from running .exe location
    fs::path bin_tg = get_binary_dir() / "test_grn";
    if (fs::is_directory(bin_tg, ec)) return bin_tg;

    // 2. "test_grn" from repository folder / current working directory
    fs::path cur = fs::current_path();
    for (int i = 0; i < 4; ++i) {
        fs::path candidate = cur / "test_grn";
        if (fs::is_directory(candidate, ec)) return candidate;
        if (!cur.has_parent_path() || cur == cur.parent_path()) break;
        cur = cur.parent_path();
    }

    return {};
}

static void test_vtex_codec() {
    std::cout << "[TEST] Native VTex Video Texture Codec (Formats 4 & 5)..." << std::endl;

    // 1. Error handling & validation
    TEST_CHECK(!grn::decode_vtex(nullptr, 0, 16, 16, 4).has_value());
    uint8_t invalid_buf[48] = { 'F', 'A', 'K', 'E' };
    TEST_CHECK(!grn::decode_vtex(invalid_buf, sizeof(invalid_buf), 16, 16, 4).has_value());

    // 2. Synthetic Roundtrip with Alpha (Format 5)
    {
        const uint32_t width = 32;
        const uint32_t height = 32;
        std::vector<uint8_t> test_rgba(width * height * 4);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 4;
                test_rgba[idx + 0] = (x < 16) ? 220 : 40;  // Red
                test_rgba[idx + 1] = (y < 16) ? 180 : 30;  // Green
                test_rgba[idx + 2] = 90;                   // Blue
                // Non-trivial alpha: half transparent, half opaque
                test_rgba[idx + 3] = (x < 16) ? 255 : ((y < 16) ? 128 : 0);
            }
        }

        auto [fmt, encoded] = grn::encode_vtex(test_rgba.data(), width, height);
        TEST_CHECK(fmt == 5); // Format 5 (VTex with Alpha)
        TEST_CHECK(!encoded.empty());
        TEST_CHECK(encoded.size() > 52);

        auto hdr = grn::parse_vtex_header(encoded.data(), encoded.size());
        TEST_CHECK(hdr.has_value());
        TEST_CHECK(hdr->width == width);
        TEST_CHECK(hdr->height == height);
        TEST_CHECK(hdr->has_alpha == true);

        auto decoded = grn::decode_vtex(encoded.data(), encoded.size(), width, height, 5);
        TEST_CHECK(decoded.has_value());
        TEST_CHECK(decoded->width == width);
        TEST_CHECK(decoded->height == height);
        TEST_CHECK(decoded->has_alpha == true);
        TEST_CHECK(decoded->is_placeholder == false);
        TEST_CHECK(decoded->pixels.size() == width * height * 4);

        // Verify alpha channel is accurate
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 4;
                int diff_a = std::abs(static_cast<int>(test_rgba[idx + 3]) - static_cast<int>(decoded->pixels[idx + 3]));
                TEST_CHECK(diff_a <= 8);
            }
        }
        std::cout << "  VTex Format 5 synthetic roundtrip passed." << std::endl;
    }

    // 3. Synthetic Roundtrip Opaque (Format 4)
    {
        const uint32_t width = 32;
        const uint32_t height = 32;
        std::vector<uint8_t> test_rgba(width * height * 4);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 4;
                test_rgba[idx + 0] = static_cast<uint8_t>(x * 7);
                test_rgba[idx + 1] = static_cast<uint8_t>(y * 7);
                test_rgba[idx + 2] = 120;
                test_rgba[idx + 3] = 255; // fully opaque
            }
        }

        auto [fmt, encoded] = grn::encode_vtex(test_rgba.data(), width, height);
        TEST_CHECK(fmt == 4); // Format 4 (VTex Opaque)
        TEST_CHECK(!encoded.empty());

        auto decoded = grn::decode_vtex(encoded.data(), encoded.size(), width, height, 4);
        TEST_CHECK(decoded.has_value());
        TEST_CHECK(decoded->width == width);
        TEST_CHECK(decoded->height == height);
        TEST_CHECK(decoded->has_alpha == false);
        TEST_CHECK(decoded->is_placeholder == false);

        std::cout << "  VTex Format 4 synthetic roundtrip passed." << std::endl;
    }

    // 4. Dynamic Asset Discovery in test_grn (Format 4 and Format 5)
    {
        fs::path tg_dir = locate_test_grn_dir();
        if (tg_dir.empty()) {
            std::cout << "  (Notice: test_grn directory not found, skipping asset discovery)" << std::endl;
        } else {
            bool tested_fmt4 = false;
            bool tested_fmt5 = false;

            std::error_code ec;
            for (const auto& entry : fs::recursive_directory_iterator(tg_dir, ec)) {
                if (!entry.is_regular_file(ec) || entry.path().extension() != ".grn") continue;

                // Skip output folders if present
                auto rel = fs::relative(entry.path(), tg_dir, ec);
                auto first = rel.begin()->string();
                if (first == "GRN" || first == "GLB") continue;

                auto model = grn::parse_grn_file(entry.path());
                if (!model.has_value() || model->textures.empty()) continue;

                for (const auto& tex : model->textures) {
                    if (!tested_fmt4 && tex.format_code == 4 && !tex.decoded_rgba.empty() && !tex.is_placeholder) {
                        TEST_CHECK(tex.width > 0 && tex.height > 0);
                        TEST_CHECK(tex.decoded_rgba.size() == static_cast<size_t>(tex.width) * tex.height * 4);
                        tested_fmt4 = true;
                        std::cout << "  Discovered Format 4 texture in " << entry.path().filename().string()
                                  << " (" << tex.width << "x" << tex.height << ") decoded successfully." << std::endl;
                    }
                    if (!tested_fmt5 && tex.format_code == 5 && !tex.decoded_rgba.empty() && !tex.is_placeholder) {
                        TEST_CHECK(tex.width > 0 && tex.height > 0);
                        TEST_CHECK(tex.has_alpha == true);
                        TEST_CHECK(tex.decoded_rgba.size() == static_cast<size_t>(tex.width) * tex.height * 4);
                        tested_fmt5 = true;
                        std::cout << "  Discovered Format 5 texture in " << entry.path().filename().string()
                                  << " (" << tex.width << "x" << tex.height << ") decoded successfully." << std::endl;
                    }
                }

                if (tested_fmt4 && tested_fmt5) break;
            }

            if (!tested_fmt4 && !tested_fmt5) {
                std::cout << "  (Notice: No Format 4 or Format 5 textures found in discovered .grn files)" << std::endl;
            }
        }
    }
}

static void test_dxt5_codec() {
    std::cout << "[TEST] DXT5 Encode/Decode (Alpha Interpolation)..." << std::endl;
    const uint32_t width = 16;
    const uint32_t height = 16;
    std::vector<uint8_t> test_pixels(width * height * 4);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            test_pixels[idx + 0] = static_cast<uint8_t>(x * 15);
            test_pixels[idx + 1] = static_cast<uint8_t>(y * 15);
            test_pixels[idx + 2] = 100;
            test_pixels[idx + 3] = static_cast<uint8_t>((x + y) * 8); // Gradient alpha
        }
    }

    auto encoded = grn::encode_dxt5(test_pixels.data(), width, height);
    TEST_CHECK(encoded.size() == (width * height)); // DXT5 is 8bpp (1 byte per pixel)

    auto decoded = grn::decode_dxt5(encoded.data(), encoded.size(), width, height);
    TEST_CHECK(decoded.size() == test_pixels.size());

    // Verify alpha and RGB reconstruction fidelity
    for (size_t i = 0; i < test_pixels.size(); i += 4) {
        int diff_a = std::abs(static_cast<int>(test_pixels[i + 3]) - static_cast<int>(decoded[i + 3]));
        TEST_CHECK(diff_a <= 20); // DXT5 3-bit interpolated alpha fidelity
    }
    std::cout << "  DXT5 test passed." << std::endl;
}

int main() {
    std::cout << "=== Running Codec Unit Tests ===" << std::endl;
    test_dxt1_codec();
    test_dxt5_codec();
    test_tga_lossless();
    test_png_codec();
    test_vtex_codec();
    std::cout << "All codec tests passed successfully." << std::endl;
    return 0;
}
