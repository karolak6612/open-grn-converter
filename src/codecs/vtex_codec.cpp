/**
 * @file vtex_codec.cpp
 * @brief Native C++20 Granny 1.2b compatible VTex texture decompressor and compressor (Formats 4 and 5).
 *        Independent standalone implementation with direct planar bitstream algorithms.
 */

#include "vtex_codec.h"
#include "vtex_tables.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <optional>

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

static inline uint8_t clampByte(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return static_cast<uint8_t>(value);
}

static inline int integerLog2(int value) {
    int result = 0;
    while ((value >>= 1) != 0) ++result;
    return result;
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
    hdr.file_size = readLE32(data + 4) + 8;
    hdr.frame_count = readLE32(data + 8);
    hdr.max_frame_size = readLE32(data + 12);
    hdr.width = readLE32(data + 20);
    hdr.height = readLE32(data + 24);
    hdr.frame_rate_num = readLE32(data + 28);
    hdr.frame_rate_den = readLE32(data + 32);
    hdr.flags = readLE32(data + 36);
    hdr.audio_tracks = readLE32(data + 40);
    hdr.has_alpha = (hdr.flags & 0x00100000U) != 0;

    return hdr;
}

// ---------------------------------------------------------------------------
// Bitstream Reader (LSB-first streaming)
// ---------------------------------------------------------------------------

class VTexBitReader {
public:
    VTexBitReader(const uint8_t* data, size_t size)
        : m_data(data), m_size(size), m_bit(0), m_failed(false) {}

    uint32_t read(uint32_t count) {
        if (count > 32 || m_bit > m_size * 8 || count > m_size * 8 - m_bit) {
            m_failed = true;
            return 0;
        }
        uint32_t value = 0;
        for (uint32_t i = 0; i < count; ++i) {
            value |= ((m_data[m_bit >> 3] >> (m_bit & 7)) & 1U) << i;
            ++m_bit;
        }
        return value;
    }

    uint32_t peek(uint32_t count) {
        size_t old_bit = m_bit;
        bool old_failed = m_failed;
        uint32_t value = read(count);
        m_bit = old_bit;
        m_failed = old_failed;
        return value;
    }

    void skip(uint32_t count) {
        read(count);
    }

    void align32() {
        uint32_t rem = static_cast<uint32_t>(m_bit & 31);
        if (rem != 0) {
            skip(32 - rem);
        }
    }

    bool failed() const { return m_failed; }
    size_t bit_pos() const { return m_bit; }
    size_t size_bits() const { return m_size * 8; }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_bit = 0;
    bool m_failed = false;
};

// ---------------------------------------------------------------------------
// VTex Huffman Trees & Bundles (Decoder)
// ---------------------------------------------------------------------------

enum Source {
    SOURCE_BLOCK_TYPES,
    SOURCE_SUB_BLOCK_TYPES,
    SOURCE_COLORS,
    SOURCE_PATTERN,
    SOURCE_X_OFF,
    SOURCE_Y_OFF,
    SOURCE_INTRA_DC,
    SOURCE_INTER_DC,
    SOURCE_RUN,
    SOURCE_COUNT
};

enum BlockType {
    BLOCK_SKIP,
    BLOCK_SCALED,
    BLOCK_MOTION,
    BLOCK_RUN,
    BLOCK_RESIDUE,
    BLOCK_INTRA,
    BLOCK_FILL,
    BLOCK_INTER,
    BLOCK_PATTERN,
    BLOCK_RAW
};

struct VTexTree {
    uint8_t codebook = 0;
    uint8_t symbols[16] = {};
};

struct VTexBundle {
    uint32_t length_bits = 0;
    VTexTree huffman;
    uint8_t* data = nullptr;
    uint8_t* data_end = nullptr;
    uint8_t* decoded = nullptr;
    uint8_t* current = nullptr;
};

struct PlaneBuffer {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    std::vector<uint8_t> pixels;
};

struct DecoderState {
    uint32_t width = 0;
    uint32_t height = 0;
    bool has_alpha = false;
    std::vector<uint8_t> bundle_storage;
    VTexBundle bundles[SOURCE_COUNT];
    VTexTree color_high[16];
    int last_color = 0;
    PlaneBuffer planes[4]; // 0=Y, 1=U, 2=V, 3=Alpha
};

static int decode_huffman(VTexBitReader& bits, const VTexTree& tree) {
    uint32_t codebook = tree.codebook;
    for (uint32_t len = 1; len <= 7; ++len) {
        uint32_t code = bits.peek(len);
        for (uint32_t sym = 0; sym < 16; ++sym) {
            if (vtex_tree_lens[codebook][sym] == len &&
                vtex_tree_bits[codebook][sym] == code) {
                bits.skip(len);
                return tree.symbols[sym];
            }
        }
    }
    bits.skip(1);
    return 0;
}

static void merge_symbols(VTexBitReader& bits, uint8_t* dst, uint8_t* src, int size) {
    uint8_t* second = src + size;
    int second_size = size;
    while (size > 0 && second_size > 0) {
        if (!bits.read(1)) {
            *dst++ = *src++;
            --size;
        } else {
            *dst++ = *second++;
            --second_size;
        }
    }
    while (size-- > 0) *dst++ = *src++;
    while (second_size-- > 0) *dst++ = *second++;
}

