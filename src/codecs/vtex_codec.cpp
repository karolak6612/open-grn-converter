#include "vtex_codec.h"
#include <cstring>
#include <algorithm>

namespace grn {

static inline uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static inline void writeLE32(uint8_t* p, uint32_t val) {
    p[0] = static_cast<uint8_t>(val & 0xFF);
    p[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

std::optional<VTexHeader> parse_vtex_header(const uint8_t* data, size_t size) {
    if (!data || size < 44) return std::nullopt;

    if (std::memcmp(data, "BIKi", 4) != 0 &&
        std::memcmp(data, "BIKh", 4) != 0 &&
        std::memcmp(data, "BIKg", 4) != 0 &&
        std::memcmp(data, "BIKf", 4) != 0) {
        return std::nullopt;
    }

    VTexHeader hdr{};
    std::memcpy(hdr.magic, data, 4);
    hdr.file_size = readLE32(data + 4);
    hdr.frame_count = readLE32(data + 8);
    hdr.max_frame_size = readLE32(data + 12);
    hdr.frame_rate_num = readLE32(data + 16);
    hdr.frame_rate_den = readLE32(data + 20);
    hdr.flags = readLE32(data + 24);
    hdr.width = readLE32(data + 28);
    hdr.height = readLE32(data + 32);
    hdr.audio_tracks = readLE32(data + 36);
    hdr.has_alpha = (hdr.flags & 0x00100000) != 0;

    return hdr;
}

std::optional<ImageData> decode_vtex(const uint8_t* data, size_t size, uint32_t width, uint32_t height, uint32_t format_code) {
    auto hdr = parse_vtex_header(data, size);
    if (!hdr) return std::nullopt;

    uint32_t w = width ? width : hdr->width;
    uint32_t h = height ? height : hdr->height;
    if (w == 0 || h == 0) return std::nullopt;

    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.has_alpha = (format_code == 5) || hdr->has_alpha;
    img.is_placeholder = true;
    img.pixels.resize(w * h * 4, 255);

    // Note: Proprietary DCT video-texture stream decoding requires external
    // codecs or external texture assets (e.g. PAK/TGA/PNG). To ensure models
    // render safely in 3D tools without crashing, a neutral fallback image
    // is generated, flagged with is_placeholder = true so conversion passes
    // can resolve real image assets from disk or cache if available.
    return img;
}

std::pair<uint32_t, std::vector<uint8_t>> encode_vtex(const uint8_t* rgba, uint32_t width, uint32_t height) {
    // VTex compression uses a proprietary DCT video-texture bitstream.
    // Creating dummy 44-byte headers produces invalid video streams.
    // Callers (such as grn_writer) must encode textures using standard
    // supported GRN formats (e.g. DXT1/DXT5/RGBA) instead.
    (void)rgba;
    (void)width;
    (void)height;
    return {0, {}};
}

} // namespace grn
