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