static bool read_tree(VTexBitReader& bits, VTexTree& tree) {
    if (bits.size_bits() - bits.bit_pos() < 4) return false;
    tree.codebook = static_cast<uint8_t>(bits.read(4));
    if (tree.codebook == 0) {
        for (int i = 0; i < 16; ++i) tree.symbols[i] = static_cast<uint8_t>(i);
        return !bits.failed();
    }

    uint8_t first[16] = {0}, second[16] = {0};
    if (bits.read(1)) {
        int last = static_cast<int>(bits.read(3));
        for (int i = 0; i <= last; ++i) {
            tree.symbols[i] = static_cast<uint8_t>(bits.read(4));
            first[tree.symbols[i]] = 1;
        }
        for (int v = 0; v < 16 && last < 15; ++v) {
            if (!first[v]) tree.symbols[++last] = static_cast<uint8_t>(v);
        }
    } else {
        int levels = static_cast<int>(bits.read(2));
        for (int i = 0; i < 16; ++i) first[i] = static_cast<uint8_t>(i);
        uint8_t* in_p = first;
        uint8_t* out_p = second;
        for (int lvl = 0; lvl <= levels; ++lvl) {
            int sz = 1 << lvl;
            for (int off = 0; off < 16; off += sz * 2) {
                merge_symbols(bits, out_p + off, in_p + off, sz);
            }
            std::swap(in_p, out_p);
        }
        std::memcpy(tree.symbols, in_p, 16);
    }
    return !bits.failed();
}

static bool read_bundle_header(VTexBitReader& bits, DecoderState& dec, int src) {
    if (src == SOURCE_COLORS) {
        for (int t = 0; t < 16; ++t) {
            if (!read_tree(bits, dec.color_high[t])) return false;
        }
        dec.last_color = 0;
    }
    if (src != SOURCE_INTRA_DC && src != SOURCE_INTER_DC) {
        if (!read_tree(bits, dec.bundles[src].huffman)) return false;
    }
    dec.bundles[src].decoded = dec.bundles[src].data;
    dec.bundles[src].current = dec.bundles[src].data;
    return true;
}

static inline bool begin_bundle_read(VTexBitReader& bits, VTexBundle& b, uint32_t& cnt) {
    if (!b.decoded || b.decoded > b.current) { cnt = 0; return false; }
    cnt = bits.read(b.length_bits);
    if (cnt == 0) b.decoded = nullptr;
    return !bits.failed();
}

static bool read_runs(VTexBitReader& bits, VTexBundle& b) {
    uint32_t cnt = 0;
    if (!begin_bundle_read(bits, b, cnt) || !cnt) return !bits.failed();
    uint8_t* end = b.decoded + cnt;
    if (end > b.data_end) return false;
    if (bits.read(1)) {
        std::memset(b.decoded, static_cast<int>(bits.read(4)), cnt);
        b.decoded = end;
    } else {
        while (b.decoded < end) *b.decoded++ = static_cast<uint8_t>(decode_huffman(bits, b.huffman));
    }
    return !bits.failed();
}

static bool read_motion_values(VTexBitReader& bits, VTexBundle& b) {
    uint32_t cnt = 0;
    if (!begin_bundle_read(bits, b, cnt) || !cnt) return !bits.failed();
    uint8_t* end = b.decoded + cnt;
    if (end > b.data_end) return false;
    if (bits.read(1)) {
        int val = static_cast<int>(bits.read(4));
        if (val && bits.read(1)) val = -val;
        std::memset(b.decoded, val, cnt);
        b.decoded = end;
    } else {
        while (b.decoded < end) {
            int val = decode_huffman(bits, b.huffman);
            if (val && bits.read(1)) val = -val;
            *b.decoded++ = static_cast<uint8_t>(val);
        }
    }
    return !bits.failed();
}

static bool read_block_types(VTexBitReader& bits, VTexBundle& b) {
    static const uint8_t run_lens[4] = {4, 8, 12, 32};
    uint32_t cnt = 0;
    if (!begin_bundle_read(bits, b, cnt) || !cnt) return !bits.failed();
    uint8_t* end = b.decoded + cnt;
    if (end > b.data_end) return false;
    if (bits.read(1)) {
        std::memset(b.decoded, static_cast<int>(bits.read(4)), cnt);
        b.decoded = end;
    } else {
        int last = 0;
        while (b.decoded < end) {
            int val = decode_huffman(bits, b.huffman);
            if (val < 12) {
                last = val;
                *b.decoded++ = static_cast<uint8_t>(val);
            } else {
                uint32_t run = run_lens[val - 12];
                if (static_cast<size_t>(end - b.decoded) < run) return false;
                std::memset(b.decoded, last, run);
                b.decoded += run;
            }
        }
    }
    return !bits.failed();
}

static bool read_patterns(VTexBitReader& bits, VTexBundle& b) {
    uint32_t cnt = 0;
    if (!begin_bundle_read(bits, b, cnt) || !cnt) return !bits.failed();
    uint8_t* end = b.decoded + cnt;
    if (end > b.data_end) return false;
    while (b.decoded < end) {
        int val = decode_huffman(bits, b.huffman);
        val |= decode_huffman(bits, b.huffman) << 4;
        *b.decoded++ = static_cast<uint8_t>(val);
    }
    return !bits.failed();
}

static bool read_colors(VTexBitReader& bits, DecoderState& dec, VTexBundle& b) {
    uint32_t cnt = 0;
    if (!begin_bundle_read(bits, b, cnt) || !cnt) return !bits.failed();
    uint8_t* end = b.decoded + cnt;
    if (end > b.data_end) return false;
    bool constant = bits.read(1) != 0;
    do {
        dec.last_color = decode_huffman(bits, dec.color_high[dec.last_color]);
        int val = decode_huffman(bits, b.huffman);
        val |= dec.last_color << 4;
        if (constant) {
            std::memset(b.decoded, val, cnt);
            b.decoded = end;
        } else {
            *b.decoded++ = static_cast<uint8_t>(val);
        }
    } while (b.decoded < end);
    return !bits.failed();
}

