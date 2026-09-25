/**
 * @file grn_types.h
 * @brief Core data structures and chunk tag constants for the GRN container format specification.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <memory>
#include <cmath>
#include <algorithm>
#include <utility>

namespace grn {

/**
 * @brief Section tag constants defining top-level regions in the GRN container.
 */
inline constexpr uint32_t T_SECTION_FOOTER                  = 0xca5e0101;
inline constexpr uint32_t T_SECTION_HEADER                  = 0xca5e0102;
inline constexpr uint32_t T_SECTION_PAYLOAD                 = 0xca5e0103;

/**
 * @brief Chunk tag constants defining container and leaf elements in the GRN binary hierarchy.
 */
inline constexpr uint32_t T_FILE_DIRECTORY                  = 0xca5e0000;
inline constexpr uint32_t T_STRING_TABLE                    = 0xca5e0200;
inline constexpr uint32_t T_TEXTURE_MAP                     = 0xca5e0301;
inline constexpr uint32_t T_TEXTURE_MAP_IMAGE               = 0xca5e0303;
inline constexpr uint32_t T_TEXTURE_SECTION                 = 0xca5e0304;
inline constexpr uint32_t T_TEXTURE_IMAGE_SECTION           = 0xca5e0305;
inline constexpr uint32_t T_SKELETON                        = 0xca5e0505;
inline constexpr uint32_t T_BONE                            = 0xca5e0506;
inline constexpr uint32_t T_SKELETON_SECTION                = 0xca5e0507;
inline constexpr uint32_t T_BONE_SECTION                    = 0xca5e0508;
inline constexpr uint32_t T_MESH                            = 0xca5e0601;
inline constexpr uint32_t T_MESH_SECTION                    = 0xca5e0602;
inline constexpr uint32_t T_MESH_VERTEX_SET                 = 0xca5e0603;
inline constexpr uint32_t T_MESH_VERTEX_SET_SECTION         = 0xca5e0604;
inline constexpr uint32_t T_MESH_WEIGHTS                    = 0xca5e0702;
inline constexpr uint32_t T_MESH_VERTICES                   = 0xca5e0801;
inline constexpr uint32_t T_MESH_NORMALS                    = 0xca5e0802;
inline constexpr uint32_t T_MESH_FIELD                      = 0xca5e0803;
inline constexpr uint32_t T_MESH_FIELD_SECTION              = 0xca5e0804;
inline constexpr uint32_t T_MESH_TRIANGLES                  = 0xca5e0901;
inline constexpr uint32_t T_HEADER_SPACER                   = 0xca5e0a01;
inline constexpr uint32_t T_TRANSFORM_CHANNEL               = 0xca5e0b00;
inline constexpr uint32_t T_TRANSFORM_CHANNEL_SECTION       = 0xca5e0b01;
inline constexpr uint32_t T_FORM                            = 0xca5e0c00;
inline constexpr uint32_t T_FORM_SECTION                    = 0xca5e0c01;
inline constexpr uint32_t T_FORM_BONE_CHANNELS              = 0xca5e0c02;
inline constexpr uint32_t T_FORM_MESH                       = 0xca5e0c03;
inline constexpr uint32_t T_FORM_MESH_INFO                  = 0xca5e0c04;
inline constexpr uint32_t T_FORM_CHANNEL_INFO               = 0xca5e0c05;
inline constexpr uint32_t T_FORM_MESH_SECTION               = 0xca5e0c06;
inline constexpr uint32_t T_FORM_SKELETON_SECTION           = 0xca5e0c07;
inline constexpr uint32_t T_FORM_SKELETON                   = 0xca5e0c08;
inline constexpr uint32_t T_FORM_MESH_BONE_SECTION          = 0xca5e0c09;
inline constexpr uint32_t T_FORM_MESH_BONE                  = 0xca5e0c0a;
inline constexpr uint32_t T_MATERIAL                        = 0xca5e0d00;
inline constexpr uint32_t T_MATERIAL_SECTION                = 0xca5e0d01;
inline constexpr uint32_t T_MATERIAL_SIMPLE_DIFFUSE_TEXTURE = 0xca5e0d03;
inline constexpr uint32_t T_MODEL                           = 0xca5e0e00;
inline constexpr uint32_t T_MODEL_SECTION                   = 0xca5e0e01;
inline constexpr uint32_t T_RENDER_PASS                     = 0xca5e0e02;
inline constexpr uint32_t T_RENDER_PASS_SOURCE_CONTAINER    = 0xca5e0e03;
inline constexpr uint32_t T_RENDER_PASS_SOURCE              = 0xca5e0e04;
inline constexpr uint32_t T_RENDER_PASS_TRIANGLES           = 0xca5e0e06;
inline constexpr uint32_t T_RENDER_PASS_SECTION             = 0xca5e0e07;
inline constexpr uint32_t T_DATA_EXTENSION                  = 0xca5e0f00;
inline constexpr uint32_t T_DATA_EXTENSION_PROPERTY         = 0xca5e0f01;
inline constexpr uint32_t T_DATA_EXTENSION_PROPERTY_VALUE   = 0xca5e0f02;
inline constexpr uint32_t T_DATA_EXTENSION_SECTION          = 0xca5e0f03;
inline constexpr uint32_t T_DATA_EXTENSION_REFERENCE        = 0xca5e0f04;
inline constexpr uint32_t T_DATA_EXTENSION_PROPERTY_SECTION  = 0xca5e0f05;
inline constexpr uint32_t T_DATA_EXTENSION_VALUE_SECTION     = 0xca5e0f06;
inline constexpr uint32_t T_ANIMATION                       = 0xca5e1200;
inline constexpr uint32_t T_ANIMATION_HEADER                = 0xca5e1201;
inline constexpr uint32_t T_ANIMATION_TRANSFORM_TRACK_SECTION= 0xca5e1203;
inline constexpr uint32_t T_ANIMATION_TRANSFORM_TRACK_KEYS  = 0xca5e1204;
inline constexpr uint32_t T_ANIMATION_SECTION               = 0xca5e1205;
inline constexpr uint32_t T_NULL_TERMINATOR                 = 0xca5effff;

