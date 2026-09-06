/**
 * @file test_codecs.cpp
 * @brief Unit tests for texture codecs: DXT1, TGA, PNG, and VTex.
 */

#include "../src/codecs/dxt_codec.h"
#include "../src/codecs/tga_png.h"
#include "../src/codecs/vtex_codec.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <filesystem>

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
    assert(encoded.size() == (width * height / 2)); // DXT1 is 4bpp (0.5 bytes per pixel)

    auto decoded = grn::decode_dxt1(encoded.data(), encoded.size(), width, height);
    assert(decoded.size() == test_pixels.size());

    // Verify reasonable reconstruction fidelity (DXT1 is lossy 5:6:5 color)
    for (size_t i = 0; i < test_pixels.size(); i += 4) {
        int diff_r = std::abs(static_cast<int>(test_pixels[i + 0]) - static_cast<int>(decoded[i + 0]));
        int diff_g = std::abs(static_cast<int>(test_pixels[i + 1]) - static_cast<int>(decoded[i + 1]));
        int diff_b = std::abs(static_cast<int>(test_pixels[i + 2]) - static_cast<int>(decoded[i + 2]));
        assert(diff_r < 32 && diff_g < 32 && diff_b < 32);
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
    assert(saved);

    auto loaded = grn::load_image_file(temp_tga);
    assert(loaded.has_value());
    assert(loaded->width == width);
    assert(loaded->height == height);
    assert(loaded->channels == 4);
    assert(loaded->pixels == orig_pixels); // Must be bit-exact for uncompressed Truecolor

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
    assert(!png_bytes.empty());

    auto decoded = grn::decode_image_memory(png_bytes.data(), png_bytes.size());
    assert(decoded.has_value());
    assert(decoded->width == width);
    assert(decoded->height == height);
    assert(decoded->pixels == orig_pixels);

    std::cout << "  PNG memory codec passed." << std::endl;
}

static void test_vtex_codec() {
    std::cout << "[TEST] VTex Video Texture Codec..." << std::endl;
    const uint32_t width = 16;
    const uint32_t height = 16;
    std::vector<uint8_t> test_pixels(width * height * 4);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 4;
            test_pixels[idx + 0] = 200;
            test_pixels[idx + 1] = 100;
            test_pixels[idx + 2] = 50;
            test_pixels[idx + 3] = 255;
        }
    }

    // 1. Re-encoding is disabled / read-only: callers fall back to standard DXT/RGBA
    auto [fmt, encoded] = grn::encode_vtex(test_pixels.data(), width, height);
    assert(encoded.empty());
    assert(fmt == 0);

    // 2. Decoding a valid BIKi stream header yields a neutral placeholder fallback (never crashes)
    std::vector<uint8_t> vtex_hdr(44, 0);
    std::memcpy(vtex_hdr.data(), "BIKi", 4);
    vtex_hdr[4] = 44; // file_size
    vtex_hdr[8] = 1;  // frame_count
    vtex_hdr[28] = static_cast<uint8_t>(width);
    vtex_hdr[32] = static_cast<uint8_t>(height);

    auto decoded = grn::decode_vtex(vtex_hdr.data(), vtex_hdr.size(), width, height, 4);
    assert(decoded.has_value());
    assert(decoded->width == width);
    assert(decoded->height == height);
    assert(decoded->is_placeholder == true);
    assert(decoded->pixels.size() == width * height * 4);
    assert(decoded->pixels[0] == 255); // Neutral white/fallback pixel

    // 3. Corrupted or undersized streams safely fail
    assert(!grn::decode_vtex(vtex_hdr.data(), 40, width, height, 4).has_value());

    std::cout << "  VTex codec test passed." << std::endl;
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
    assert(encoded.size() == (width * height)); // DXT5 is 8bpp (1 byte per pixel)

    auto decoded = grn::decode_dxt5(encoded.data(), encoded.size(), width, height);
    assert(decoded.size() == test_pixels.size());

    // Verify alpha and RGB reconstruction fidelity
    for (size_t i = 0; i < test_pixels.size(); i += 4) {
        int diff_a = std::abs(static_cast<int>(test_pixels[i + 3]) - static_cast<int>(decoded[i + 3]));
        assert(diff_a <= 20); // DXT5 3-bit interpolated alpha fidelity
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