static bool read_dcs(VTexBitReader& bits, VTexBundle& b, bool signed_vals) {
    uint32_t cnt = 0;
    if (!begin_bundle_read(bits, b, cnt) || !cnt) return !bits.failed();
    int16_t* dst = reinterpret_cast<int16_t*>(b.decoded);
    int16_t* end = reinterpret_cast<int16_t*>(b.data_end);
    int val = static_cast<int>(bits.read(11 - (signed_vals ? 1 : 0)));
    if (val && signed_vals && bits.read(1)) val = -val;
    if (dst >= end) return false;
    *dst++ = static_cast<int16_t>(val);
    --cnt;
    for (uint32_t off = 0; off < cnt; off += 8) {
        uint32_t grp = (std::min)(8U, cnt - off);
        uint32_t sz = bits.read(4);
        if (dst + grp > end) return false;
        for (uint32_t i = 0; i < grp; ++i) {
            if (sz) {
                int delta = static_cast<int>(bits.read(sz));
                if (delta && bits.read(1)) delta = -delta;
                val += delta;
            }
            *dst++ = static_cast<int16_t>(val);
        }
    }
    b.decoded = reinterpret_cast<uint8_t*>(dst);
    return !bits.failed();
}

static inline int get_value(DecoderState& dec, int src) {
    VTexBundle& b = dec.bundles[src];
    if (!b.current || b.current >= b.data_end) return 0;
    if (src < SOURCE_X_OFF || src == SOURCE_RUN) return *b.current++;
    if (src == SOURCE_X_OFF || src == SOURCE_Y_OFF) return static_cast<int8_t>(*b.current++);
    if (b.current + 2 > b.data_end) return 0;
    int val = static_cast<int16_t>(b.current[0] | (b.current[1] << 8));
    b.current += 2;
    return val;
}

// ---------------------------------------------------------------------------
// IDCT Implementation (AAN fast integer IDCT)
// ---------------------------------------------------------------------------

static int read_dct_coefficients(VTexBitReader& bits, int32_t block[64], int& coeff_cnt, int indices[64]) {
    int coeff_list[128], mode_list[128];
    int start = 64, end = 64;
    coeff_cnt = 0;
    coeff_list[end] = 4; mode_list[end++] = 0;
    coeff_list[end] = 24; mode_list[end++] = 0;
    coeff_list[end] = 44; mode_list[end++] = 0;
    coeff_list[end] = 1; mode_list[end++] = 3;
    coeff_list[end] = 2; mode_list[end++] = 3;
    coeff_list[end] = 3; mode_list[end++] = 3;

    for (int mag = static_cast<int>(bits.read(4)) - 1; mag >= 0; --mag) {
        int pos = start;
        while (pos < end) {
            if (!(mode_list[pos] | coeff_list[pos]) || !bits.read(1)) { ++pos; continue; }
            int coeff = coeff_list[pos];
            int mode = mode_list[pos];
            if (mode == 0 || mode == 2) {
                if (mode == 0) { coeff_list[pos] = coeff + 4; mode_list[pos] = 1; }
                else { coeff_list[pos] = 0; mode_list[pos++] = 0; }
                for (int g = 0; g < 4; ++g, ++coeff) {
                    if (bits.read(1)) {
                        coeff_list[--start] = coeff;
                        mode_list[start] = 3;
                    } else {
                        int v = (!mag) ? (bits.read(1) ? -1 : 1) : (static_cast<int>(bits.read(mag)) | (1 << mag));
                        if (mag && bits.read(1)) v = -v;
                        block[vtex_scan[coeff]] = v;
                        indices[coeff_cnt++] = coeff;
                    }
                }
            } else if (mode == 1) {
                mode_list[pos] = 2;
                for (int g = 0; g < 3; ++g) {
                    coeff += 4;
                    coeff_list[end] = coeff;
                    mode_list[end++] = 2;
                }
            } else {
                int v = (!mag) ? (bits.read(1) ? -1 : 1) : (static_cast<int>(bits.read(mag)) | (1 << mag));
                if (mag && bits.read(1)) v = -v;
                block[vtex_scan[coeff]] = v;
                indices[coeff_cnt++] = coeff;
                coeff_list[pos] = 0;
                mode_list[pos++] = 0;
            }
        }
    }
    return bits.failed() ? -1 : static_cast<int>(bits.read(4));
}

static void unquantize(int32_t block[64], const int32_t quant[64], int count, const int indices[64]) {
    block[0] = static_cast<int32_t>((static_cast<int64_t>(block[0]) * quant[0]) >> 11);
    for (int i = 0; i < count; ++i) {
        int s = indices[i];
        int d = vtex_scan[s];
        block[d] = static_cast<int32_t>((static_cast<int64_t>(block[d]) * quant[s]) >> 11);
    }
}

static inline int multiply_dct(int left, int right) {
    return static_cast<int32_t>(static_cast<uint32_t>(left) * static_cast<uint32_t>(right)) >> 11;
}

static void transform_dct(const int src[8], int dst[8], bool row) {
    int a0 = src[0] + src[4], a1 = src[0] - src[4];
    int a2 = src[2] + src[6], a3 = multiply_dct(2896, src[2] - src[6]);
    int a4 = src[5] + src[3], a5 = src[5] - src[3];
    int a6 = src[1] + src[7], a7 = src[1] - src[7];
    int b0 = a4 + a6, b1 = multiply_dct(3784, a5 + a7);
    int b2 = multiply_dct(-5352, a5) - b0 + b1;
    int b3 = multiply_dct(2896, a6 - a4) - b2;
    int b4 = multiply_dct(2217, a7) + b3 - b1;
    int vals[8] = {
        a0 + a2 + b0, a1 + a3 - a2 + b2, a1 - a3 + a2 + b3, a0 - a2 - b4,
        a0 - a2 + b4, a1 - a3 + a2 - b3, a1 + a3 - a2 - b2, a0 + a2 - b0
    };
    for (int i = 0; i < 8; ++i) dst[i] = row ? ((vals[i] + 0x7F) >> 8) : vals[i];
}

