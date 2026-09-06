#include "dxt_codec.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace grn {

static inline uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));
}

static inline void decode565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    b = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
    r = (uint8_t)((c & 0x1F) * 255 / 31);
}

std::vector<uint8_t> decode_dxt1(const uint8_t* data, size_t size, uint32_t width, uint32_t height) {
    std::vector<uint8_t> rgba(width * height * 4, 255);
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    size_t offset = 0;
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            if (offset + 8 > size) break;

            uint16_t c0 = data[offset] | (data[offset + 1] << 8);
            uint16_t c1 = data[offset + 2] | (data[offset + 3] << 8);
            uint32_t bits = data[offset + 4] | (data[offset + 5] << 8) | 
                            (data[offset + 6] << 16) | (data[offset + 7] << 24);
            offset += 8;

            uint8_t pal[4][4];
            decode565(c0, pal[0][0], pal[0][1], pal[0][2]); pal[0][3] = 255;
            decode565(c1, pal[1][0], pal[1][1], pal[1][2]); pal[1][3] = 255;

            if (c0 > c1) {
                pal[2][0] = (2 * pal[0][0] + pal[1][0]) / 3;
                pal[2][1] = (2 * pal[0][1] + pal[1][1]) / 3;
                pal[2][2] = (2 * pal[0][2] + pal[1][2]) / 3;
                pal[2][3] = 255;

                pal[3][0] = (pal[0][0] + 2 * pal[1][0]) / 3;
                pal[3][1] = (pal[0][1] + 2 * pal[1][1]) / 3;
                pal[3][2] = (pal[0][2] + 2 * pal[1][2]) / 3;
                pal[3][3] = 255;
            } else {
                pal[2][0] = (pal[0][0] + pal[1][0]) / 2;
                pal[2][1] = (pal[0][1] + pal[1][1]) / 2;
                pal[2][2] = (pal[0][2] + pal[1][2]) / 2;
                pal[2][3] = 255;

                pal[3][0] = 0;
                pal[3][1] = 0;
                pal[3][2] = 0;
                pal[3][3] = 0;
            }

            for (uint32_t py = 0; py < 4; ++py) {
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t gx = bx * 4 + px;
                    uint32_t gy = by * 4 + py;
                    if (gx < width && gy < height) {
                        uint32_t shift = (py * 4 + px) * 2;
                        uint32_t code = (bits >> shift) & 0x3;
                        size_t p_idx = (gy * width + gx) * 4;
                        rgba[p_idx + 0] = pal[code][0];
                        rgba[p_idx + 1] = pal[code][1];
                        rgba[p_idx + 2] = pal[code][2];
                        rgba[p_idx + 3] = pal[code][3];
                    }
                }
            }
        }
    }
    return rgba;
}

std::vector<uint8_t> encode_dxt1(const uint8_t* rgba, uint32_t width, uint32_t height) {
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;
    std::vector<uint8_t> out(blocks_x * blocks_y * 8);

    size_t out_idx = 0;
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            uint8_t min_c[3] = {255, 255, 255};
            uint8_t max_c[3] = {0, 0, 0};
            uint8_t block[16][4];

            for (uint32_t py = 0; py < 4; ++py) {
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t gx = std::min(bx * 4 + px, width - 1);
                    uint32_t gy = std::min(by * 4 + py, height - 1);
                    size_t p_idx = (gy * width + gx) * 4;
                    for (int c = 0; c < 3; ++c) {
                        block[py * 4 + px][c] = rgba[p_idx + c];
                        min_c[c] = std::min(min_c[c], rgba[p_idx + c]);
                        max_c[c] = std::max(max_c[c], rgba[p_idx + c]);
                    }
                    block[py * 4 + px][3] = rgba[p_idx + 3];
                }
            }

            uint16_t c0 = rgbTo565(max_c[0], max_c[1], max_c[2]);
            uint16_t c1 = rgbTo565(min_c[0], min_c[1], min_c[2]);
            if (c0 < c1) std::swap(c0, c1);

            uint8_t pal[4][3];
            decode565(c0, pal[0][0], pal[0][1], pal[0][2]);
            decode565(c1, pal[1][0], pal[1][1], pal[1][2]);
            pal[2][0] = (2 * pal[0][0] + pal[1][0]) / 3;
            pal[2][1] = (2 * pal[0][1] + pal[1][1]) / 3;
            pal[2][2] = (2 * pal[0][2] + pal[1][2]) / 3;
            pal[3][0] = (pal[0][0] + 2 * pal[1][0]) / 3;
            pal[3][1] = (pal[0][1] + 2 * pal[1][1]) / 3;
            pal[3][2] = (pal[0][2] + 2 * pal[1][2]) / 3;

            uint32_t bits = 0;
            for (uint32_t p = 0; p < 16; ++p) {
                uint32_t best_dist = 0xFFFFFFFF;
                uint32_t best_code = 0;
                for (uint32_t code = 0; code < 4; ++code) {
                    int dr = (int)block[p][0] - pal[code][0];
                    int dg = (int)block[p][1] - pal[code][1];
                    int db = (int)block[p][2] - pal[code][2];
                    uint32_t dist = (uint32_t)(dr * dr + dg * dg + db * db);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_code = code;
                    }
                }
                bits |= (best_code << (p * 2));
            }

            out[out_idx++] = (uint8_t)(c0 & 0xFF);
            out[out_idx++] = (uint8_t)((c0 >> 8) & 0xFF);
            out[out_idx++] = (uint8_t)(c1 & 0xFF);
            out[out_idx++] = (uint8_t)((c1 >> 8) & 0xFF);
            out[out_idx++] = (uint8_t)(bits & 0xFF);
            out[out_idx++] = (uint8_t)((bits >> 8) & 0xFF);
            out[out_idx++] = (uint8_t)((bits >> 16) & 0xFF);
            out[out_idx++] = (uint8_t)((bits >> 24) & 0xFF);
        }
    }
    return out;
}

