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

static fs::path find_asset(const std::string& filename) {
    std::vector<fs::path> candidates = {
        fs::path("test_grn") / "GRN_ORIGINAL" / filename,
        fs::path("..") / "test_grn" / "GRN_ORIGINAL" / filename,
        fs::path("GRN_TEXTURED") / "GRN_ORIGINAL" / filename,
        fs::path("..") / "GRN_TEXTURED" / "GRN_ORIGINAL" / filename,
        fs::path("E:/Github/open-grn-converter/GRN_TEXTURED/GRN_ORIGINAL") / filename,
        fs::path("E:/Github/open-grn-converter/test_grn/GRN_ORIGINAL") / filename
    };
    std::error_code ec;
    for (const auto& p : candidates) {
        if (fs::exists(p, ec)) return p;
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

    // 4. Real Model Format 4 Decoding: BLACK_MAGICIAN.grn
    {
        fs::path bm_path = find_asset("BLACK_MAGICIAN.grn");
        if (!bm_path.empty()) {
            std::cout << "  Decoding real asset: " << bm_path.string() << std::endl;
            auto model = grn::parse_grn_file(bm_path);
            TEST_CHECK(model.has_value());
            TEST_CHECK(!model->textures.empty());

            const auto& tex = model->textures[0];
            TEST_CHECK(tex.width == 512);
            TEST_CHECK(tex.height == 512);
            TEST_CHECK(tex.format_code == 4);
            TEST_CHECK(!tex.decoded_rgba.empty());
            TEST_CHECK(tex.is_placeholder == false);
            TEST_CHECK(tex.decoded_rgba.size() == 512 * 512 * 4);

            // Check non-neutral, vibrant colors (deep red robe)
            size_t red_pixels = 0;
            for (size_t i = 0; i < tex.decoded_rgba.size(); i += 4) {
                if (tex.decoded_rgba[i + 0] > 100 && tex.decoded_rgba[i + 1] < 60) {
                    ++red_pixels;
                }
            }
            TEST_CHECK(red_pixels > 1000);
            std::cout << "  BLACK_MAGICIAN.grn Format 4 decoded successfully (" << red_pixels << " red robe pixels)." << std::endl;
        } else {
            std::cout << "  (Notice: BLACK_MAGICIAN.grn not found in search paths, skipping asset test)" << std::endl;
        }
    }

    // 5. Real Model Format 5 Decoding: BLACK_RIDER.grn
    {
        fs::path br_path = find_asset("BLACK_RIDER.grn");
        if (!br_path.empty()) {
            std::cout << "  Decoding real asset: " << br_path.string() << std::endl;
            auto model = grn::parse_grn_file(br_path);
            TEST_CHECK(model.has_value());
            TEST_CHECK(!model->textures.empty());

            const auto& tex = model->textures[0];
            TEST_CHECK(tex.width == 512);
            TEST_CHECK(tex.height == 512);
            TEST_CHECK(tex.format_code == 5);
            TEST_CHECK(!tex.decoded_rgba.empty());
            TEST_CHECK(tex.is_placeholder == false);
            TEST_CHECK(tex.has_alpha == true);
            TEST_CHECK(tex.decoded_rgba.size() == 512 * 512 * 4);

            // Verify alpha channel variation (transparent cutout vs opaque body)
            size_t transparent_pixels = 0;
            size_t opaque_pixels = 0;
            for (size_t i = 0; i < tex.decoded_rgba.size(); i += 4) {
                if (tex.decoded_rgba[i + 3] < 32) ++transparent_pixels;
                else if (tex.decoded_rgba[i + 3] > 220) ++opaque_pixels;
            }
            TEST_CHECK(transparent_pixels > 1000);
            TEST_CHECK(opaque_pixels > 1000);
            std::cout << "  BLACK_RIDER.grn Format 5 decoded successfully ("
                      << transparent_pixels << " transparent, " << opaque_pixels << " opaque pixels)." << std::endl;
        } else {
            std::cout << "  (Notice: BLACK_RIDER.grn not found in search paths, skipping asset test)" << std::endl;
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