static void inverse_dct(int32_t block[64]) {
    int tmp[64];
    for (int c = 0; c < 8; ++c) {
        int src[8], dst[8];
        for (int r = 0; r < 8; ++r) src[r] = block[r * 8 + c];
        if ((src[1]|src[2]|src[3]|src[4]|src[5]|src[6]|src[7]) == 0) {
            for (int r = 0; r < 8; ++r) tmp[r * 8 + c] = src[0];
        } else {
            transform_dct(src, dst, false);
            for (int r = 0; r < 8; ++r) tmp[r * 8 + c] = dst[r];
        }
    }
    for (int r = 0; r < 8; ++r) {
        int dst[8];
        transform_dct(tmp + r * 8, dst, true);
        for (int c = 0; c < 8; ++c) block[r * 8 + c] = dst[c];
    }
}

static void idct_put(uint8_t* dst, uint32_t stride, int32_t block[64]) {
    inverse_dct(block);
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            dst[r * stride + c] = static_cast<uint8_t>(block[r * 8 + c]);
        }
    }
}

static void scale_block(const uint8_t src[64], uint8_t* dst, uint32_t stride) {
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            uint8_t val = src[r * 8 + c];
            dst[(r * 2) * stride + c * 2] = val;
            dst[(r * 2) * stride + c * 2 + 1] = val;
            dst[(r * 2 + 1) * stride + c * 2] = val;
            dst[(r * 2 + 1) * stride + c * 2 + 1] = val;
        }
    }
}

static bool decode_plane(DecoderState* dec, VTexBitReader& bits, int plane_idx, bool chroma) {
    PlaneBuffer& plane = dec->planes[plane_idx];
    uint32_t bw = chroma ? (dec->width + 15) >> 4 : (dec->width + 7) >> 3;
    uint32_t bh = chroma ? (dec->height + 15) >> 4 : (dec->height + 7) >> 3;
    uint32_t log_w = chroma ? dec->width >> 1 : dec->width;
    uint32_t len_w = (std::max)(log_w, 8U);
    uint32_t al_w = (len_w + 7) & ~7U;

    dec->bundles[SOURCE_BLOCK_TYPES].length_bits = integerLog2((al_w >> 3) + 511) + 1;
    dec->bundles[SOURCE_SUB_BLOCK_TYPES].length_bits = integerLog2((al_w >> 4) + 511) + 1;
    dec->bundles[SOURCE_COLORS].length_bits = integerLog2(bw * 64 + 511) + 1;
    dec->bundles[SOURCE_INTRA_DC].length_bits =
    dec->bundles[SOURCE_INTER_DC].length_bits =
    dec->bundles[SOURCE_X_OFF].length_bits =
    dec->bundles[SOURCE_Y_OFF].length_bits = integerLog2((al_w >> 3) + 511) + 1;
    dec->bundles[SOURCE_PATTERN].length_bits = integerLog2((bw << 3) + 511) + 1;
    dec->bundles[SOURCE_RUN].length_bits = integerLog2(bw * 48 + 511) + 1;

    for (int s = 0; s < SOURCE_COUNT; ++s) {
        if (!read_bundle_header(bits, *dec, s)) return false;
    }

    int coords[64];
    for (int i = 0; i < 64; ++i) coords[i] = (i & 7) + (i >> 3) * plane.stride;

    for (uint32_t by = 0; by < bh; ++by) {
        if (!read_block_types(bits, dec->bundles[SOURCE_BLOCK_TYPES]) ||
            !read_block_types(bits, dec->bundles[SOURCE_SUB_BLOCK_TYPES]) ||
            !read_colors(bits, *dec, dec->bundles[SOURCE_COLORS]) ||
            !read_patterns(bits, dec->bundles[SOURCE_PATTERN]) ||
            !read_motion_values(bits, dec->bundles[SOURCE_X_OFF]) ||
            !read_motion_values(bits, dec->bundles[SOURCE_Y_OFF]) ||
            !read_dcs(bits, dec->bundles[SOURCE_INTRA_DC], false) ||
            !read_dcs(bits, dec->bundles[SOURCE_INTER_DC], true) ||
            !read_runs(bits, dec->bundles[SOURCE_RUN])) return false;

        for (uint32_t bx = 0; bx < bw; ++bx) {
            uint8_t* dst = &plane.pixels[by * 8 * plane.stride + bx * 8];
            int type = get_value(*dec, SOURCE_BLOCK_TYPES);

            if (((by & 1) || (bx & 1)) && type == BLOCK_SCALED) { ++bx; continue; }

            if (type == BLOCK_SKIP) {
                for (int r = 0; r < 8; ++r) std::memset(dst + r * plane.stride, 0, 8);
            } else if (type == BLOCK_SCALED) {
                uint8_t unscaled[64] = {0};
                int sub = get_value(*dec, SOURCE_SUB_BLOCK_TYPES);
                if (sub == BLOCK_RUN) {
                    const uint8_t* scan = vtex_patterns[bits.read(4)];
                    int written = 0;
                    do {
                        int run = get_value(*dec, SOURCE_RUN) + 1;
                        written += run;
                        if (written > 64) return false;
                        if (bits.read(1)) {
                            uint8_t v = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                            for (int i = 0; i < run; ++i) unscaled[*scan++] = v;
                        } else {
                            for (int i = 0; i < run; ++i) unscaled[*scan++] = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                        }
                    } while (written < 63);
                    if (written == 63) unscaled[*scan] = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                } else if (sub == BLOCK_INTRA) {
                    int32_t coeffs[64] = {0};
                    int idxs[64]; int cnt = 0;
                    coeffs[0] = get_value(*dec, SOURCE_INTRA_DC);
                    int q = read_dct_coefficients(bits, coeffs, cnt, idxs);
                    if (q < 0 || q > 15) return false;
                    unquantize(coeffs, vtex_intra_quant[q], cnt, idxs);
                    idct_put(unscaled, 8, coeffs);
                } else if (sub == BLOCK_FILL) {
                    uint8_t v = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                    for (int r = 0; r < 16; ++r) std::memset(dst + r * plane.stride, v, 16);
                } else if (sub == BLOCK_PATTERN) {
                    uint8_t c0 = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                    uint8_t c1 = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                    for (int r = 0; r < 8; ++r) {
                        int pat = get_value(*dec, SOURCE_PATTERN);
                        for (int c = 0; c < 8; ++c, pat >>= 1) unscaled[r * 8 + c] = (pat & 1) ? c1 : c0;
                    }
                } else if (sub == BLOCK_RAW) {
                    for (int i = 0; i < 64; ++i) unscaled[i] = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                } else return false;

                if (sub != BLOCK_FILL) scale_block(unscaled, dst, plane.stride);
                ++bx;
            } else if (type == BLOCK_RUN) {
                const uint8_t* scan = vtex_patterns[bits.read(4)];
                int written = 0;
                do {
                    int run = get_value(*dec, SOURCE_RUN) + 1;
                    written += run;
                    if (written > 64) return false;
                    if (bits.read(1)) {
                        uint8_t v = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                        for (int i = 0; i < run; ++i) dst[coords[*scan++]] = v;
                    } else {
                        for (int i = 0; i < run; ++i) dst[coords[*scan++]] = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                    }
                } while (written < 63);
                if (written == 63) dst[coords[*scan]] = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
            } else if (type == BLOCK_INTRA) {
                int32_t coeffs[64] = {0};
                int idxs[64]; int cnt = 0;
                coeffs[0] = get_value(*dec, SOURCE_INTRA_DC);
                int q = read_dct_coefficients(bits, coeffs, cnt, idxs);
                if (q < 0 || q > 15) return false;
                unquantize(coeffs, vtex_intra_quant[q], cnt, idxs);
                idct_put(dst, plane.stride, coeffs);
            } else if (type == BLOCK_FILL) {
                uint8_t v = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                for (int r = 0; r < 8; ++r) std::memset(dst + r * plane.stride, v, 8);
            } else if (type == BLOCK_PATTERN) {
                uint8_t c0 = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                uint8_t c1 = static_cast<uint8_t>(get_value(*dec, SOURCE_COLORS));
                for (int r = 0; r < 8; ++r) {
                    int pat = get_value(*dec, SOURCE_PATTERN);
                    for (int c = 0; c < 8; ++c, pat >>= 1) dst[r * plane.stride + c] = (pat & 1) ? c1 : c0;
                }
            } else if (type == BLOCK_RAW) {
                VTexBundle& cols = dec->bundles[SOURCE_COLORS];
                if (!cols.current || cols.current + 64 > cols.data_end) return false;
                for (int r = 0; r < 8; ++r) std::memcpy(dst + r * plane.stride, cols.current + r * 8, 8);
                cols.current += 64;
            } else return false;
        }
    }
    bits.align32();
    return !bits.failed();
}

