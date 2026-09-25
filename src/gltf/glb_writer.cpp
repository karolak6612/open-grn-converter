#include "glb_writer.h"
#include "../codecs/tga_png.h"
#include "../gui/viewer/grn_anim_sampler.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <set>
#include <unordered_set>

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

static inline Mat4x4 mat4_mul(const Mat4x4& a, const Mat4x4& b) {
    Mat4x4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[c * 4 + k];
            }
            r.m[c * 4 + row] = sum;
        }
    }
    return r;
}

static inline Mat4x4 compose_grn_transform(const Vec3& pos, const Vec4& rot, const std::array<float, 9>& scale_3x3) {
    float qx = rot.x, qy = rot.y, qz = rot.z, qw = rot.w;
    float lenSq = qx * qx + qy * qy + qz * qz + qw * qw;
    if (lenSq > 1e-8f) {
        float invLen = 1.0f / std::sqrt(lenSq);
        qx *= invLen; qy *= invLen; qz *= invLen; qw *= invLen;
    } else {
        qx = qy = qz = 0.0f; qw = 1.0f;
    }

    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    float R[3][3];
    R[0][0] = 1.0f - 2.0f * (yy + zz);
    R[0][1] = 2.0f * (xy - wz);
    R[0][2] = 2.0f * (xz + wy);

    R[1][0] = 2.0f * (xy + wz);
    R[1][1] = 1.0f - 2.0f * (xx + zz);
    R[1][2] = 2.0f * (yz - wx);

    R[2][0] = 2.0f * (xz - wy);
    R[2][1] = 2.0f * (yz + wx);
    R[2][2] = 1.0f - 2.0f * (xx + yy);

    float S[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            S[r][c] = scale_3x3[r * 3 + c];
        }
    }

    float RS[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            RS[r][c] = R[r][0] * S[0][c] + R[r][1] * S[1][c] + R[r][2] * S[2][c];
        }
    }

    Mat4x4 mat{};
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            mat.m[c * 4 + r] = RS[r][c];
        }
    }
    mat.m[12] = pos.x;
    mat.m[13] = pos.y;
    mat.m[14] = pos.z;
    mat.m[15] = 1.0f;
    return mat;
}