std::vector<uint8_t> decode_dxt5(const uint8_t* data, size_t size, uint32_t width, uint32_t height) {
    std::vector<uint8_t> rgba(width * height * 4, 255);
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    size_t offset = 0;
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            if (offset + 16 > size) break;

            // 1. Alpha block (8 bytes)
            uint8_t a0 = data[offset + 0];
            uint8_t a1 = data[offset + 1];
            uint64_t a_bits = 0;
            for (int b = 0; b < 6; ++b) {
                a_bits |= (static_cast<uint64_t>(data[offset + 2 + b]) << (b * 8));
            }

            uint8_t pal_a[8];
            pal_a[0] = a0;
            pal_a[1] = a1;
            if (a0 > a1) {
                for (int i = 1; i <= 6; ++i) {
                    pal_a[1 + i] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
                }
            } else {
                for (int i = 1; i <= 4; ++i) {
                    pal_a[1 + i] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
                }
                pal_a[6] = 0;
                pal_a[7] = 255;
            }

            // 2. Color block (8 bytes)
            uint16_t c0 = data[offset + 8] | (data[offset + 9] << 8);
            uint16_t c1 = data[offset + 10] | (data[offset + 11] << 8);
            uint32_t c_bits = data[offset + 12] | (data[offset + 13] << 8) | 
                              (data[offset + 14] << 16) | (data[offset + 15] << 24);
            offset += 16;

            uint8_t pal_c[4][3];
            decode565(c0, pal_c[0][0], pal_c[0][1], pal_c[0][2]);
            decode565(c1, pal_c[1][0], pal_c[1][1], pal_c[1][2]);
            pal_c[2][0] = (2 * pal_c[0][0] + pal_c[1][0]) / 3;
            pal_c[2][1] = (2 * pal_c[0][1] + pal_c[1][1]) / 3;
            pal_c[2][2] = (2 * pal_c[0][2] + pal_c[1][2]) / 3;
            pal_c[3][0] = (pal_c[0][0] + 2 * pal_c[1][0]) / 3;
            pal_c[3][1] = (pal_c[0][1] + 2 * pal_c[1][1]) / 3;
            pal_c[3][2] = (pal_c[0][2] + 2 * pal_c[1][2]) / 3;

            for (uint32_t py = 0; py < 4; ++py) {
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t gx = bx * 4 + px;
                    uint32_t gy = by * 4 + py;
                    if (gx < width && gy < height) {
                        uint32_t p = py * 4 + px;
                        uint32_t c_code = (c_bits >> (p * 2)) & 0x3;
                        uint32_t a_code = static_cast<uint32_t>((a_bits >> (p * 3)) & 0x7);
                        size_t p_idx = (gy * width + gx) * 4;
                        rgba[p_idx + 0] = pal_c[c_code][0];
                        rgba[p_idx + 1] = pal_c[c_code][1];
                        rgba[p_idx + 2] = pal_c[c_code][2];
                        rgba[p_idx + 3] = pal_a[a_code];
                    }
                }
            }
        }
    }
    return rgba;
}