// ---------------------------------------------------------------------------
// High-Level Decoder (`decode_vtex`)
// ---------------------------------------------------------------------------

std::optional<ImageData> decode_vtex(const uint8_t* data, size_t size, uint32_t width, uint32_t height, uint32_t format_code) {
    auto hdr = parse_vtex_header(data, size);
    if (!hdr) return std::nullopt;

    uint32_t w = width ? width : hdr->width;
    uint32_t h = height ? height : hdr->height;
    if (w == 0 || h == 0) return std::nullopt;

    bool has_alpha = hdr->has_alpha || (format_code == 5);

    if (size < 48) return std::nullopt;
    uint32_t frame0_off = readLE32(data + 44) & ~1U;
    if (frame0_off >= size) return std::nullopt;

    const uint8_t* packet = data + frame0_off;
    size_t packet_size = size - frame0_off;

    DecoderState dec;
    dec.width = w;
    dec.height = h;
    dec.has_alpha = has_alpha;

    uint32_t lw = ((w + 7) >> 3) * 8;
    uint32_t lh = ((h + 7) >> 3) * 8;
    uint32_t cw = ((w + 15) >> 4) * 8;
    uint32_t ch = ((h + 15) >> 4) * 8;

    for (int p = 0; p < 4; ++p) {
        PlaneBuffer& pl = dec.planes[p];
        pl.width = (p == 1 || p == 2) ? cw : lw;
        pl.height = (p == 1 || p == 2) ? ch : lh;
        pl.stride = pl.width;
        pl.pixels.resize(static_cast<size_t>(pl.stride) * pl.height, (p == 3 ? 255 : 0));
    }

    size_t blk_cnt = static_cast<size_t>((w + 7) >> 3) * ((h + 7) >> 3);
    size_t b_sz = blk_cnt * 64;
    dec.bundle_storage.resize(b_sz * SOURCE_COUNT);
    for (int s = 0; s < SOURCE_COUNT; ++s) {
        VTexBundle& b = dec.bundles[s];
        b.data = dec.bundle_storage.data() + s * b_sz;
        b.data_end = b.data + b_sz;
        b.decoded = b.data;
        b.current = b.data;
    }

    VTexBitReader bits(packet, packet_size);

    if (has_alpha) {
        uint32_t alpha_len = bits.read(32);
        (void)alpha_len;
        if (!decode_plane(&dec, bits, 3, false)) {
            return std::nullopt;
        }
    }

    uint32_t y_len = bits.read(32);
    (void)y_len;
    if (!decode_plane(&dec, bits, 0, false)) {
        return std::nullopt;
    }
    if (!decode_plane(&dec, bits, 2, true)) {
        return std::nullopt;
    }
    if (!decode_plane(&dec, bits, 1, true)) {
        return std::nullopt;
    }

    ImageData img;
    img.width = w;
    img.height = h;
    img.channels = 4;
    img.has_alpha = has_alpha;
    img.is_placeholder = false;
    img.pixels.resize(static_cast<size_t>(w) * h * 4);

    const PlaneBuffer& Y = dec.planes[0];
    const PlaneBuffer& U = dec.planes[1];
    const PlaneBuffer& V = dec.planes[2];
    const PlaneBuffer& A = dec.planes[3];

    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            int vy = Y.pixels[y * Y.stride + x];
            int vu = U.pixels[(y >> 1) * U.stride + (x >> 1)];
            int vv = V.pixels[(y >> 1) * V.stride + (x >> 1)];
            int c = (std::max)(0, vy - 16);
            int d = vu - 128;
            int e = vv - 128;
            size_t dst = (static_cast<size_t>(y) * w + x) * 4;
            img.pixels[dst + 0] = clampByte((298 * c + 409 * e + 128) >> 8);
            img.pixels[dst + 1] = clampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
            img.pixels[dst + 2] = clampByte((298 * c + 516 * d + 128) >> 8);
            img.pixels[dst + 3] = has_alpha ? A.pixels[y * A.stride + x] : 255;
        }
    }

    return img;
}