enum class TextureFormatCode : uint32_t {
    RawRGBX     = 0, // 32-bit uncompressed RGBX (opaque)
    RawRGBA     = 1, // 32-bit uncompressed RGBA (with alpha)
    VTexOpaque  = 4, // Compressed video texture (opaque)
    VTexAlpha   = 5, // Compressed video texture (with alpha)
    RawRGB24    = 6, // 24-bit uncompressed RGB
    ExternalRef = 7, // External texture reference (.tga / .png)
    DXT1        = 8  // S3TC / DXT1 compressed
};

struct GrnChunkNode {
    uint32_t tag = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    std::vector<GrnChunkNode> children;
};

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct Mat4x4 {
    std::array<float, 16> m{};
    static Mat4x4 identity() {
        Mat4x4 res{};
        res.m[0] = res.m[5] = res.m[10] = res.m[15] = 1.0f;
        return res;
    }
};

struct Mat3x3 {
    std::array<float, 9> m{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    Mat3x3() = default;
    constexpr Mat3x3(const std::array<float, 9>& arr) : m(arr) {}
    constexpr Mat3x3(float m00, float m01, float m02,
                     float m10, float m11, float m12,
                     float m20, float m21, float m22)
        : m{m00, m01, m02, m10, m11, m12, m20, m21, m22} {}

    static Mat3x3 identity() {
        return Mat3x3{};
    }

    float operator()(int r, int c) const { return m[r * 3 + c]; }
    float& operator()(int r, int c) { return m[r * 3 + c]; }

    Mat3x3 operator*(const Mat3x3& o) const {
        Mat3x3 res{};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                res(r, c) = (*this)(r, 0) * o(0, c) +
                            (*this)(r, 1) * o(1, c) +
                            (*this)(r, 2) * o(2, c);
            }
        }
        return res;
    }

    Vec3 operator*(const Vec3& v) const {
        return {
            m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z
        };
    }

    Vec3 transform(const Vec3& v) const {
        return (*this) * v;
    }

    Mat3x3 transposed() const {
        return {
            m[0], m[3], m[6],
            m[1], m[4], m[7],
            m[2], m[5], m[8]
        };
    }

    Mat3x3 transpose() const {
        return transposed();
    }

    float determinant() const {
        return m[0] * (m[4] * m[8] - m[5] * m[7]) -
               m[1] * (m[3] * m[8] - m[5] * m[6]) +
               m[2] * (m[3] * m[7] - m[4] * m[6]);
    }

    Mat3x3 inverted() const {
        float det = determinant();
        if (std::abs(det) < 1e-12f) return Mat3x3::identity();
        float invDet = 1.0f / det;
        return {
            (m[4] * m[8] - m[5] * m[7]) * invDet,
            (m[2] * m[7] - m[1] * m[8]) * invDet,
            (m[1] * m[5] - m[2] * m[4]) * invDet,

            (m[5] * m[6] - m[3] * m[8]) * invDet,
            (m[0] * m[8] - m[2] * m[6]) * invDet,
            (m[2] * m[3] - m[0] * m[5]) * invDet,

            (m[3] * m[7] - m[4] * m[6]) * invDet,
            (m[1] * m[6] - m[0] * m[7]) * invDet,
            (m[0] * m[4] - m[1] * m[3]) * invDet
        };
    }
};