std::vector<uint8_t> encode_dxt5(const uint8_t* rgba, uint32_t width, uint32_t height) {
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;
    std::vector<uint8_t> out(blocks_x * blocks_y * 16);

    size_t out_idx = 0;
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            uint8_t min_c[3] = {255, 255, 255};
            uint8_t max_c[3] = {0, 0, 0};
            uint8_t min_a = 255;
            uint8_t max_a = 0;
            uint8_t block[16][4];

            for (uint32_t py = 0; py < 4; ++py) {
                for (uint32_t px = 0; px < 4; ++px) {
                    uint32_t gx = std::min(bx * 4 + px, width - 1);
                    uint32_t gy = std::min(by * 4 + py, height - 1);
                    size_t p_idx = (gy * width + gx) * 4;
                    for (int c = 0; c < 3; ++c) {
                        block[py * 4 + px][c] = rgba[p_idx + c];
                        min_c[c] = std::min(min_c[c], rgba[p_idx + c]);
                        max_c[c] = std::max(max_c[c], rgba[p_idx + c]);
                    }
                    uint8_t a = rgba[p_idx + 3];
                    block[py * 4 + px][3] = a;
                    min_a = std::min(min_a, a);
                    max_a = std::max(max_a, a);
                }
            }

            // 1. Encode Alpha (8 bytes)
            uint8_t a0 = max_a;
            uint8_t a1 = min_a;
            if (a0 <= a1) {
                a1 = (a0 > 0) ? 0 : 255;
            }

            uint8_t pal_a[8];
            pal_a[0] = a0;
            pal_a[1] = a1;
            for (int i = 1; i <= 6; ++i) {
                pal_a[1 + i] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
            }

            uint64_t a_bits = 0;
            for (uint32_t p = 0; p < 16; ++p) {
                uint8_t ap = block[p][3];
                int best_diff = 1000;
                uint32_t best_idx = 0;
                for (uint32_t i = 0; i < 8; ++i) {
                    int diff = std::abs(static_cast<int>(pal_a[i]) - static_cast<int>(ap));
                    if (diff < best_diff) {
                        best_diff = diff;
                        best_idx = i;
                    }
                }
                a_bits |= (static_cast<uint64_t>(best_idx) << (p * 3));
            }

            out[out_idx++] = a0;
            out[out_idx++] = a1;
            for (int b = 0; b < 6; ++b) {
                out[out_idx++] = static_cast<uint8_t>((a_bits >> (b * 8)) & 0xFF);
            }

            // 2. Encode Color (8 bytes)
            uint16_t c0 = rgbTo565(max_c[0], max_c[1], max_c[2]);
            uint16_t c1 = rgbTo565(min_c[0], min_c[1], min_c[2]);
            if (c0 <= c1) {
                if (c0 == c1) c0 = (c0 < 65535) ? (c0 + 1) : c0;
                else std::swap(c0, c1);
            }

            uint8_t pal_c[4][3];
            decode565(c0, pal_c[0][0], pal_c[0][1], pal_c[0][2]);
            decode565(c1, pal_c[1][0], pal_c[1][1], pal_c[1][2]);
            pal_c[2][0] = (2 * pal_c[0][0] + pal_c[1][0]) / 3;
            pal_c[2][1] = (2 * pal_c[0][1] + pal_c[1][1]) / 3;
            pal_c[2][2] = (2 * pal_c[0][2] + pal_c[1][2]) / 3;
            pal_c[3][0] = (pal_c[0][0] + 2 * pal_c[1][0]) / 3;
            pal_c[3][1] = (pal_c[0][1] + 2 * pal_c[1][1]) / 3;
            pal_c[3][2] = (pal_c[0][2] + 2 * pal_c[1][2]) / 3;

            uint32_t c_bits = 0;
            for (uint32_t p = 0; p < 16; ++p) {
                uint32_t best_dist = 0xFFFFFFFF;
                uint32_t best_code = 0;
                for (uint32_t code = 0; code < 4; ++code) {
                    int dr = static_cast<int>(block[p][0]) - pal_c[code][0];
                    int dg = static_cast<int>(block[p][1]) - pal_c[code][1];
                    int db = static_cast<int>(block[p][2]) - pal_c[code][2];
                    uint32_t dist = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_code = code;
                    }
                }
                c_bits |= (best_code << (p * 2));
            }

            out[out_idx++] = static_cast<uint8_t>(c0 & 0xFF);
            out[out_idx++] = static_cast<uint8_t>((c0 >> 8) & 0xFF);
            out[out_idx++] = static_cast<uint8_t>(c1 & 0xFF);
            out[out_idx++] = static_cast<uint8_t>((c1 >> 8) & 0xFF);
            out[out_idx++] = static_cast<uint8_t>(c_bits & 0xFF);
            out[out_idx++] = static_cast<uint8_t>((c_bits >> 8) & 0xFF);
            out[out_idx++] = static_cast<uint8_t>((c_bits >> 16) & 0xFF);
            out[out_idx++] = static_cast<uint8_t>((c_bits >> 24) & 0xFF);
        }
    }
    return out;
}

} // namespace grn