// ---------------------------------------------------------------------------
// Bitstream Writer (LSB-first streaming for Encoder)
// ---------------------------------------------------------------------------

class VTexBitWriter {
public:
    void write(uint32_t val, uint32_t count) {
        if (count == 0) return;
        uint64_t mask = (count == 32) ? 0xFFFFFFFFULL : ((1ULL << count) - 1ULL);
        m_bit_buf |= (static_cast<uint64_t>(val) & mask) << m_bits_in_buf;
        m_bits_in_buf += count;
        while (m_bits_in_buf >= 32) {
            uint32_t word = static_cast<uint32_t>(m_bit_buf & 0xFFFFFFFFU);
            m_data.push_back(static_cast<uint8_t>(word & 0xFF));
            m_data.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
            m_data.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
            m_data.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
            m_bit_buf >>= 32;
            m_bits_in_buf -= 32;
        }
    }

    void align32() {
        uint32_t rem = m_bits_in_buf & 31;
        if (rem != 0) {
            write(0, 32 - rem);
        }
    }

    std::vector<uint8_t> to_array() {
        align32();
        while (m_bits_in_buf >= 32) {
            uint32_t word = static_cast<uint32_t>(m_bit_buf & 0xFFFFFFFFU);
            m_data.push_back(static_cast<uint8_t>(word & 0xFF));
            m_data.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
            m_data.push_back(static_cast<uint8_t>((word >> 16) & 0xFF));
            m_data.push_back(static_cast<uint8_t>((word >> 24) & 0xFF));
            m_bit_buf >>= 32;
            m_bits_in_buf -= 32;
        }
        return m_data;
    }

private:
    std::vector<uint8_t> m_data;
    uint64_t m_bit_buf = 0;
    uint32_t m_bits_in_buf = 0;
};

enum class BundleKind {
    BlockType,
    Color,
    Pattern,
    Motion,
    Run,
    DcUnsigned,
    DcSigned
};

class EncoderBundleWriter {
public:
    EncoderBundleWriter(std::vector<uint8_t> values, int length_bits, BundleKind kind)
        : m_values(std::move(values)), m_length_bits(length_bits), m_kind(kind) {}

    void write_if_needed(VTexBitWriter& bits, int requested_count) {
        if (m_ended || m_loaded > m_consumed) return;

        int max_count = (1 << m_length_bits) - 1;
        int count = std::min(static_cast<int>(m_values.size()) - m_loaded, requested_count);
        if (count > max_count) count = max_count;

        bits.write(static_cast<uint32_t>(count), m_length_bits);
        if (count == 0) {
            m_ended = true;
            return;
        }

        const uint8_t* chunk = m_values.data() + m_loaded;
        switch (m_kind) {
            case BundleKind::BlockType:
            case BundleKind::Run: {
                bool constant = true;
                for (int i = 1; i < count; ++i) {
                    if (chunk[i] != chunk[0]) { constant = false; break; }
                }
                bits.write(constant ? 1U : 0U, 1);
                if (constant) {
                    bits.write(chunk[0], 4);
                } else {
                    for (int i = 0; i < count; ++i) {
                        bits.write(chunk[i], 4);
                    }
                }
                break;
            }
            case BundleKind::Color: {
                bool constant = true;
                for (int i = 1; i < count; ++i) {
                    if (chunk[i] != chunk[0]) { constant = false; break; }
                }
                bits.write(constant ? 1U : 0U, 1);
                int write_count = constant ? 1 : count;
                for (int i = 0; i < write_count; ++i) {
                    bits.write(chunk[i] >> 4, 4);
                    bits.write(chunk[i] & 15, 4);
                }
                break;
            }
            case BundleKind::Pattern: {
                for (int i = 0; i < count; ++i) {
                    bits.write(chunk[i] & 15, 4);
                    bits.write(chunk[i] >> 4, 4);
                }
                break;
            }
            default:
                break;
        }

        m_loaded += count;
    }

    void consume(int count) {
        m_consumed += count;
    }

private:
    std::vector<uint8_t> m_values;
    int m_length_bits = 0;
    BundleKind m_kind = BundleKind::BlockType;
    int m_loaded = 0;
    int m_consumed = 0;
    bool m_ended = false;
};

static int next_nonzero(const std::vector<int>& consumption, size_t row) {
    for (size_t i = row; i < consumption.size(); ++i) {
        if (consumption[i] != 0) return consumption[i];
    }
    return 0;
}