inline Mat3x3 quat_to_mat3(const Vec4& q) {
    float qx = q.x, qy = q.y, qz = q.z, qw = q.w;
    float lenSq = qx * qx + qy * qy + qz * qz + qw * qw;
    if (lenSq > 1e-8f) {
        float inv = 1.0f / std::sqrt(lenSq);
        qx *= inv; qy *= inv; qz *= inv; qw *= inv;
    } else {
        return Mat3x3::identity();
    }
    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    return {
        1.0f - 2.0f * (yy + zz), 2.0f * (xy - wz),        2.0f * (xz + wy),
        2.0f * (xy + wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz - wx),
        2.0f * (xz - wy),        2.0f * (yz + wx),        1.0f - 2.0f * (xx + yy)
    };
}

inline Vec4 mat3_to_quat(const Mat3x3& R) {
    float tr = R(0, 0) + R(1, 1) + R(2, 2);
    Vec4 q;
    if (tr > 0.0f) {
        float s = 0.5f / std::sqrt(tr + 1.0f);
        q.w = 0.25f / s;
        q.x = (R(2, 1) - R(1, 2)) * s;
        q.y = (R(0, 2) - R(2, 0)) * s;
        q.z = (R(1, 0) - R(0, 1)) * s;
    } else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
        float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + R(0, 0) - R(1, 1) - R(2, 2)));
        if (s > 1e-8f) {
            q.w = (R(2, 1) - R(1, 2)) / s;
            q.x = 0.25f * s;
            q.y = (R(0, 1) + R(1, 0)) / s;
            q.z = (R(0, 2) + R(2, 0)) / s;
        } else {
            q = {1.0f, 0.0f, 0.0f, 0.0f};
        }
    } else if (R(1, 1) > R(2, 2)) {
        float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + R(1, 1) - R(0, 0) - R(2, 2)));
        if (s > 1e-8f) {
            q.w = (R(0, 2) - R(2, 0)) / s;
            q.x = (R(0, 1) + R(1, 0)) / s;
            q.y = 0.25f * s;
            q.z = (R(1, 2) + R(2, 1)) / s;
        } else {
            q = {0.0f, 1.0f, 0.0f, 0.0f};
        }
    } else {
        float s = 2.0f * std::sqrt(std::max(0.0f, 1.0f + R(2, 2) - R(0, 0) - R(1, 1)));
        if (s > 1e-8f) {
            q.w = (R(1, 0) - R(0, 1)) / s;
            q.x = (R(0, 2) + R(2, 0)) / s;
            q.y = (R(1, 2) + R(2, 1)) / s;
            q.z = 0.25f * s;
        } else {
            q = {0.0f, 0.0f, 1.0f, 0.0f};
        }
    }
    float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len > 1e-8f) {
        q.x /= len; q.y /= len; q.z /= len; q.w /= len;
    } else {
        q = {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return q;
}

inline void jacobi_sym3(const Mat3x3& A_in, Vec3& out_eigvals, Mat3x3& out_V) {
    Mat3x3 A = A_in;
    // Symmetrize input
    A(0, 1) = A(1, 0) = 0.5f * (A(0, 1) + A(1, 0));
    A(0, 2) = A(2, 0) = 0.5f * (A(0, 2) + A(2, 0));
    A(1, 2) = A(2, 1) = 0.5f * (A(1, 2) + A(2, 1));

    out_V = Mat3x3::identity();

    for (int iter = 0; iter < 20; ++iter) {
        float off_max = std::max({std::abs(A(0, 1)), std::abs(A(0, 2)), std::abs(A(1, 2))});
        if (off_max < 1e-7f) break;

        const std::pair<int, int> pairs[3] = {{0, 1}, {0, 2}, {1, 2}};
        for (const auto& [p, q] : pairs) {
            if (std::abs(A(p, q)) < 1e-9f) continue;
            float tau = (A(q, q) - A(p, p)) / (2.0f * A(p, q));
            float t = (tau >= 0.0f) ? (1.0f / (tau + std::sqrt(1.0f + tau * tau)))
                                    : (-1.0f / (-tau + std::sqrt(1.0f + tau * tau)));
            float c = 1.0f / std::sqrt(1.0f + t * t);
            float s = t * c;

            float App = A(p, p), Aqq = A(q, q), Apq = A(p, q);
            A(p, p) = c * c * App - 2.0f * s * c * Apq + s * s * Aqq;
            A(q, q) = s * s * App + 2.0f * s * c * Apq + c * c * Aqq;
            A(p, q) = A(q, p) = 0.0f;

            for (int r = 0; r < 3; ++r) {
                if (r != p && r != q) {
                    float Apr = A(p, r), Aqr = A(q, r);
                    A(p, r) = A(r, p) = c * Apr - s * Aqr;
                    A(q, r) = A(r, q) = s * Apr + c * Aqr;
                }
            }
            for (int r = 0; r < 3; ++r) {
                float Vrp = out_V(r, p), Vrq = out_V(r, q);
                out_V(r, p) = c * Vrp - s * Vrq;
                out_V(r, q) = s * Vrp + c * Vrq;
            }
        }
    }
    out_eigvals = {A(0, 0), A(1, 1), A(2, 2)};
    if (out_V.determinant() < 0.0f) {
        out_V(0, 0) = -out_V(0, 0);
        out_V(1, 0) = -out_V(1, 0);
        out_V(2, 0) = -out_V(2, 0);
    }
}


struct GrnBone {
    int32_t index = 0;
    std::string name;
    int32_t parent_index = -1;
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // Quaternion (x, y, z, w)
    std::array<float, 9> scale_3x3{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    Mat4x4 inverse_bind_matrix = Mat4x4::identity();
};

struct VertexWeight {
    std::vector<int32_t> bone_indices;
    std::vector<float> bone_weights;
};

/**
 * @brief Represents a sub-mesh triangle group or render pass with a dedicated material.
 */
struct GrnTriGroup {
    int32_t material_index = -1;
    std::string material_name;
    std::vector<std::array<uint32_t, 3>> faces;        ///< Vertex indices for triangles in this group
    std::vector<std::array<uint32_t, 3>> face_uvs;     ///< UV indices for triangles in this group
    std::vector<std::array<uint32_t, 3>> face_normals; ///< Normal indices for triangles in this group
};

struct GrnMesh {
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<std::array<uint32_t, 3>> faces; // Triangle vertex indices
    std::vector<std::array<uint32_t, 3>> face_uvs;
    std::vector<std::array<uint32_t, 3>> face_normals;
    std::vector<VertexWeight> weights;
    int32_t material_index = -1;
    std::string material_name;
    std::vector<int32_t> bone_index_map;
    uint32_t bone_count = 0;
    std::vector<GrnTriGroup> tri_groups; ///< Render passes / material sub-groups
};

struct GrnTexture {
    std::string name;
    std::string file_name;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format_code = 0;
    std::string format_str = "raw"; // "vtex", "dxt1", "raw", "external"
    std::vector<uint8_t> raw_blob;  // Preserved raw / compressed stream
    std::vector<uint8_t> decoded_rgba; // 32-bit decoded pixels
    bool has_alpha = false;
    bool is_external = false;
    bool is_placeholder = false;
};

struct GrnMaterial {
    std::string name;
    int32_t diffuse_texture_index = -1;
};

struct AnimChannel {
    std::string node_name;
    int32_t node_index = -1;
    int32_t channel_id = 0;
    std::string target_path; // "translation", "rotation", "scale"
    std::vector<float> timestamps;
    std::vector<float> values; // 3 floats for pos/scale, 4 floats for rot
};

struct AnimTrack {
    int32_t channel_id = 0;
    std::string bone_name;
    std::string format = "split"; // "split" or "interleaved"
    int32_t position_interp_mode = 1;
    int32_t quaternion_interp_mode = 1;
    int32_t scale_shear_interp_mode = 1;
    std::vector<float> translation_times;
    std::vector<float> rotation_times;
    std::vector<float> scale_shear_times;
    std::vector<Vec3> translations;
    std::vector<Vec4> rotations;
    std::vector<std::array<float, 9>> scale_shears;
    std::vector<float> times; // For interleaved format
};

struct GrnAnimation {
    std::string name = "Animation";
    float duration = 0.0f;
    float fps = 30.0f;
    std::vector<AnimTrack> tracks;
    std::vector<AnimChannel> channels;
};

struct GrnModel {
    std::vector<uint8_t> raw_file_bytes;
    std::vector<GrnChunkNode> root_nodes;
    std::vector<GrnBone> bones;
    std::vector<GrnMesh> meshes;
    std::vector<GrnTexture> textures;
    std::vector<GrnMaterial> materials;
    std::vector<GrnAnimation> animations;
};

} // namespace grn