static inline Mat3x3 extract_rotation_from_mat4(const Mat4x4& m) {
    Mat3x3 A;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            A(r, c) = m.m[c * 4 + r];
        }
    }
    Mat3x3 C = A.transposed() * A;
    Vec3 evals;
    Mat3x3 V;
    jacobi_sym3(C, evals, V);
    Mat3x3 Sinv{};
    Sinv(0, 0) = 1.0f / std::sqrt(std::max(1e-8f, evals.x));
    Sinv(1, 1) = 1.0f / std::sqrt(std::max(1e-8f, evals.y));
    Sinv(2, 2) = 1.0f / std::sqrt(std::max(1e-8f, evals.z));
    Mat3x3 R = A * V * Sinv * V.transposed();
    if (R.determinant() < 0.0f) {
        int min_c = 0;
        if (evals.y < evals.x && evals.y < evals.z) min_c = 1;
        else if (evals.z < evals.x && evals.z < evals.y) min_c = 2;
        Sinv(min_c, min_c) = -Sinv(min_c, min_c);
        R = A * V * Sinv * V.transposed();
    }
    return R;
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

    // Coordinate conversion matrix S_conv (Granny Z-up to glTF Y-up)
    Mat4x4 S_conv = Mat4x4::identity();
    Mat4x4 S_conv_inv = Mat4x4::identity();
    if (options.y_up) {
        S_conv.m[0] = 1.0f;
        S_conv.m[5] = 0.0f;
        S_conv.m[6] = -1.0f; // col 1, row 2: y_grn -> -z_glb
        S_conv.m[9] = 1.0f;  // col 2, row 1: z_grn -> y_glb
        S_conv.m[10] = 0.0f;
        S_conv.m[15] = 1.0f;

        S_conv_inv.m[0] = 1.0f;
        S_conv_inv.m[5] = 0.0f;
        S_conv_inv.m[6] = 1.0f;  // col 1, row 2: y_glb -> z_grn
        S_conv_inv.m[9] = -1.0f; // col 2, row 1: z_glb -> -y_grn
        S_conv_inv.m[10] = 0.0f;
        S_conv_inv.m[15] = 1.0f;
    }

    // Bones / Skeleton
    std::vector<uint32_t> joint_indices;
    std::vector<Mat4x4> W_grn_rest(model.bones.size());
    std::vector<Mat4x4> IBM_grn_rest(model.bones.size());
    std::vector<Mat4x4> W_glb_rest(model.bones.size());
    std::vector<Mat4x4> IBM_glb(model.bones.size());
    std::vector<Vec3> bone_node_trans(model.bones.size());
    std::vector<Vec4> bone_node_rot(model.bones.size());

    // 1. Evaluate Granny rest world matrices
    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
        const auto& b = model.bones[bi];
        Mat4x4 local_m = compose_grn_transform(b.position, b.rotation, b.scale_3x3);
        int32_t p = b.parent_index;
        if (p >= 0 && static_cast<size_t>(p) < bi && p != static_cast<int32_t>(bi)) {
            W_grn_rest[bi] = mat4_mul(W_grn_rest[p], local_m);
        } else {
            W_grn_rest[bi] = local_m;
        }
        IBM_grn_rest[bi] = invert_mat4(W_grn_rest[bi]);
    }

    // 2. Build GLB Rest World Transforms and IBMs
    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
        Mat3x3 R_grn = extract_rotation_from_mat4(W_grn_rest[bi]);
        Mat4x4 R_grn_mat = Mat4x4::identity();
        for (int c = 0; c < 3; ++c) {
            for (int r = 0; r < 3; ++r) {
                R_grn_mat.m[c * 4 + r] = R_grn(r, c);
            }
        }
        R_grn_mat.m[12] = W_grn_rest[bi].m[12];
        R_grn_mat.m[13] = W_grn_rest[bi].m[13];
        R_grn_mat.m[14] = W_grn_rest[bi].m[14];

        W_glb_rest[bi] = mat4_mul(S_conv, mat4_mul(R_grn_mat, S_conv_inv));
        IBM_glb[bi] = invert_mat4(W_glb_rest[bi]);
    }

    // 3. Build GLB Rest Local Transforms & Joint Nodes
    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
        const auto& b = model.bones[bi];
        int32_t p = b.parent_index;
        bool is_root = (p < 0 || p == static_cast<int32_t>(bi) || static_cast<size_t>(p) >= model.bones.size());

        Mat4x4 Local_glb;
        if (!is_root) {
            Local_glb = mat4_mul(IBM_glb[p], W_glb_rest[bi]);
        } else {
            Local_glb = W_glb_rest[bi];
        }

        Vec3 tx = {Local_glb.m[12], Local_glb.m[13], Local_glb.m[14]};
        Mat3x3 local_R;
        for (int c = 0; c < 3; ++c) {
            for (int r = 0; r < 3; ++r) {
                local_R(r, c) = Local_glb.m[c * 4 + r];
            }
        }
        Vec4 rx = mat3_to_quat(local_R);

        bone_node_trans[bi] = tx;
        bone_node_rot[bi] = rx;

        json node = {
            {"name", to_valid_utf8(b.name)},
            {"translation", {tx.x, tx.y, tx.z}},
            {"rotation", {rx.x, rx.y, rx.z, rx.w}}
        };
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
        std::vector<float> ibm_floats;
        ibm_floats.reserve(model.bones.size() * 16);
        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            for (int k = 0; k < 16; ++k) ibm_floats.push_back(IBM_glb[bi].m[k]);
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
    std::vector<uint32_t> mesh_node_indices;
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

            uint32_t norm_acc = 0;
            if (has_normals && !norm_buf.empty()) {
                uint32_t norm_bv = add_buffer_view(norm_buf.data(), norm_buf.size() * sizeof(float), 34962);
                norm_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", norm_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC3"}
                });
            }

            uint32_t uv_acc = 0;
            if (has_uvs && !uv_buf.empty()) {
                uint32_t uv_bv = add_buffer_view(uv_buf.data(), uv_buf.size() * sizeof(float), 34962);
                uv_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", uv_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC2"}
                });
            } else if (!tex_indices.empty()) {
                std::vector<float> default_uvs(vert_count * 2, 0.0f);
                uint32_t uv_bv = add_buffer_view(default_uvs.data(), default_uvs.size() * sizeof(float), 34962);
                uv_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", uv_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", vert_count}, {"type", "VEC2"}
                });
            }

            json prim_attrs = {{"POSITION", pos_acc}};
            if (has_normals && !norm_buf.empty()) {
                prim_attrs["NORMAL"] = norm_acc;
            }
            if ((has_uvs && !uv_buf.empty()) || !tex_indices.empty()) {
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
        mesh_node_indices.push_back(mesh_node_idx);
    }

    // Animations
    if (!model.animations.empty()) {
        std::unordered_set<std::string> used_anim_names;
        for (const auto& anim : model.animations) {
            if (anim.tracks.empty()) continue;

            std::string anim_name = to_valid_utf8(anim.name);
            if (anim_name.empty()) anim_name = "Animation";
            std::string unique_anim_name = anim_name;
            int counter = 1;
            while (used_anim_names.count(unique_anim_name)) {
                unique_anim_name = anim_name + "_" + std::to_string(counter++);
            }
            used_anim_names.insert(unique_anim_name);

            json anim_obj;
            anim_obj["name"] = unique_anim_name;
            json samplers = json::array();
            json channels = json::array();

            if (!model.bones.empty()) {
                GrnAnimSampler sampler(anim, model.bones);

                // Collect unique sample timestamps
                std::set<float> time_set;
                for (const auto& trk : anim.tracks) {
                    for (float t : trk.translation_times) time_set.insert(t);
                    for (float t : trk.rotation_times) time_set.insert(t);
                    for (float t : trk.scale_shear_times) time_set.insert(t);
                    for (float t : trk.times) time_set.insert(t);
                }
                time_set.insert(0.0f);
                if (anim.duration > 0.0f) time_set.insert(anim.duration);

                float fps = anim.fps > 0.0f ? anim.fps : 30.0f;
                int num_frames = static_cast<int>(std::ceil(anim.duration * fps));
                for (int f = 0; f <= num_frames; ++f) {
                    float t = std::min(static_cast<float>(f) / fps, anim.duration);
                    time_set.insert(t);
                }
                std::vector<float> sample_times(time_set.begin(), time_set.end());

                if (sample_times.size() < 2) {
                    sample_times.push_back(std::max(0.03333f, anim.duration));
                }

                uint32_t t_bv = add_buffer_view(sample_times.data(), sample_times.size() * sizeof(float));
                float min_t = sample_times.front();
                float max_t = sample_times.back();
                uint32_t t_acc = static_cast<uint32_t>(accessors.size());
                accessors.push_back({
                    {"bufferView", t_bv}, {"byteOffset", 0}, {"componentType", 5126},
                    {"count", sample_times.size()}, {"type", "SCALAR"},
                    {"min", {min_t}}, {"max", {max_t}}
                });

                std::vector<std::vector<float>> all_bone_trans(model.bones.size());
                std::vector<std::vector<float>> all_bone_rot(model.bones.size());
                std::vector<Vec4> prev_rot = bone_node_rot;

                for (float t : sample_times) {
                    std::vector<Mat4x4> local_grn(model.bones.size());
                    std::vector<Mat4x4> W_grn_anim(model.bones.size());
                    std::vector<Mat4x4> W_glb_anim(model.bones.size());
                    std::vector<Mat4x4> Local_glb_anim(model.bones.size());

                    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
                        const auto& b = model.bones[bi];
                        Vec3 pos;
                        Vec4 rot;
                        std::array<float, 9> scale;
                        sampler.sampleBone(bi, b.position, b.rotation, b.scale_3x3, t, pos, rot, scale);
                        local_grn[bi] = compose_grn_transform(pos, rot, scale);

                        int32_t p = b.parent_index;
                        bool is_root = (p < 0 || p == static_cast<int32_t>(bi) || static_cast<size_t>(p) >= model.bones.size());
                        if (!is_root) {
                            W_grn_anim[bi] = mat4_mul(W_grn_anim[p], local_grn[bi]);
                        } else {
                            W_grn_anim[bi] = local_grn[bi];
                        }

                        // World-Space Skinning Invariance:
                        // Skin_grn = W_grn_anim * IBM_grn_rest
                        Mat4x4 Skin_grn = mat4_mul(W_grn_anim[bi], IBM_grn_rest[bi]);
                        // Skin_glb = S_conv * Skin_grn * S_conv_inv
                        Mat4x4 Skin_glb = mat4_mul(S_conv, mat4_mul(Skin_grn, S_conv_inv));
                        // W_glb_anim = Skin_glb * W_glb_rest
                        W_glb_anim[bi] = mat4_mul(Skin_glb, W_glb_rest[bi]);

                        if (!is_root) {
                            Local_glb_anim[bi] = mat4_mul(invert_mat4(W_glb_anim[p]), W_glb_anim[bi]);
                        } else {
                            Local_glb_anim[bi] = W_glb_anim[bi];
                        }

                        Vec3 tr = {Local_glb_anim[bi].m[12], Local_glb_anim[bi].m[13], Local_glb_anim[bi].m[14]};
                        Mat3x3 R_loc = extract_rotation_from_mat4(Local_glb_anim[bi]);
                        Vec4 q = mat3_to_quat(R_loc);

                        // Enforce sign continuity across consecutive frames
                        float dot = prev_rot[bi].x * q.x + prev_rot[bi].y * q.y + prev_rot[bi].z * q.z + prev_rot[bi].w * q.w;
                        if (dot < 0.0f) {
                            q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
                        }
                        prev_rot[bi] = q;

                        all_bone_trans[bi].push_back(tr.x);
                        all_bone_trans[bi].push_back(tr.y);
                        all_bone_trans[bi].push_back(tr.z);

                        all_bone_rot[bi].push_back(q.x);
                        all_bone_rot[bi].push_back(q.y);
                        all_bone_rot[bi].push_back(q.z);
                        all_bone_rot[bi].push_back(q.w);
                    }
                }

                // Add channels for each joint
                for (size_t bi = 0; bi < model.bones.size(); ++bi) {
                    uint32_t node_idx = joint_indices[bi];
                    bool is_root = (model.bones[bi].parent_index < 0 || model.bones[bi].parent_index == static_cast<int32_t>(bi));
                    bool has_track = sampler.hasTrackForBone(bi);

                    // Check translation motion
                    bool has_trans_anim = is_root;
                    if (!has_trans_anim) {
                        for (size_t k = 0; k < sample_times.size(); ++k) {
                            float dx = all_bone_trans[bi][k * 3 + 0] - bone_node_trans[bi].x;
                            float dy = all_bone_trans[bi][k * 3 + 1] - bone_node_trans[bi].y;
                            float dz = all_bone_trans[bi][k * 3 + 2] - bone_node_trans[bi].z;
                            if (dx * dx + dy * dy + dz * dz > 1e-6f) {
                                has_trans_anim = true;
                                break;
                            }
                        }
                    }

                    if (has_trans_anim) {
                        uint32_t v_bv = add_buffer_view(all_bone_trans[bi].data(), all_bone_trans[bi].size() * sizeof(float));
                        uint32_t v_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", v_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", sample_times.size()}, {"type", "VEC3"}
                        });
                        uint32_t samp_idx = static_cast<uint32_t>(samplers.size());
                        samplers.push_back({{"input", t_acc}, {"output", v_acc}, {"interpolation", "LINEAR"}});
                        channels.push_back({{"sampler", samp_idx}, {"target", {{"node", node_idx}, {"path", "translation"}}}});
                    }

                    // Check rotation motion
                    bool has_rot_anim = is_root || has_track;
                    if (!has_rot_anim) {
                        for (size_t k = 0; k < sample_times.size(); ++k) {
                            float qx = all_bone_rot[bi][k * 4 + 0];
                            float qy = all_bone_rot[bi][k * 4 + 1];
                            float qz = all_bone_rot[bi][k * 4 + 2];
                            float qw = all_bone_rot[bi][k * 4 + 3];
                            float dot = std::abs(qx * bone_node_rot[bi].x + qy * bone_node_rot[bi].y + qz * bone_node_rot[bi].z + qw * bone_node_rot[bi].w);
                            if (dot < 0.99999f) {
                                has_rot_anim = true;
                                break;
                            }
                        }
                    }

                    if (has_rot_anim) {
                        uint32_t v_bv = add_buffer_view(all_bone_rot[bi].data(), all_bone_rot[bi].size() * sizeof(float));
                        uint32_t v_acc = static_cast<uint32_t>(accessors.size());
                        accessors.push_back({
                            {"bufferView", v_bv}, {"byteOffset", 0}, {"componentType", 5126},
                            {"count", sample_times.size()}, {"type", "VEC4"}
                        });
                        uint32_t samp_idx = static_cast<uint32_t>(samplers.size());
                        samplers.push_back({{"input", t_acc}, {"output", v_acc}, {"interpolation", "LINEAR"}});
                        channels.push_back({{"sampler", samp_idx}, {"target", {{"node", node_idx}, {"path", "rotation"}}}});
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