static std::vector<uint8_t> encode_plane(const uint8_t* plane_pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t logical_source_width) {
    VTexBitWriter bits;
    uint32_t block_w = width / 8;
    uint32_t block_h = height / 8;

    std::vector<uint8_t> block_types;
    block_types.reserve(block_w * block_h);
    std::vector<uint8_t> colors;
    colors.reserve(block_w * block_h * 32);
    std::vector<uint8_t> patterns;
    patterns.reserve(block_w * block_h * 8);

    std::vector<int> color_consumption(block_h, 0);
    std::vector<int> pattern_consumption(block_h, 0);

    for (uint32_t by = 0; by < block_h; ++by) {
        size_t colors_start = colors.size();
        size_t patterns_start = patterns.size();

        for (uint32_t bx = 0; bx < block_w; ++bx) {
            uint32_t offset = by * 8 * stride + bx * 8;
            uint8_t first = plane_pixels[offset];
            uint8_t second = 0;
            bool has_second = false;
            bool more_than_two = false;

            for (uint32_t row = 0; row < 8 && !more_than_two; ++row) {
                uint32_t row_off = offset + row * stride;
                for (uint32_t col = 0; col < 8; ++col) {
                    uint8_t val = plane_pixels[row_off + col];
                    if (val == first || (has_second && val == second)) continue;
                    if (!has_second) {
                        second = val;
                        has_second = true;
                    } else {
                        more_than_two = true;
                        break;
                    }
                }
            }

            if (!has_second) {
                block_types.push_back(BLOCK_FILL);
                colors.push_back(first);
            } else if (!more_than_two) {
                block_types.push_back(BLOCK_PATTERN);
                colors.push_back(first);
                colors.push_back(second);
                for (uint32_t row = 0; row < 8; ++row) {
                    uint8_t pat = 0;
                    uint32_t row_off = offset + row * stride;
                    for (uint32_t col = 0; col < 8; ++col) {
                        if (plane_pixels[row_off + col] == second) {
                            pat |= static_cast<uint8_t>(1 << col);
                        }
                    }
                    patterns.push_back(pat);
                }
            } else {
                block_types.push_back(BLOCK_RAW);
                for (uint32_t row = 0; row < 8; ++row) {
                    uint32_t row_off = offset + row * stride;
                    for (uint32_t col = 0; col < 8; ++col) {
                        colors.push_back(plane_pixels[row_off + col]);
                    }
                }
            }
        }

        color_consumption[by] = static_cast<int>(colors.size() - colors_start);
        pattern_consumption[by] = static_cast<int>(patterns.size() - patterns_start);
    }

    uint32_t len_w = (std::max)(logical_source_width, 8U);
    uint32_t al_w = (len_w + 7) & ~7U;

    int length_bits[SOURCE_COUNT];
    length_bits[SOURCE_BLOCK_TYPES] = integerLog2((al_w >> 3) + 511) + 1;
    length_bits[SOURCE_SUB_BLOCK_TYPES] = integerLog2((al_w >> 4) + 511) + 1;
    length_bits[SOURCE_COLORS] = integerLog2(block_w * 64 + 511) + 1;
    length_bits[SOURCE_X_OFF] =
    length_bits[SOURCE_Y_OFF] =
    length_bits[SOURCE_INTRA_DC] =
    length_bits[SOURCE_INTER_DC] = integerLog2((al_w >> 3) + 511) + 1;
    length_bits[SOURCE_PATTERN] = integerLog2((block_w << 3) + 511) + 1;
    length_bits[SOURCE_RUN] = integerLog2(block_w * 48 + 511) + 1;

    // Write identity bundle headers (codebook = 0)
    for (int s = 0; s < SOURCE_COUNT; ++s) {
        if (s == SOURCE_COLORS) {
            for (int t = 0; t < 16; ++t) bits.write(0, 4);
        }
        if (s != SOURCE_INTRA_DC && s != SOURCE_INTER_DC) {
            bits.write(0, 4);
        }
    }

    std::vector<EncoderBundleWriter> bundle_writers;
    bundle_writers.emplace_back(block_types, length_bits[SOURCE_BLOCK_TYPES], BundleKind::BlockType);
    bundle_writers.emplace_back(std::vector<uint8_t>{}, length_bits[SOURCE_SUB_BLOCK_TYPES], BundleKind::BlockType);
    bundle_writers.emplace_back(colors, length_bits[SOURCE_COLORS], BundleKind::Color);
    bundle_writers.emplace_back(patterns, length_bits[SOURCE_PATTERN], BundleKind::Pattern);
    bundle_writers.emplace_back(std::vector<uint8_t>{}, length_bits[SOURCE_X_OFF], BundleKind::Motion);
    bundle_writers.emplace_back(std::vector<uint8_t>{}, length_bits[SOURCE_Y_OFF], BundleKind::Motion);
    bundle_writers.emplace_back(std::vector<uint8_t>{}, length_bits[SOURCE_INTRA_DC], BundleKind::DcUnsigned);
    bundle_writers.emplace_back(std::vector<uint8_t>{}, length_bits[SOURCE_INTER_DC], BundleKind::DcSigned);
    bundle_writers.emplace_back(std::vector<uint8_t>{}, length_bits[SOURCE_RUN], BundleKind::Run);

    for (uint32_t row = 0; row < block_h; ++row) {
        bundle_writers[SOURCE_BLOCK_TYPES].write_if_needed(bits, block_w);
        bundle_writers[SOURCE_SUB_BLOCK_TYPES].write_if_needed(bits, 0);
        bundle_writers[SOURCE_COLORS].write_if_needed(bits, next_nonzero(color_consumption, row));
        bundle_writers[SOURCE_PATTERN].write_if_needed(bits, next_nonzero(pattern_consumption, row));
        bundle_writers[SOURCE_X_OFF].write_if_needed(bits, 0);
        bundle_writers[SOURCE_Y_OFF].write_if_needed(bits, 0);
        bundle_writers[SOURCE_INTRA_DC].write_if_needed(bits, 0);
        bundle_writers[SOURCE_INTER_DC].write_if_needed(bits, 0);
        bundle_writers[SOURCE_RUN].write_if_needed(bits, 0);

        bundle_writers[SOURCE_BLOCK_TYPES].consume(block_w);
        bundle_writers[SOURCE_COLORS].consume(color_consumption[row]);
        bundle_writers[SOURCE_PATTERN].consume(pattern_consumption[row]);
    }

    return bits.to_array();
}

