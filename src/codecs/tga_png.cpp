#include "tga_png.h"
#include <fstream>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace grn {

static bool check_has_alpha(const uint8_t* rgba, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (rgba[i * 4 + 3] < 250) {
            return true;
        }
    }
    return false;
}

std::optional<ImageData> load_image_file(const std::filesystem::path& path) {
    int w = 0, h = 0, channels = 0;
    std::string path_str = path.string();
    uint8_t* data = stbi_load(path_str.c_str(), &w, &h, &channels, 4);
    if (!data) {
        return std::nullopt;
    }

    ImageData img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.channels = 4;
    img.pixels.assign(data, data + (w * h * 4));
    img.has_alpha = check_has_alpha(img.pixels.data(), w * h);

    stbi_image_free(data);
    return img;
}

bool save_image_tga(const std::filesystem::path& path, const uint8_t* rgba, uint32_t width, uint32_t height, bool rle) {
    std::string path_str = path.string();
    if (!rle) {
        // Standard uncompressed 32-bit truecolor TGA header (18 bytes)
        // Header format:
        // [0] id length = 0
        // [1] colormap type = 0
        // [2] image type = 2 (uncompressed truecolor)
        // [3..7] colormap spec = 0
        // [8..11] x, y origin = 0
        // [12..13] width (LE)
        // [14..15] height (LE)
        // [16] pixel depth = 32
        // [17] image descriptor = 8 (8 bits alpha, top-to-bottom: 0x28, bottom-to-top: 0x08)
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        uint8_t header[18] = {};
        header[2] = 2; // Uncompressed truecolor
        header[12] = static_cast<uint8_t>(width & 0xFF);
        header[13] = static_cast<uint8_t>((width >> 8) & 0xFF);
        header[14] = static_cast<uint8_t>(height & 0xFF);
        header[15] = static_cast<uint8_t>((height >> 8) & 0xFF);
        header[16] = 32; // 32 bpp
        header[17] = 0x28; // Top-left origin, 8 bits alpha

        file.write(reinterpret_cast<const char*>(header), sizeof(header));

        // TGA truecolor order is BGRA
        std::vector<uint8_t> bgra(width * height * 4);
        for (size_t i = 0; i < width * height; ++i) {
            bgra[i * 4 + 0] = rgba[i * 4 + 2]; // B
            bgra[i * 4 + 1] = rgba[i * 4 + 1]; // G
            bgra[i * 4 + 2] = rgba[i * 4 + 0]; // R
            bgra[i * 4 + 3] = rgba[i * 4 + 3]; // A
        }
        file.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
        return true;
    } else {
        return stbi_write_tga(path_str.c_str(), width, height, 4, rgba) != 0;
    }
}

bool save_image_png(const std::filesystem::path& path, const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::string path_str = path.string();
    return stbi_write_png(path_str.c_str(), width, height, 4, rgba, width * 4) != 0;
}

static void stbi_write_func(void* context, void* data, int size) {
    auto* vec = reinterpret_cast<std::vector<uint8_t>*>(context);
    const auto* byte_data = reinterpret_cast<const uint8_t*>(data);
    vec->insert(vec->end(), byte_data, byte_data + size);
}

std::vector<uint8_t> encode_png_memory(const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func(stbi_write_func, &out, width, height, 4, rgba, width * 4);
    return out;
}

std::optional<ImageData> decode_image_memory(const uint8_t* data, size_t size) {
    int w = 0, h = 0, channels = 0;
    uint8_t* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (!decoded) {
        return std::nullopt;
    }

    ImageData img;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.channels = 4;
    img.pixels.assign(decoded, decoded + (w * h * 4));
    img.has_alpha = check_has_alpha(img.pixels.data(), w * h);

    stbi_image_free(decoded);
    return img;
}

} // namespace grn
