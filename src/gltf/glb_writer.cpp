#include "glb_writer.h"
#include "../codecs/tga_png.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <set>

using json = nlohmann::json;

namespace grn {

static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string ret;
    ret.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (static_cast<uint32_t>(data[i]) << 16);
        if (i + 1 < len) b |= (static_cast<uint32_t>(data[i + 1]) << 8);
        if (i + 2 < len) b |= static_cast<uint32_t>(data[i + 2]);

        ret.push_back(b64_chars[(b >> 18) & 0x3F]);
        ret.push_back(b64_chars[(b >> 12) & 0x3F]);
        ret.push_back((i + 1 < len) ? b64_chars[(b >> 6) & 0x3F] : '=');
        ret.push_back((i + 2 < len) ? b64_chars[b & 0x3F] : '=');
    }
    return ret;
}

static inline void padTo4(std::vector<uint8_t>& buf) {
    while (buf.size() % 4 != 0) {
        buf.push_back(0);
    }
}

static inline Vec4 quat_mul(const Vec4& a, const Vec4& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

constexpr float kInvSqrt2 = 0.7071067811865476f;
const Vec4 q_yup_conv{-kInvSqrt2, 0.0f, 0.0f, kInvSqrt2};

static std::string to_valid_utf8(const std::string& str) {
    std::string out;
    out.reserve(str.size() * 2);
    size_t i = 0;
    while (i < str.size()) {
        uint8_t c = static_cast<uint8_t>(str[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size() && (static_cast<uint8_t>(str[i+1]) & 0xC0) == 0x80) {
            out.push_back(str[i]);
            out.push_back(str[i+1]);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < str.size() &&
                   (static_cast<uint8_t>(str[i+1]) & 0xC0) == 0x80 &&
                   (static_cast<uint8_t>(str[i+2]) & 0xC0) == 0x80) {
            out.push_back(str[i]);
            out.push_back(str[i+1]);
            out.push_back(str[i+2]);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < str.size() &&
                   (static_cast<uint8_t>(str[i+1]) & 0xC0) == 0x80 &&
                   (static_cast<uint8_t>(str[i+2]) & 0xC0) == 0x80 &&
                   (static_cast<uint8_t>(str[i+3]) & 0xC0) == 0x80) {
            out.push_back(str[i]);
            out.push_back(str[i+1]);
            out.push_back(str[i+2]);
            out.push_back(str[i+3]);
            i += 4;
        } else {
            // Convert single byte (e.g. CP-1252 / ISO-8859-1) to 2-byte UTF-8
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            i++;
        }
    }
    return out;
}

static Mat4x4 invert_mat4(const Mat4x4& mat) {
    const float* m = mat.m.data();
    float inv[16];
    inv[0] = m[5]  * m[10] * m[15] - m[5]  * m[11] * m[14] - m[9]  * m[6]  * m[15] + m[9]  * m[7]  * m[14] + m[13] * m[6]  * m[11] - m[13] * m[7]  * m[10];
    inv[4] = -m[4]  * m[10] * m[15] + m[4]  * m[11] * m[14] + m[8]  * m[6]  * m[15] - m[8]  * m[7]  * m[14] - m[12] * m[6]  * m[11] + m[12] * m[7]  * m[10];
    inv[8] = m[4]  * m[9]  * m[15] - m[4]  * m[11] * m[13] - m[8]  * m[5]  * m[15] + m[8]  * m[7]  * m[13] + m[12] * m[5]  * m[11] - m[12] * m[7]  * m[9];
    inv[12] = -m[4]  * m[9]  * m[14] + m[4]  * m[10] * m[13] + m[8]  * m[5]  * m[14] - m[8]  * m[6]  * m[13] - m[12] * m[5]  * m[10] + m[12] * m[6]  * m[9];
    inv[1] = -m[1]  * m[10] * m[15] + m[1]  * m[11] * m[14] + m[9]  * m[2]  * m[15] - m[9]  * m[3]  * m[14] - m[13] * m[2]  * m[11] + m[13] * m[3]  * m[10];
    inv[5] = m[0]  * m[10] * m[15] - m[0]  * m[11] * m[14] - m[8]  * m[2]  * m[15] + m[8]  * m[3]  * m[14] + m[12] * m[2]  * m[11] - m[12] * m[3]  * m[10];
    inv[9] = -m[0]  * m[9]  * m[15] + m[0]  * m[11] * m[13] + m[8]  * m[1]  * m[15] - m[8]  * m[3]  * m[13] - m[12] * m[1]  * m[11] + m[12] * m[3]  * m[9];
    inv[13] = m[0]  * m[9]  * m[14] - m[0]  * m[10] * m[13] - m[8]  * m[1]  * m[14] + m[8]  * m[2]  * m[13] + m[12] * m[1]  * m[10] - m[12] * m[2]  * m[9];
    inv[2] = m[1]  * m[6]  * m[15] - m[1]  * m[7]  * m[14] - m[5]  * m[2]  * m[15] + m[5]  * m[3]  * m[14] + m[13] * m[2]  * m[7]  - m[13] * m[3]  * m[6];
    inv[6] = -m[0]  * m[6]  * m[15] + m[0]  * m[7]  * m[14] + m[4]  * m[2]  * m[15] - m[4]  * m[3]  * m[14] - m[12] * m[2]  * m[7]  + m[12] * m[3]  * m[6];
    inv[10] = m[0]  * m[5]  * m[15] - m[0]  * m[7]  * m[13] - m[4]  * m[1]  * m[15] + m[4]  * m[3]  * m[13] + m[12] * m[1]  * m[7]  - m[12] * m[3]  * m[5];
    inv[14] = -m[0]  * m[5]  * m[14] + m[0]  * m[6]  * m[13] + m[4]  * m[1]  * m[14] - m[4]  * m[2]  * m[13] - m[12] * m[1]  * m[6]  + m[12] * m[2]  * m[5];
    inv[3] = -m[1]  * m[6]  * m[11] + m[1]  * m[7]  * m[10] + m[5]  * m[2]  * m[11] - m[5]  * m[3]  * m[10] - m[9]  * m[2]  * m[7]  + m[9]  * m[3]  * m[6];
    inv[7] = m[0]  * m[6]  * m[11] - m[0]  * m[7]  * m[10] - m[4]  * m[2]  * m[11] + m[4]  * m[3]  * m[10] + m[8]  * m[2]  * m[7]  - m[8]  * m[3]  * m[6];
    inv[11] = -m[0]  * m[5]  * m[11] + m[0]  * m[7]  * m[9]  + m[4]  * m[1]  * m[11] - m[4]  * m[3]  * m[9]  - m[8]  * m[1]  * m[7]  + m[8]  * m[3]  * m[5];
    inv[15] = m[0]  * m[5]  * m[10] - m[0]  * m[6]  * m[9]  - m[4]  * m[1]  * m[10] + m[4]  * m[2]  * m[9]  + m[8]  * m[1]  * m[6]  - m[8]  * m[2]  * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(det) < 1e-8f) return Mat4x4::identity();

    float invdet = 1.0f / det;
    Mat4x4 res{};
    for (int i = 0; i < 16; ++i) res.m[i] = inv[i] * invdet;
    res.m[3] = 0.0f;
    res.m[7] = 0.0f;
    res.m[11] = 0.0f;
    res.m[15] = 1.0f;
    return res;
}

std::vector<uint8_t> export_grn_to_glb_memory(const GrnModel& model, const GlbExportOptions& options) {
    json gltf;
    gltf["asset"] = {
        {"version", "2.0"},
        {"generator", "open-grn grn_converter C++20"}
    };
    gltf["scene"] = 0;
    gltf["scenes"] = json::array({{{"nodes", json::array()}}});

    std::vector<uint8_t> bin_buffer;
    std::vector<json> buffer_views;
    std::vector<json> accessors;

    auto add_buffer_view = [&](const void* data, size_t byte_length, uint32_t target = 0) -> uint32_t {
        padTo4(bin_buffer);
        size_t offset = bin_buffer.size();
        const auto* ptr = reinterpret_cast<const uint8_t*>(data);
        bin_buffer.insert(bin_buffer.end(), ptr, ptr + byte_length);

        json bv = {
            {"buffer", 0},
            {"byteOffset", offset},
            {"byteLength", byte_length}
        };
        if (target != 0) {
            bv["target"] = target;
        }
        buffer_views.push_back(bv);
        return static_cast<uint32_t>(buffer_views.size() - 1);
    };

    // Textures & Materials
    std::vector<uint32_t> tex_indices;
    for (size_t ti = 0; ti < model.textures.size(); ++ti) {
        const auto& tex = model.textures[ti];
        json img_obj;
        img_obj["name"] = to_valid_utf8(tex.name);

        // Extras for 1:1 roundtrip
        json extras;
        extras["grn_format_code"] = tex.format_code;
        if (!tex.raw_blob.empty()) {
            extras["grn_raw_blob"] = base64_encode(tex.raw_blob.data(), tex.raw_blob.size());
        }
        img_obj["extras"] = extras;

        // Either embed or save loose
        if (options.embed_textures) {
            std::vector<uint8_t> png_data;
            if (!tex.decoded_rgba.empty() && tex.width > 0 && tex.height > 0) {
                png_data = encode_png_memory(tex.decoded_rgba.data(), tex.width, tex.height);
            } else {
                // Standalone valid 2x2 placeholder PNG for glTF compliance in GLB
                uint8_t placeholder[16] = {
                    220, 220, 220, 255, 180, 180, 180, 255,
                    180, 180, 180, 255, 220, 220, 220, 255
                };
                png_data = encode_png_memory(placeholder, 2, 2);
            }
            uint32_t bv_idx = add_buffer_view(png_data.data(), png_data.size());
            img_obj["bufferView"] = bv_idx;
            img_obj["mimeType"] = "image/png";
        } else if (!options.embed_textures && !tex.decoded_rgba.empty()) {
            std::string stem = options.model_stem.empty() ? tex.name : options.model_stem;
            if (model.textures.size() > 1 && !options.model_stem.empty()) {
                stem += "_" + tex.name;
            }
            std::string ext = (options.loose_texture_format == "tga") ? ".tga" : ".png";
            std::string loose_name = stem + ext;
            img_obj["uri"] = loose_name;

            // Write loose file to output directory if specified
            if (!options.loose_texture_dir.empty()) {
                std::filesystem::path tex_path = options.loose_texture_dir / loose_name;
                std::error_code ec;
                std::filesystem::create_directories(tex_path.parent_path(), ec);
                if (options.loose_texture_format == "tga") {
                    save_image_tga(tex_path, tex.decoded_rgba.data(), tex.width, tex.height);
                } else {
                    save_image_png(tex_path, tex.decoded_rgba.data(), tex.width, tex.height);
                }
            }
        } else {
            std::string raw_name = tex.file_name.empty() ? (tex.name + ".tga") : tex.file_name;
            img_obj["uri"] = std::filesystem::path(raw_name).filename().string();
        }

        uint32_t img_idx = static_cast<uint32_t>(gltf["images"].size());
        gltf["images"].push_back(img_obj);

        json tex_obj = {{"source", img_idx}};
        uint32_t tex_idx = static_cast<uint32_t>(gltf["textures"].size());
        gltf["textures"].push_back(tex_obj);
        tex_indices.push_back(tex_idx);
    }

    // Materials
    for (size_t mi = 0; mi < model.materials.size(); ++mi) {
        const auto& mat = model.materials[mi];
        json mat_obj = {{"name", to_valid_utf8(mat.name)}};
        json pbr = {{"metallicFactor", 0.0f}, {"roughnessFactor", 0.9f}};

        if (mat.diffuse_texture_index >= 0 && static_cast<size_t>(mat.diffuse_texture_index) < tex_indices.size()) {
            pbr["baseColorTexture"] = {{"index", tex_indices[mat.diffuse_texture_index]}};
            if (model.textures[mat.diffuse_texture_index].has_alpha) {
                mat_obj["alphaMode"] = "MASK";
                mat_obj["alphaCutoff"] = 0.5f;
            }
        }
        mat_obj["pbrMetallicRoughness"] = pbr;
        mat_obj["doubleSided"] = true;
        gltf["materials"].push_back(mat_obj);
    }

    // Nodes and Scene hierarchy
    std::vector<uint32_t> root_node_indices;

    // Bones / Skeleton
    std::vector<uint32_t> joint_indices;
    std::vector<bool> is_root_bone(model.bones.size(), false);
    std::vector<Vec3> bone_node_trans(model.bones.size());
    std::vector<Vec4> bone_node_rot(model.bones.size());
    std::vector<Vec3> bone_node_scale(model.bones.size(), {1.0f, 1.0f, 1.0f});

    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
        const auto& b = model.bones[bi];
        bool is_root = (b.parent_index < 0 || b.parent_index == static_cast<int32_t>(bi) || static_cast<size_t>(b.parent_index) >= model.bones.size());
        is_root_bone[bi] = is_root;

        float qx = b.rotation.x, qy = b.rotation.y, qz = b.rotation.z, qw = b.rotation.w;
        float tx = b.position.x, ty = b.position.y, tz = b.position.z;

        if (options.y_up && is_root) {
            Vec4 qr = quat_mul(q_yup_conv, {qx, qy, qz, qw});
            qx = qr.x; qy = qr.y; qz = qr.z; qw = qr.w;
            float n_tx = tx;
            float n_ty = tz;
            float n_tz = -ty;
            tx = n_tx; ty = n_ty; tz = n_tz;
        }

        float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (qlen > 1e-6f) {
            qx /= qlen; qy /= qlen; qz /= qlen; qw /= qlen;
        } else {
            qx = 0.0f; qy = 0.0f; qz = 0.0f; qw = 1.0f;
        }

        float sx = b.scale_3x3[0];
        float sy = b.scale_3x3[4];
        float sz = b.scale_3x3[8];

        if (options.y_up && is_root) {
            float n_sy = sz;
            float n_sz = sy;
            sy = n_sy;
            sz = n_sz;
        }

        if (std::abs(sx - 1.0f) < 1e-4f) sx = 1.0f;
        if (std::abs(sy - 1.0f) < 1e-4f) sy = 1.0f;
        if (std::abs(sz - 1.0f) < 1e-4f) sz = 1.0f;
        if (std::abs(sx - (-1.0f)) < 1e-4f) sx = -1.0f;
        if (std::abs(sy - (-1.0f)) < 1e-4f) sy = -1.0f;
        if (std::abs(sz - (-1.0f)) < 1e-4f) sz = -1.0f;

        bone_node_trans[bi] = {tx, ty, tz};
        bone_node_rot[bi] = {qx, qy, qz, qw};
        bone_node_scale[bi] = {sx, sy, sz};

        json node = {
            {"name", to_valid_utf8(b.name)},
            {"translation", {tx, ty, tz}},
            {"rotation", {qx, qy, qz, qw}}
        };
        if (std::abs(sx - 1.0f) > 1e-5f || std::abs(sy - 1.0f) > 1e-5f || std::abs(sz - 1.0f) > 1e-5f) {
            node["scale"] = {sx, sy, sz};
        }
        uint32_t node_idx = static_cast<uint32_t>(gltf["nodes"].size());
        gltf["nodes"].push_back(node);
        joint_indices.push_back(node_idx);

        if (is_root) {
            root_node_indices.push_back(node_idx);
        }
    }

    // Connect bone children
    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
        int32_t parent = model.bones[bi].parent_index;
        if (parent >= 0 && static_cast<size_t>(parent) < model.bones.size() && parent != static_cast<int32_t>(bi)) {
            gltf["nodes"][joint_indices[parent]]["children"].push_back(joint_indices[bi]);
        }
    }

    // Skins & Inverse Bind Matrices
    if (!model.bones.empty()) {
        std::vector<Mat4x4> world_matrices(model.bones.size(), Mat4x4::identity());
        std::vector<float> ibm_floats;
        ibm_floats.reserve(model.bones.size() * 16);

        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            const auto& b = model.bones[bi];
            float qx = bone_node_rot[bi].x, qy = bone_node_rot[bi].y, qz = bone_node_rot[bi].z, qw = bone_node_rot[bi].w;
            float tx = bone_node_trans[bi].x, ty = bone_node_trans[bi].y, tz = bone_node_trans[bi].z;
            float sx = bone_node_scale[bi].x, sy = bone_node_scale[bi].y, sz = bone_node_scale[bi].z;

            float xx = qx * qx, yy = qy * qy, zz = qz * qz;
            float xy = qx * qy, xz = qx * qz, yz = qy * qz;
            float wx = qw * qx, wy = qw * qy, wz = qw * qz;

            Mat4x4 local_m{};
            local_m.m[0] = (1.0f - 2.0f * (yy + zz)) * sx;
            local_m.m[1] = (2.0f * (xy + wz)) * sx;
            local_m.m[2] = (2.0f * (xz - wy)) * sx;
            local_m.m[3] = 0.0f;

            local_m.m[4] = (2.0f * (xy - wz)) * sy;
            local_m.m[5] = (1.0f - 2.0f * (xx + zz)) * sy;
            local_m.m[6] = (2.0f * (yz + wx)) * sy;
            local_m.m[7] = 0.0f;

            local_m.m[8] = (2.0f * (xz + wy)) * sz;
            local_m.m[9] = (2.0f * (yz - wx)) * sz;
            local_m.m[10] = (1.0f - 2.0f * (xx + yy)) * sz;
            local_m.m[11] = 0.0f;

            local_m.m[12] = tx;
            local_m.m[13] = ty;
            local_m.m[14] = tz;
            local_m.m[15] = 1.0f;

            if (!is_root_bone[bi] && b.parent_index >= 0 && static_cast<size_t>(b.parent_index) < bi) {
                const auto& pw = world_matrices[b.parent_index];
                Mat4x4 w_mat{};
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        float sum = 0.0f;
                        for (int k = 0; k < 4; ++k) {
                            sum += pw.m[k * 4 + r] * local_m.m[c * 4 + k];
                        }
                        w_mat.m[c * 4 + r] = sum;
                    }
                }
                world_matrices[bi] = w_mat;
            } else {
                world_matrices[bi] = local_m;
            }

            Mat4x4 inv = invert_mat4(world_matrices[bi]);
            inv.m[3] = 0.0f;
            inv.m[7] = 0.0f;
            inv.m[11] = 0.0f;
            inv.m[15] = 1.0f;
            for (int k = 0; k < 16; ++k) ibm_floats.push_back(inv.m[k]);
        }

        uint32_t ibm_bv = add_buffer_view(ibm_floats.data(), ibm_floats.size() * sizeof(float));
        uint32_t ibm_acc = static_cast<uint32_t>(accessors.size());
        accessors.push_back({
            {"bufferView", ibm_bv}, {"byteOffset", 0}, {"componentType", 5126},
            {"count", model.bones.size()}, {"type", "MAT4"}
        });

        json skin_obj = {
            {"name", "Armature"},
            {"inverseBindMatrices", ibm_acc},
            {"joints", joint_indices}
        };
        if (!joint_indices.empty()) {
            skin_obj["skeleton"] = joint_indices[0];
        }
        gltf["skins"] = json::array({skin_obj});
    }

    // Meshes and Primitives
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const auto& m = model.meshes[mi];
        if (m.vertices.empty()) continue;

        auto groups = m.tri_groups;
        bool has_faces = false;
        for (const auto& g : groups) {
            if (!g.faces.empty()) { has_faces = true; break; }
        }
        if (!has_faces && !m.faces.empty()) {
            groups.clear();
            GrnTriGroup g;
            g.material_index = m.material_index;
            g.material_name = m.material_name;
            g.faces = m.faces;
            g.face_uvs = m.face_uvs;
            g.face_normals = m.face_normals;
            groups.push_back(std::move(g));
        }

        json mesh_obj = {{"name", to_valid_utf8(m.name)}, {"primitives", json::array()}};
        if (!m.bone_index_map.empty()) {
            mesh_obj["extras"] = {{"grn_bone_index_map", m.bone_index_map}};
        }

        for (const auto& group : groups) {
            if (group.faces.empty()) continue;

            struct VertexKey {
                uint32_t v;
                uint32_t n;
                uint32_t u;
                bool operator==(const VertexKey& o) const {
                    return v == o.v && n == o.n && u == o.u;
                }
            };
            struct VertexKeyHash {
                size_t operator()(const VertexKey& k) const {
                    return (static_cast<size_t>(k.v) * 73856093) ^
                           (static_cast<size_t>(k.n) * 19349663) ^
                           (static_cast<size_t>(k.u) * 83492791);
                }
            };

            std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vmap;
            std::vector<float> pos_buf;
            std::vector<float> norm_buf;
            std::vector<float> uv_buf;
            std::vector<uint16_t> joint_buf;
            std::vector<float> weight_buf;
            std::vector<uint32_t> idx_buf;

            Vec3 min_pos{1e9f, 1e9f, 1e9f};
            Vec3 max_pos{-1e9f, -1e9f, -1e9f};

            bool has_normals = !m.normals.empty();
            bool has_uvs = !m.uvs.empty();
            bool has_weights = !m.weights.empty();

            for (size_t f = 0; f < group.faces.size(); ++f) {
                const auto& tri_v = group.faces[f];
                std::array<uint32_t, 3> tri_u = (f < group.face_uvs.size()) ? group.face_uvs[f] : tri_v;
                std::array<uint32_t, 3> tri_n = (f < group.face_normals.size()) ? group.face_normals[f] : tri_v;

                for (int k = 0; k < 3; ++k) {
                    uint32_t vi = tri_v[k];
                    uint32_t ui = (has_uvs && tri_u[k] < m.uvs.size()) ? tri_u[k] : (vi < m.uvs.size() ? vi : 0);
                    uint32_t ni = (has_normals && tri_n[k] < m.normals.size()) ? tri_n[k] : (vi < m.normals.size() ? vi : 0);

                    VertexKey key{vi, ni, ui};
                    auto it = vmap.find(key);
                    if (it != vmap.end()) {
                        idx_buf.push_back(it->second);
                        continue;
                    }

                    uint32_t new_idx = static_cast<uint32_t>(pos_buf.size() / 3);
                    vmap[key] = new_idx;
                    idx_buf.push_back(new_idx);

                    // Position
                    Vec3 pos = (vi < m.vertices.size()) ? m.vertices[vi] : Vec3{0, 0, 0};
                    float vx = std::isfinite(pos.x) ? pos.x : 0.0f;
                    float vy = options.y_up ? (std::isfinite(pos.z) ? pos.z : 0.0f) : (std::isfinite(pos.y) ? pos.y : 0.0f);
                    float vz = options.y_up ? (std::isfinite(pos.y) ? -pos.y : 0.0f) : (std::isfinite(pos.z) ? pos.z : 0.0f);
                    pos_buf.push_back(vx); pos_buf.push_back(vy); pos_buf.push_back(vz);
                    min_pos.x = std::min(min_pos.x, vx); max_pos.x = std::max(max_pos.x, vx);
                    min_pos.y = std::min(min_pos.y, vy); max_pos.y = std::max(max_pos.y, vy);
                    min_pos.z = std::min(min_pos.z, vz); max_pos.z = std::max(max_pos.z, vz);

                    // Normal
                    if (has_normals && ni < m.normals.size()) {
                        Vec3 n = m.normals[ni];
                        float nx = std::isfinite(n.x) ? n.x : 0.0f;
                        float ny = options.y_up ? (std::isfinite(n.z) ? n.z : 1.0f) : (std::isfinite(n.y) ? n.y : 1.0f);
                        float nz = options.y_up ? (std::isfinite(n.y) ? -n.y : 0.0f) : (std::isfinite(n.z) ? n.z : 0.0f);
                        float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
                        if (nlen > 1e-6f) { nx /= nlen; ny /= nlen; nz /= nlen; }
                        else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
                        norm_buf.push_back(nx); norm_buf.push_back(ny); norm_buf.push_back(nz);
                    } else if (has_normals) {
                        norm_buf.push_back(0.0f); norm_buf.push_back(1.0f); norm_buf.push_back(0.0f);
                    }

                    // UV
                    if (has_uvs && ui < m.uvs.size()) {
                        float u = std::isfinite(m.uvs[ui].x) ? m.uvs[ui].x : 0.0f;
                        float v = std::isfinite(m.uvs[ui].y) ? m.uvs[ui].y : 0.0f;
                        uv_buf.push_back(u);
                        uv_buf.push_back(v);
                    } else if (has_uvs) {
                        uv_buf.push_back(0.0f); uv_buf.push_back(0.0f);
                    }

                    // Skinning weights & joints
                    if (has_weights) {
                        uint16_t j[4] = {0, 0, 0, 0};
                        float w[4] = {1.0f, 0.0f, 0.0f, 0.0f};

                        if (vi < m.weights.size()) {
                            const auto& vw = m.weights[vi];
                            struct BW { uint16_t b; float w; };
                            std::vector<BW> pairs;
                            for (size_t b_idx = 0; b_idx < vw.bone_indices.size(); ++b_idx) {
                                int32_t local_b = vw.bone_indices[b_idx];
                                int32_t skel_b = (local_b >= 0 && static_cast<size_t>(local_b) < m.bone_index_map.size())
                                                 ? m.bone_index_map[local_b]
                                                 : local_b;
                                float weight_val = (b_idx < vw.bone_weights.size()) ? vw.bone_weights[b_idx] : 0.0f;
                                if (skel_b >= 0 && static_cast<size_t>(skel_b) < model.bones.size() && weight_val > 0.0f) {
                                    pairs.push_back({static_cast<uint16_t>(skel_b), weight_val});
                                }
                            }
                            std::sort(pairs.begin(), pairs.end(), [](const BW& a, const BW& b){ return a.w > b.w; });
                            float total_w = 0.0f;
                            size_t count_influences = std::min(pairs.size(), size_t(4));
                            for (size_t c = 0; c < count_influences; ++c) total_w += pairs[c].w;
                            if (total_w > 1e-6f) {
                                for (size_t c = 0; c < count_influences; ++c) {
                                    j[c] = pairs[c].b;
                                    w[c] = pairs[c].w / total_w;
                                }
                                for (size_t c = count_influences; c < 4; ++c) {
                                    j[c] = 0;
                                    w[c] = 0.0f;
                                }
                            }
                        }
                        for (int l = 0; l < 4; ++l) joint_buf.push_back(j[l]);
                        for (int l = 0; l < 4; ++l) weight_buf.push_back(w[l]);
                    }
                }
            }

            uint32_t vert_count = static_cast<uint32_t>(pos_buf.size() / 3);
            uint32_t pos_bv = add_buffer_view(pos_buf.data(), pos_buf.size() * sizeof(float), 34962);
            uint32_t pos_acc = static_cast<uint32_t>(accessors.size());
            accessors.push_back({
                {"bufferView", pos_bv}, {"byteOffset", 0}, {"componentType", 5126},
                {"count", vert_count}, {"type", "VEC3"},
                {"min", {min_pos.x, min_pos.y, min_pos.z}},
                {"max", {max_pos.x, max_pos.y, max_pos.z}}
            });

            json prim_attrs = {{"POSITION", pos_acc}};

            if (has_normals && !norm_buf.empty()) {
                uint32_t norm_bv = add_buffer_view(norm_buf.data(), norm_buf.size() * sizeof(float), 34962);
                uint32_t norm_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", norm_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC3"}
                });
                prim_attrs["NORMAL"] = norm_acc;
            }

            if (has_uvs && !uv_buf.empty()) {
                uint32_t uv_bv = add_buffer_view(uv_buf.data(), uv_buf.size() * sizeof(float), 34962);
                uint32_t uv_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", uv_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC2"}
                });
                prim_attrs["TEXCOORD_0"] = uv_acc;
            } else if (!tex_indices.empty()) {
                std::vector<float> default_uvs(vert_count * 2, 0.0f);
                uint32_t uv_bv = add_buffer_view(default_uvs.data(), default_uvs.size() * sizeof(float), 34962);
                uint32_t uv_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", uv_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC2"}
                });
                prim_attrs["TEXCOORD_0"] = uv_acc;
            }

            if (has_weights && !joint_buf.empty() && !weight_buf.empty()) {
                uint32_t j_bv = add_buffer_view(joint_buf.data(), joint_buf.size() * sizeof(uint16_t), 34962);
                uint32_t j_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", j_bv}, {"byteOffset", 0}, {"componentType", 5123},
                    {"count", vert_count}, {"type", "VEC4"}
                });
                prim_attrs["JOINTS_0"] = j_acc;

                uint32_t w_bv = add_buffer_view(weight_buf.data(), weight_buf.size() * sizeof(float), 34962);
                uint32_t w_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", w_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC4"}
                });
                prim_attrs["WEIGHTS_0"] = w_acc;
            }

            uint32_t idx_bv = add_buffer_view(idx_buf.data(), idx_buf.size() * sizeof(uint32_t), 34963);
            uint32_t idx_acc = static_cast<uint32_t>(accessors.size());
            accessors.push_back({
                {"bufferView", idx_bv}, {"byteOffset", 0}, {"componentType", 5125},
                {"count", idx_buf.size()}, {"type", "SCALAR"}
            });

            json prim = {
                {"attributes", prim_attrs},
                {"indices", idx_acc}
            };
            if (group.material_index >= 0 && static_cast<size_t>(group.material_index) < model.materials.size()) {
                prim["material"] = group.material_index;
            }
            mesh_obj["primitives"].push_back(prim);
        }

        uint32_t mesh_idx = static_cast<uint32_t>(gltf["meshes"].size());
        gltf["meshes"].push_back(mesh_obj);

        json mesh_node = {{"name", m.name}, {"mesh", mesh_idx}};
        if (!model.bones.empty() && !m.weights.empty()) {
            mesh_node["skin"] = 0;
        }
        uint32_t mesh_node_idx = static_cast<uint32_t>(gltf["nodes"].size());
        gltf["nodes"].push_back(mesh_node);
        root_node_indices.push_back(mesh_node_idx);
    }

    // Animations
    if (!model.animations.empty()) {
        for (const auto& anim : model.animations) {
            if (anim.tracks.empty()) continue;
            json anim_obj;
            anim_obj["name"] = to_valid_utf8(anim.name);
            json samplers = json::array();
            json channels = json::array();
            std::set<std::pair<int32_t, std::string>> used_targets;
            auto add_channel = [&](uint32_t sampler_idx, int32_t target_node, const std::string& path) {
                if (used_targets.insert({target_node, path}).second) {
                    channels.push_back({{"sampler", sampler_idx}, {"target", {{"node", target_node}, {"path", path}}}});
                }
            };

            for (const auto& track : anim.tracks) {
                int32_t node_idx = -1;
                size_t target_bi = 0;
                for (size_t bi = 0; bi < model.bones.size(); ++bi) {
                    if (model.bones[bi].name == track.bone_name ||
                        (track.channel_id > 0 && static_cast<size_t>(track.channel_id - 1) == bi)) {
                        node_idx = joint_indices[bi];
                        target_bi = bi;
                        break;
                    }
                }
                if (node_idx < 0) continue;
                bool is_root = is_root_bone[target_bi];

                if (track.format == "split") {
                    if (!track.translation_times.empty() && !track.translations.empty()) {
                        uint32_t t_bv = add_buffer_view(track.translation_times.data(), track.translation_times.size() * sizeof(float));
                        float min_t = track.translation_times.front();
                        float max_t = track.translation_times.back();
                        uint32_t t_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", t_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.translation_times.size()}, {"type", "SCALAR"},
                            {"min", {min_t}}, {"max", {max_t}}
                        });

                        std::vector<float> trans_vals;
                        for (const auto& tr : track.translations) {
                            if (options.y_up && is_root) {
                                trans_vals.push_back(tr.x);
                                trans_vals.push_back(tr.z);
                                trans_vals.push_back(-tr.y);
                            } else {
                                trans_vals.push_back(tr.x);
                                trans_vals.push_back(tr.y);
                                trans_vals.push_back(tr.z);
                            }
                        }
                        uint32_t v_bv = add_buffer_view(trans_vals.data(), trans_vals.size() * sizeof(float));
                        uint32_t v_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", v_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.translations.size()}, {"type", "VEC3"}
                        });

                        uint32_t samp_idx = static_cast<uint32_t>(samplers.size());
                        samplers.push_back({{"input", t_acc}, {"output", v_acc}, {"interpolation", "LINEAR"}});
                        add_channel(samp_idx, node_idx, "translation");
                    }

                    if (!track.rotation_times.empty() && !track.rotations.empty()) {
                        uint32_t t_bv = add_buffer_view(track.rotation_times.data(), track.rotation_times.size() * sizeof(float));
                        float min_t = track.rotation_times.front();
                        float max_t = track.rotation_times.back();
                        uint32_t t_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", t_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.rotation_times.size()}, {"type", "SCALAR"},
                            {"min", {min_t}}, {"max", {max_t}}
                        });

                        std::vector<float> rot_vals;
                        for (const auto& rot : track.rotations) {
                            Vec4 r = rot;
                            if (options.y_up && is_root) {
                                r = quat_mul(q_yup_conv, r);
                            }
                            float qlen = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
                            if (qlen > 1e-6f) {
                                r.x /= qlen; r.y /= qlen; r.z /= qlen; r.w /= qlen;
                            } else {
                                r = {0.0f, 0.0f, 0.0f, 1.0f};
                            }
                            rot_vals.push_back(r.x);
                            rot_vals.push_back(r.y);
                            rot_vals.push_back(r.z);
                            rot_vals.push_back(r.w);
                        }
                        uint32_t v_bv = add_buffer_view(rot_vals.data(), rot_vals.size() * sizeof(float));
                        uint32_t v_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", v_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.rotations.size()}, {"type", "VEC4"}
                        });

                        uint32_t samp_idx = static_cast<uint32_t>(samplers.size());
                        samplers.push_back({{"input", t_acc}, {"output", v_acc}, {"interpolation", "LINEAR"}});
                        add_channel(samp_idx, node_idx, "rotation");
                    }
                } else {
                    if (!track.times.empty() && !track.translations.empty() && !track.rotations.empty()) {
                        uint32_t t_bv = add_buffer_view(track.times.data(), track.times.size() * sizeof(float));
                        float min_t = track.times.front();
                        float max_t = track.times.back();
                        uint32_t t_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", t_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.times.size()}, {"type", "SCALAR"},
                            {"min", {min_t}}, {"max", {max_t}}
                        });

                        std::vector<float> trans_vals;
                        for (const auto& tr : track.translations) {
                            if (options.y_up && is_root) {
                                trans_vals.push_back(tr.x);
                                trans_vals.push_back(tr.z);
                                trans_vals.push_back(-tr.y);
                            } else {
                                trans_vals.push_back(tr.x);
                                trans_vals.push_back(tr.y);
                                trans_vals.push_back(tr.z);
                            }
                        }
                        uint32_t tv_bv = add_buffer_view(trans_vals.data(), trans_vals.size() * sizeof(float));
                        uint32_t tv_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", tv_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.translations.size()}, {"type", "VEC3"}
                        });

                        std::vector<float> rot_vals;
                        for (const auto& rot : track.rotations) {
                            Vec4 r = rot;
                            if (options.y_up && is_root) {
                                r = quat_mul(q_yup_conv, r);
                            }
                            float qlen = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
                            if (qlen > 1e-6f) {
                                r.x /= qlen; r.y /= qlen; r.z /= qlen; r.w /= qlen;
                            } else {
                                r = {0.0f, 0.0f, 0.0f, 1.0f};
                            }
                            rot_vals.push_back(r.x);
                            rot_vals.push_back(r.y);
                            rot_vals.push_back(r.z);
                            rot_vals.push_back(r.w);
                        }
                        uint32_t rv_bv = add_buffer_view(rot_vals.data(), rot_vals.size() * sizeof(float));
                        uint32_t rv_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", rv_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", track.rotations.size()}, {"type", "VEC4"}
                        });

                        uint32_t s_tr = static_cast<uint32_t>(samplers.size());
                        samplers.push_back({{"input", t_acc}, {"output", tv_acc}, {"interpolation", "LINEAR"}});
                        add_channel(s_tr, node_idx, "translation");

                        uint32_t s_rot = static_cast<uint32_t>(samplers.size());
                        samplers.push_back({{"input", t_acc}, {"output", rv_acc}, {"interpolation", "LINEAR"}});
                        add_channel(s_rot, node_idx, "rotation");
                    }
                }
            }

            if (!channels.empty()) {
                anim_obj["samplers"] = samplers;
                anim_obj["channels"] = channels;
                gltf["animations"].push_back(anim_obj);
            }
        }
    }

    // Preserve animation presence tag if static/empty animation exists
    if (!model.animations.empty() && (!gltf.contains("animations") || gltf["animations"].empty())) {
        gltf["extras"]["grn_has_animation"] = true;
        gltf["extras"]["grn_anim_name"] = to_valid_utf8(model.animations[0].name);
    }

    gltf["scenes"][0]["nodes"] = root_node_indices;
    gltf["bufferViews"] = buffer_views;
    gltf["accessors"] = accessors;

    // Finalize Buffer
    padTo4(bin_buffer);
    gltf["buffers"] = json::array({{{"byteLength", bin_buffer.size()}}});

    std::string json_str = gltf.dump(-1, ' ', false, json::error_handler_t::replace);
    while (json_str.size() % 4 != 0) {
        json_str.push_back(' ');
    }

    uint32_t json_chunk_len = static_cast<uint32_t>(json_str.size());
    uint32_t bin_chunk_len = static_cast<uint32_t>(bin_buffer.size());
    uint32_t total_glb_len = 12 + 8 + json_chunk_len + (bin_chunk_len > 0 ? (8 + bin_chunk_len) : 0);

    std::vector<uint8_t> glb(total_glb_len);
    size_t off = 0;

    // Header
    std::memcpy(glb.data() + off, "glTF", 4); off += 4;
    *reinterpret_cast<uint32_t*>(glb.data() + off) = 2; off += 4;
    *reinterpret_cast<uint32_t*>(glb.data() + off) = total_glb_len; off += 4;

    // JSON Chunk
    *reinterpret_cast<uint32_t*>(glb.data() + off) = json_chunk_len; off += 4;
    *reinterpret_cast<uint32_t*>(glb.data() + off) = 0x4E4F534A; off += 4;
    std::memcpy(glb.data() + off, json_str.data(), json_chunk_len); off += json_chunk_len;

    // BIN Chunk
    if (bin_chunk_len > 0) {
        *reinterpret_cast<uint32_t*>(glb.data() + off) = bin_chunk_len; off += 4;
        *reinterpret_cast<uint32_t*>(glb.data() + off) = 0x004E4942; off += 4;
        std::memcpy(glb.data() + off, bin_buffer.data(), bin_chunk_len); off += bin_chunk_len;
    }

    return glb;
}

bool export_grn_to_glb_file(const std::filesystem::path& path, const GrnModel& model, const GlbExportOptions& options) {
    GlbExportOptions opt = options;
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (opt.loose_texture_dir.empty()) {
        opt.loose_texture_dir = path.parent_path();
    }
    if (opt.model_stem.empty()) {
        opt.model_stem = path.stem().string();
    }

    auto glb_data = export_grn_to_glb_memory(model, opt);
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(glb_data.data()), glb_data.size());
    return file.good();
}

} // namespace grn