// ---------------------------------------------------------------------------
// High-Level Encoder (`encode_vtex`)
// ---------------------------------------------------------------------------

std::pair<uint32_t, std::vector<uint8_t>> encode_vtex(const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0) return {0, {}};

    bool has_alpha = false;
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        if (rgba[i * 4 + 3] < 250) {
            has_alpha = true;
            break;
        }
    }
    uint32_t fmt_code = has_alpha ? 5 : 4;

    uint32_t lw = ((width + 7) >> 3) * 8;
    uint32_t lh = ((height + 7) >> 3) * 8;
    uint32_t cw = ((width + 15) >> 4) * 8;
    uint32_t ch = ((height + 15) >> 4) * 8;

    std::vector<uint8_t> y_plane(static_cast<size_t>(lw) * lh, 16);
    std::vector<uint8_t> cr_plane(static_cast<size_t>(cw) * ch, 128);
    std::vector<uint8_t> cb_plane(static_cast<size_t>(cw) * ch, 128);
    std::vector<uint8_t> a_plane;
    if (has_alpha) a_plane.resize(static_cast<size_t>(lw) * lh, 255);

    // RGB -> YCbCr conversion
    for (uint32_t y = 0; y < lh; ++y) {
        uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < lw; ++x) {
            uint32_t sx = std::min(x, width - 1);
            size_t idx = (static_cast<size_t>(sy) * width + sx) * 4;
            int r = rgba[idx + 0];
            int g = rgba[idx + 1];
            int b = rgba[idx + 2];
            y_plane[y * lw + x] = clampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            if (has_alpha) {
                a_plane[y * lw + x] = rgba[idx + 3];
            }
        }
    }

    for (uint32_t cy = 0; cy < ch; ++cy) {
        for (uint32_t cx = 0; cx < cw; ++cx) {
            int r_sum = 0, g_sum = 0, b_sum = 0;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                uint32_t sy = std::min(cy * 2 + dy, height - 1);
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    uint32_t sx = std::min(cx * 2 + dx, width - 1);
                    size_t idx = (static_cast<size_t>(sy) * width + sx) * 4;
                    r_sum += rgba[idx + 0];
                    g_sum += rgba[idx + 1];
                    b_sum += rgba[idx + 2];
                }
            }
            int r = (r_sum + 2) >> 2;
            int g = (g_sum + 2) >> 2;
            int b = (b_sum + 2) >> 2;
            cb_plane[cy * cw + cx] = clampByte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            cr_plane[cy * cw + cx] = clampByte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }

    std::vector<uint8_t> a_stream;
    if (has_alpha) {
        a_stream = encode_plane(a_plane.data(), lw, lh, lw, width);
    }
    std::vector<uint8_t> y_stream = encode_plane(y_plane.data(), lw, lh, lw, width);
    std::vector<uint8_t> cr_stream = encode_plane(cr_plane.data(), cw, ch, cw, width >> 1);
    std::vector<uint8_t> cb_stream = encode_plane(cb_plane.data(), cw, ch, cw, width >> 1);

    std::vector<uint8_t> packet;
    if (has_alpha) {
        uint32_t color_offset = 4 + static_cast<uint32_t>(a_stream.size());
        uint8_t col_buf[4];
        writeLE32(col_buf, color_offset);
        packet.insert(packet.end(), col_buf, col_buf + 4);
        packet.insert(packet.end(), a_stream.begin(), a_stream.end());
    }

    uint32_t chroma_offset = 4 + static_cast<uint32_t>(y_stream.size());
    uint8_t chr_buf[4];
    writeLE32(chr_buf, chroma_offset);
    packet.insert(packet.end(), chr_buf, chr_buf + 4);
    packet.insert(packet.end(), y_stream.begin(), y_stream.end());
    packet.insert(packet.end(), cr_stream.begin(), cr_stream.end());
    packet.insert(packet.end(), cb_stream.begin(), cb_stream.end());

    uint32_t total_file_size = 52 + static_cast<uint32_t>(packet.size());
    std::vector<uint8_t> bik(total_file_size, 0);

    std::memcpy(bik.data(), "BIKi", 4);
    writeLE32(bik.data() + 4, total_file_size - 8);
    writeLE32(bik.data() + 8, 1);
    writeLE32(bik.data() + 12, static_cast<uint32_t>(packet.size()));
    writeLE32(bik.data() + 16, 1);
    writeLE32(bik.data() + 20, width);
    writeLE32(bik.data() + 24, height);
    writeLE32(bik.data() + 28, 30);
    writeLE32(bik.data() + 32, 1);
    writeLE32(bik.data() + 36, has_alpha ? 0x00100000U : 0U);
    writeLE32(bik.data() + 40, 0);
    writeLE32(bik.data() + 44, 53); // offset 52 | keyframe 1
    writeLE32(bik.data() + 48, total_file_size);

    std::memcpy(bik.data() + 52, packet.data(), packet.size());

    return {fmt_code, bik};
}

} // namespace grn
