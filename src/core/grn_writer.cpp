/**
 * @file grn_writer.cpp
 * @brief Binary container and chunk serializer for constructing GRN format files
 *        compatible with standard viewers and game engines.
 */

#include "grn_writer.h"
#include "../codecs/dxt_codec.h"
#include "../codecs/vtex_codec.h"
#include <fstream>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <algorithm>

namespace grn {

static const uint8_t DEFAULT_SIGNATURE[64] = {
    0x2A, 0x30, 0x39, 0x04, 0x18, 0x46, 0x6C, 0x66, 0x8D, 0x6D, 0x26, 0x23, 0x9A, 0x52, 0xC1, 0x7A,
    0x70, 0x10, 0xE0, 0x44, 0x84, 0x23, 0x26, 0x32, 0x25, 0x3C, 0x0A, 0x64, 0xF7, 0x26, 0x61, 0x1F,
    0x25, 0x3C, 0x0A, 0x64, 0xF7, 0x26, 0x61, 0x1F, 0x44, 0x68, 0x2A, 0x3B, 0xA8, 0x78, 0xE4, 0x61,
    0x58, 0x58, 0x71, 0x5F, 0x08, 0x39, 0xAC, 0x1D, 0x7A, 0x3D, 0x21, 0x7F, 0x60, 0x4A, 0xF6, 0x37
};

static uint32_t calc_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

static inline void wU32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

static inline void wI32(std::vector<uint8_t>& buf, int32_t val) {
    wU32(buf, static_cast<uint32_t>(val));
}

static inline void wF32(std::vector<uint8_t>& buf, float val) {
    uint32_t u = 0;
    std::memcpy(&u, &val, 4);
    wU32(buf, u);
}

static inline void wVec3(std::vector<uint8_t>& buf, const Vec3& v) {
    wF32(buf, v.x);
    wF32(buf, v.y);
    wF32(buf, v.z);
}

static inline void wVec4(std::vector<uint8_t>& buf, const Vec4& v) {
    wF32(buf, v.x);
    wF32(buf, v.y);
    wF32(buf, v.z);
    wF32(buf, v.w);
}

class SectionChunkBuilder {
public:
    struct ChunkEntry {
        uint32_t tag = 0;
        uint32_t rel_data_offset = 0;
        uint32_t child_budget = 0;
    };

    std::vector<ChunkEntry> flat_chunks;
    std::vector<uint8_t> payload;
    std::vector<size_t> container_stack;

    size_t begin_container(uint32_t tag, const std::vector<uint8_t>& cont_payload = {}) {
        ChunkEntry entry;
        entry.tag = tag;
        entry.child_budget = 0;
        entry.rel_data_offset = static_cast<uint32_t>(payload.size());

        if (!cont_payload.empty()) {
            payload.insert(payload.end(), cont_payload.begin(), cont_payload.end());
        }

        size_t idx = flat_chunks.size();
        flat_chunks.push_back(entry);
        container_stack.push_back(idx);
        return idx;
    }

    void end_container(size_t container_idx) {
        if (container_stack.empty() || container_stack.back() != container_idx) {
            return;
        }
        container_stack.pop_back();
        size_t direct_children = (flat_chunks.size() - 1) - container_idx;
        flat_chunks[container_idx].child_budget = static_cast<uint32_t>(direct_children);
    }

    void add_leaf(uint32_t tag, const std::vector<uint8_t>& leaf_payload = {}) {
        ChunkEntry entry;
        entry.tag = tag;
        entry.child_budget = 0;
        entry.rel_data_offset = static_cast<uint32_t>(payload.size());

        if (!leaf_payload.empty()) {
            payload.insert(payload.end(), leaf_payload.begin(), leaf_payload.end());
        }

        flat_chunks.push_back(entry);
    }
};

static std::vector<uint8_t> build_section_0() {
    std::vector<std::string> exp_strings = {
        "GRN 3D Exporter",
        "1.2b",
        "10-4-2000",
        "win32",
        "(C) 3D Model Format. All Rights Reserved.",
        ""
    };

    std::vector<uint8_t> exp_str_bytes;
    for (const auto& s : exp_strings) {
        exp_str_bytes.insert(exp_str_bytes.end(), s.begin(), s.end());
        exp_str_bytes.push_back(0);
    }

    std::vector<uint8_t> exp_str_payload;
    wU32(exp_str_payload, static_cast<uint32_t>(exp_strings.size()));
    wU32(exp_str_payload, static_cast<uint32_t>(exp_str_bytes.size()));
    exp_str_payload.insert(exp_str_payload.end(), exp_str_bytes.begin(), exp_str_bytes.end());

    uint32_t c0_off = 0x58;
    uint32_t c1_off = c0_off + static_cast<uint32_t>(exp_str_payload.size());
    uint32_t pad_c1 = (4 - (c1_off % 4)) % 4;
    c1_off += pad_c1;

    std::vector<uint8_t> c1_payload;
    wU32(c1_payload, 0); wU32(c1_payload, 1); wU32(c1_payload, 2); wU32(c1_payload, 3);
    uint32_t c3_off = c1_off + static_cast<uint32_t>(c1_payload.size());

    // Units per meter metric (0.0254 = inches to meters)
    std::vector<uint8_t> c3_payload;
    wF32(c3_payload, 0.0f); wF32(c3_payload, 0.0f); wF32(c3_payload, 0.0f); // origin
    wF32(c3_payload, 0.0254f); wF32(c3_payload, 0.0f); wF32(c3_payload, 0.0f);
    wF32(c3_payload, 0.0f); wF32(c3_payload, 0.0254f); wF32(c3_payload, 0.0f);
    wF32(c3_payload, 0.0f); wF32(c3_payload, 0.0f); wF32(c3_payload, 0.0254f);

    uint32_t c4_off = c3_off + static_cast<uint32_t>(c3_payload.size());
    std::vector<uint8_t> c4_payload;
    wU32(c4_payload, 2); wU32(c4_payload, 4); wU32(c4_payload, 3); wU32(c4_payload, 5);
    uint32_t c5_off = c4_off + static_cast<uint32_t>(c4_payload.size());

    std::vector<uint8_t> sec0;
    wU32(sec0, 6); wU32(sec0, 0); wU32(sec0, 0); wU32(sec0, 0); // 6 chunks
    wU32(sec0, T_STRING_TABLE); wU32(sec0, c0_off); wU32(sec0, 0);
    wU32(sec0, 0xca5e1003); wU32(sec0, c1_off); wU32(sec0, 3);
    wU32(sec0, 0xca5e1001); wU32(sec0, c1_off); wU32(sec0, 1);
    wU32(sec0, 0xca5e1002); wU32(sec0, c3_off); wU32(sec0, 0);
    wU32(sec0, 0xca5e1000); wU32(sec0, c4_off); wU32(sec0, 0);
    wU32(sec0, T_NULL_TERMINATOR); wU32(sec0, c5_off); wU32(sec0, 0);

    if (sec0.size() < c0_off) {
        sec0.resize(c0_off, 0);
    }
    sec0.insert(sec0.end(), exp_str_payload.begin(), exp_str_payload.end());
    if (pad_c1) sec0.insert(sec0.end(), pad_c1, 0);
    sec0.insert(sec0.end(), c1_payload.begin(), c1_payload.end());
    sec0.insert(sec0.end(), c3_payload.begin(), c3_payload.end());
    sec0.insert(sec0.end(), c4_payload.begin(), c4_payload.end());

    size_t pad = (16 - (sec0.size() % 16)) % 16;
    if (pad) sec0.insert(sec0.end(), pad, 0);
    return sec0;
}

std::vector<uint8_t> write_grn_memory(const GrnModel& model) {
    // Collect strings: 0 is empty, 1 is __Standard, 2 is __ObjectName, 3 is __FileName
    std::vector<std::string> strings = {"", "__Standard", "__ObjectName", "__FileName"};
    std::unordered_map<std::string, int32_t> string_map = {
        {"", 0}, {"__Standard", 1}, {"__ObjectName", 2}, {"__FileName", 3}
    };

    auto get_str_idx = [&](const std::string& s) -> int32_t {
        auto it = string_map.find(s);
        if (it != string_map.end()) return it->second;
        int32_t idx = static_cast<int32_t>(strings.size());
        strings.push_back(s);
        string_map[s] = idx;
        return idx;
    };

    struct DextEntry {
        std::string obj_name;
        std::string file_name;
    };
    std::vector<DextEntry> dext_list;
    auto add_dext = [&](const std::string& obj, const std::string& file = "") -> int32_t {
        int32_t idx = static_cast<int32_t>(dext_list.size());
        dext_list.push_back({obj, file});
        get_str_idx(obj);
        if (!file.empty()) get_str_idx(file);
        return idx;
    };

    std::vector<int32_t> bone_dexts;
    for (const auto& b : model.bones) bone_dexts.push_back(add_dext(b.name));

    std::vector<int32_t> mesh_dexts;
    for (const auto& m : model.meshes) mesh_dexts.push_back(add_dext(m.name));

    std::vector<int32_t> tex_dexts;
    for (const auto& t : model.textures) tex_dexts.push_back(add_dext(t.name, t.file_name));

    std::vector<int32_t> mat_dexts;
    if (!model.materials.empty()) {
        for (const auto& m : model.materials) mat_dexts.push_back(add_dext(m.name));
    } else if (!model.meshes.empty()) {
        mat_dexts.push_back(add_dext("Material"));
    }

    int32_t model_dext = add_dext("Model");

    SectionChunkBuilder builder;

    // 1. String Table Chunk (0xca5e0200)
    std::vector<uint8_t> str_payload;
    std::vector<uint8_t> str_bytes;
    for (const auto& s : strings) {
        str_bytes.insert(str_bytes.end(), s.begin(), s.end());
        str_bytes.push_back(0);
    }
    wU32(str_payload, static_cast<uint32_t>(strings.size()));
    wU32(str_payload, static_cast<uint32_t>(str_bytes.size()));
    str_payload.insert(str_payload.end(), str_bytes.begin(), str_bytes.end());
    builder.add_leaf(T_STRING_TABLE, str_payload);

    // 2. Data Extensions Section (0xca5e0f03)
    std::vector<uint8_t> dsec_payload;
    wU32(dsec_payload, 1); wU32(dsec_payload, 0);
    size_t dext_sec_idx = builder.begin_container(T_DATA_EXTENSION_SECTION, dsec_payload);

    for (const auto& entry : dext_list) {
        int32_t name_idx = get_str_idx(entry.obj_name);
        int32_t fname_idx = entry.file_name.empty() ? get_str_idx("") : get_str_idx(entry.file_name);

        std::vector<uint8_t> dnode_payload;
        wU32(dnode_payload, 1); wU32(dnode_payload, 0); // String index 1 = "__Standard"
        size_t d_node_idx = builder.begin_container(T_DATA_EXTENSION, dnode_payload);

        std::vector<uint8_t> psec_payload;
        wU32(psec_payload, 2);
        size_t prop_sec_idx = builder.begin_container(T_DATA_EXTENSION_PROPERTY_SECTION, psec_payload);

        // Property 1: __ObjectName (key string index 2)
        std::vector<uint8_t> prop1_payload;
        wU32(prop1_payload, 2);
        size_t p1_idx = builder.begin_container(T_DATA_EXTENSION_PROPERTY, prop1_payload);

        std::vector<uint8_t> vsec1_payload;
        wU32(vsec1_payload, 0); wU32(vsec1_payload, static_cast<uint32_t>(name_idx));
        size_t val_sec1 = builder.begin_container(T_DATA_EXTENSION_VALUE_SECTION, vsec1_payload);

        std::vector<uint8_t> v1_payload;
        wU32(v1_payload, 0); wU32(v1_payload, static_cast<uint32_t>(name_idx));
        builder.add_leaf(T_DATA_EXTENSION_PROPERTY_VALUE, v1_payload);

        builder.end_container(val_sec1);
        builder.end_container(p1_idx);

        // Property 2: __FileName (key string index 3)
        std::vector<uint8_t> prop2_payload;
        wU32(prop2_payload, 3);
        size_t p2_idx = builder.begin_container(T_DATA_EXTENSION_PROPERTY, prop2_payload);

        std::vector<uint8_t> vsec2_payload;
        wU32(vsec2_payload, 0); wU32(vsec2_payload, static_cast<uint32_t>(fname_idx));
        size_t val_sec2 = builder.begin_container(T_DATA_EXTENSION_VALUE_SECTION, vsec2_payload);

        std::vector<uint8_t> v2_payload;
        wU32(v2_payload, 0); wU32(v2_payload, static_cast<uint32_t>(fname_idx));
        builder.add_leaf(T_DATA_EXTENSION_PROPERTY_VALUE, v2_payload);

        builder.end_container(val_sec2);
        builder.end_container(p2_idx);

        builder.end_container(prop_sec_idx);
        builder.end_container(d_node_idx);
    }
    builder.end_container(dext_sec_idx);

    // 3. Header Spacer (0xca5e0a01)
    std::vector<uint8_t> spacer_payload;
    wU32(spacer_payload, 0x5F);
    builder.add_leaf(T_HEADER_SPACER, spacer_payload);

    // 4. Transform Channels Section (0xca5e0b01)
    if (!model.bones.empty()) {
        std::vector<uint8_t> tc_sec_payload;
        wU32(tc_sec_payload, 0x5F);
        size_t tc_sec_idx = builder.begin_container(T_TRANSFORM_CHANNEL_SECTION, tc_sec_payload);
        for (size_t i = 0; i < model.bones.size(); ++i) {
            uint32_t dext_1b = static_cast<uint32_t>(bone_dexts[i] + 1);
            std::vector<uint8_t> tc_payload;
            wU32(tc_payload, dext_1b);
            size_t tc_idx = builder.begin_container(T_TRANSFORM_CHANNEL, tc_payload);

            std::vector<uint8_t> ref_payload;
            wU32(ref_payload, dext_1b);
            builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);
            builder.end_container(tc_idx);
        }
        builder.end_container(tc_sec_idx);
    }

    // 5. Meshes Section (0xca5e0602)
    if (!model.meshes.empty()) {
        size_t mesh_sec_idx = builder.begin_container(T_MESH_SECTION);
        for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
            const auto& mesh = model.meshes[mi];
            uint32_t m_dext_1b = static_cast<uint32_t>(mesh_dexts[mi] + 1);
            size_t m_node_idx = builder.begin_container(T_MESH);

            // Vertex Set
            size_t vset_sec_idx = builder.begin_container(T_MESH_VERTEX_SET_SECTION);
            size_t vset_node_idx = builder.begin_container(T_MESH_VERTEX_SET);

            std::vector<uint8_t> v_payload;
            for (const auto& v : mesh.vertices) wVec3(v_payload, v);
            builder.add_leaf(T_MESH_VERTICES, v_payload);

            std::vector<uint8_t> n_payload;
            if (!mesh.normals.empty()) {
                for (const auto& n : mesh.normals) wVec3(n_payload, n);
            } else {
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    wF32(n_payload, 0.0f); wF32(n_payload, 0.0f); wF32(n_payload, 1.0f);
                }
            }
            builder.add_leaf(T_MESH_NORMALS, n_payload);

            // UV field
            size_t f_sec_idx = builder.begin_container(T_MESH_FIELD_SECTION);
            std::vector<uint8_t> f_payload;
            wU32(f_payload, 3);
            if (!mesh.uvs.empty()) {
                for (const auto& uv : mesh.uvs) {
                    wF32(f_payload, uv.x);
                    wF32(f_payload, uv.y);
                    wF32(f_payload, 0.0f);
                }
            } else {
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    wF32(f_payload, 0.0f); wF32(f_payload, 0.0f); wF32(f_payload, 0.0f);
                }
            }
            builder.add_leaf(T_MESH_FIELD, f_payload);
            builder.end_container(f_sec_idx);

            builder.end_container(vset_node_idx);
            builder.end_container(vset_sec_idx);

            // Weights
            std::vector<uint8_t> w_payload;
            if (!mesh.weights.empty()) {
                std::vector<int32_t> palette = mesh.bone_index_map;
                if (palette.empty()) {
                    std::vector<int32_t> used_bones;
                    for (const auto& vw : mesh.weights) {
                        for (int32_t bi : vw.bone_indices) {
                            if (std::find(used_bones.begin(), used_bones.end(), bi) == used_bones.end()) {
                                used_bones.push_back(bi);
                            }
                        }
                    }
                    std::sort(used_bones.begin(), used_bones.end());
                    palette = std::move(used_bones);
                    if (palette.empty() && !model.bones.empty()) palette.push_back(0);
                    const_cast<GrnMesh&>(mesh).bone_index_map = palette;
                }

                size_t max_w_count = 1;
                for (const auto& vw : mesh.weights) {
                    max_w_count = std::max(max_w_count, vw.bone_indices.size());
                }

                int32_t highest_local_b = palette.empty() ? 0 : static_cast<int32_t>(palette.size() - 1);
                wI32(w_payload, static_cast<int32_t>(mesh.weights.size()));
                wI32(w_payload, highest_local_b);
                wI32(w_payload, static_cast<int32_t>(max_w_count));

                for (const auto& vw : mesh.weights) {
                    wI32(w_payload, static_cast<int32_t>(vw.bone_indices.size()));
                    for (size_t k = 0; k < vw.bone_indices.size(); ++k) {
                        int32_t local_b = vw.bone_indices[k];
                        if (local_b < 0 || local_b > highest_local_b) local_b = 0;
                        wI32(w_payload, local_b);
                        wF32(w_payload, vw.bone_weights[k]);
                    }
                }
            } else {
                wI32(w_payload, static_cast<int32_t>(mesh.vertices.size()));
                wI32(w_payload, 0);
                wI32(w_payload, 1);
                for (size_t i = 0; i < mesh.vertices.size(); ++i) {
                    wI32(w_payload, 1);
                    wI32(w_payload, 0);
                    wF32(w_payload, 1.0f);
                }
            }
            builder.add_leaf(T_MESH_WEIGHTS, w_payload);

            // Triangles (v0, v1, v2, n0, n1, n2)
            std::vector<uint8_t> tri_payload;
            for (size_t f = 0; f < mesh.faces.size(); ++f) {
                uint32_t v0 = mesh.faces[f][0];
                uint32_t v1 = mesh.faces[f][1];
                uint32_t v2 = mesh.faces[f][2];
                uint32_t n0 = (f < mesh.face_normals.size()) ? mesh.face_normals[f][0] : v0;
                uint32_t n1 = (f < mesh.face_normals.size()) ? mesh.face_normals[f][1] : v1;
                uint32_t n2 = (f < mesh.face_normals.size()) ? mesh.face_normals[f][2] : v2;
                wU32(tri_payload, v0); wU32(tri_payload, v1); wU32(tri_payload, v2);
                wU32(tri_payload, n0); wU32(tri_payload, n1); wU32(tri_payload, n2);
            }
            builder.add_leaf(T_MESH_TRIANGLES, tri_payload);

            // Data Extension Reference
            std::vector<uint8_t> ref_payload;
            wU32(ref_payload, m_dext_1b);
            builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);

            builder.end_container(m_node_idx);
        }
        builder.end_container(mesh_sec_idx);
    } else {
        builder.add_leaf(T_MESH_SECTION);
    }

    // 6. Skeleton Section (0xca5e0507)
    if (!model.bones.empty()) {
        size_t skel_sec_idx = builder.begin_container(T_SKELETON_SECTION);
        size_t skel_node_idx = builder.begin_container(T_SKELETON);
        size_t bone_sec_idx = builder.begin_container(T_BONE_SECTION);

        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            const auto& b = model.bones[bi];
            uint32_t b_dext_1b = static_cast<uint32_t>(bone_dexts[bi] + 1);

            std::vector<uint8_t> b_payload;
            int32_t p_idx = (b.parent_index < 0) ? static_cast<int32_t>(bi) : b.parent_index;
            wI32(b_payload, p_idx);
            wVec3(b_payload, b.position);
            wVec4(b_payload, b.rotation);
            for (int k = 0; k < 9; ++k) wF32(b_payload, b.scale_3x3[k]);

            size_t b_node_idx = builder.begin_container(T_BONE, b_payload);
            std::vector<uint8_t> ref_payload;
            wU32(ref_payload, b_dext_1b);
            builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);
            builder.end_container(b_node_idx);
        }

        builder.end_container(bone_sec_idx);
        builder.end_container(skel_node_idx);
        builder.end_container(skel_sec_idx);
    }

    // 7. Textures Section (0xca5e0304)
    if (!model.textures.empty()) {
        size_t tex_sec_idx = builder.begin_container(T_TEXTURE_SECTION);
        for (size_t ti = 0; ti < model.textures.size(); ++ti) {
            const auto& tex = model.textures[ti];
            uint32_t t_dext_1b = static_cast<uint32_t>(tex_dexts[ti] + 1);
            size_t t_node_idx = builder.begin_container(T_TEXTURE_MAP);

            std::vector<uint8_t> raw_pix;
            uint32_t fmt_code = tex.format_code;

            if (!tex.raw_blob.empty()) {
                raw_pix = tex.raw_blob;
            } else if (!tex.decoded_rgba.empty() && tex.width > 0 && tex.height > 0) {
                if ((tex.width % 4 == 0) && (tex.height % 4 == 0) && tex.width >= 4 && tex.height >= 4) {
                    if (tex.has_alpha) {
                        raw_pix = encode_dxt5(tex.decoded_rgba.data(), tex.width, tex.height);
                        fmt_code = 10; // DXT5
                    } else {
                        raw_pix = encode_dxt1(tex.decoded_rgba.data(), tex.width, tex.height);
                        fmt_code = 8; // DXT1
                    }
                } else {
                    raw_pix = tex.decoded_rgba;
                    fmt_code = tex.has_alpha ? 1 : 0;
                }
            }

            std::vector<uint8_t> t_img_payload;
            wI32(t_img_payload, static_cast<int32_t>(tex.width));
            wI32(t_img_payload, static_cast<int32_t>(tex.height));
            wI32(t_img_payload, static_cast<int32_t>(fmt_code));
            t_img_payload.insert(t_img_payload.end(), raw_pix.begin(), raw_pix.end());

            // CRITICAL: T_TEXTURE_IMAGE_SECTION container has EMPTY payload to prevent double-write!
            size_t t_img_sec = builder.begin_container(T_TEXTURE_IMAGE_SECTION);
            builder.add_leaf(T_TEXTURE_MAP_IMAGE, t_img_payload);
            builder.end_container(t_img_sec);

            std::vector<uint8_t> ref_payload;
            wU32(ref_payload, t_dext_1b);
            builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);

            builder.end_container(t_node_idx);
        }
        builder.end_container(tex_sec_idx);
    }

    // 8. Materials Section (0xca5e0d01)
    if (!model.materials.empty()) {
        size_t mat_sec_idx = builder.begin_container(T_MATERIAL_SECTION);
        for (size_t mi = 0; mi < model.materials.size(); ++mi) {
            const auto& mat = model.materials[mi];
            uint32_t mat_dext_1b = static_cast<uint32_t>(mat_dexts[mi] + 1);
            size_t m_node_idx = builder.begin_container(T_MATERIAL);

            if (mat.diffuse_texture_index >= 0 && static_cast<size_t>(mat.diffuse_texture_index) < model.textures.size()) {
                int32_t t_1b = mat.diffuse_texture_index + 1;
                std::vector<uint8_t> diff_payload;
                wI32(diff_payload, 0);
                wI32(diff_payload, t_1b);
                wI32(diff_payload, 0);
                builder.add_leaf(T_MATERIAL_SIMPLE_DIFFUSE_TEXTURE, diff_payload);
            }

            std::vector<uint8_t> ref_payload;
            wU32(ref_payload, mat_dext_1b);
            builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);

            builder.end_container(m_node_idx);
        }
        builder.end_container(mat_sec_idx);
    } else if (!model.meshes.empty()) {
        size_t mat_sec_idx = builder.begin_container(T_MATERIAL_SECTION);
        size_t m_node_idx = builder.begin_container(T_MATERIAL);
        if (!model.textures.empty()) {
            std::vector<uint8_t> diff_payload;
            wI32(diff_payload, 0); wI32(diff_payload, 1); wI32(diff_payload, 0);
            builder.add_leaf(T_MATERIAL_SIMPLE_DIFFUSE_TEXTURE, diff_payload);
        }
        std::vector<uint8_t> ref_payload;
        wU32(ref_payload, static_cast<uint32_t>(mat_dexts[0] + 1));
        builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);
        builder.end_container(m_node_idx);
        builder.end_container(mat_sec_idx);
    }

    // 9. Forms Section (0xca5e0c01)
    if (!model.meshes.empty() || !model.bones.empty()) {
        size_t form_sec_idx = builder.begin_container(T_FORM_SECTION);
        std::vector<uint8_t> form_payload;
        wU32(form_payload, 1);
        size_t form_node_idx = builder.begin_container(T_FORM, form_payload);

        if (!model.bones.empty()) {
            size_t fsk_sec_idx = builder.begin_container(T_FORM_SKELETON_SECTION);
            size_t fsk_node_idx = builder.begin_container(T_FORM_SKELETON);

            std::vector<uint8_t> fbc_payload;
            for (size_t b = 0; b < model.bones.size(); ++b) {
                wI32(fbc_payload, static_cast<int32_t>(b + 1));
            }
            builder.add_leaf(T_FORM_BONE_CHANNELS, fbc_payload);

            std::vector<uint8_t> ch_payload;
            wU32(ch_payload, 1);
            builder.add_leaf(T_FORM_CHANNEL_INFO, ch_payload);

            builder.end_container(fsk_node_idx);
            builder.end_container(fsk_sec_idx);
        }

        if (!model.meshes.empty()) {
            size_t fm_sec_idx = builder.begin_container(T_FORM_MESH_SECTION);
            for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
                const auto& mesh = model.meshes[mi];
                std::vector<uint8_t> fm_payload;
                wU32(fm_payload, static_cast<uint32_t>(mi + 1));
                size_t fm_node_idx = builder.begin_container(T_FORM_MESH, fm_payload);

                std::vector<uint8_t> info_payload;
                wI32(info_payload, 12);
                for (int k = 0; k < 7; ++k) wI32(info_payload, 0);
                builder.add_leaf(T_FORM_MESH_INFO, info_payload);

                size_t fmb_sec_idx = builder.begin_container(T_FORM_MESH_BONE_SECTION);
                std::vector<int32_t> palette = mesh.bone_index_map;
                if (palette.empty() && !model.bones.empty()) palette.push_back(0);

                for (int32_t b : palette) {
                    std::vector<uint8_t> fmb_payload;
                    wI32(fmb_payload, b);
                    for (int k = 0; k < 7; ++k) wI32(fmb_payload, 0);
                    builder.add_leaf(T_FORM_MESH_BONE, fmb_payload);
                }
                builder.end_container(fmb_sec_idx);
                builder.end_container(fm_node_idx);
            }
            builder.end_container(fm_sec_idx);
        }

        builder.end_container(form_node_idx);
        builder.end_container(form_sec_idx);
    }

    // 10. Model & Render Passes Section (0xca5e0e01)
    if (!model.meshes.empty()) {
        std::vector<uint8_t> msec_payload;
        wU32(msec_payload, 1);
        size_t model_sec_idx = builder.begin_container(T_MODEL_SECTION, msec_payload);
        size_t model_node_idx = builder.begin_container(T_MODEL, msec_payload);

        std::vector<uint8_t> rpsec_payload;
        wI32(rpsec_payload, 0); wI32(rpsec_payload, 1);
        size_t rp_sec_idx = builder.begin_container(T_RENDER_PASS_SECTION, rpsec_payload);

        for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
            const auto& mesh = model.meshes[mi];
            auto groups = mesh.tri_groups;
            if (groups.empty()) {
                GrnTriGroup g;
                g.material_index = mesh.material_index;
                g.material_name = mesh.material_name;
                g.faces = mesh.faces;
                g.face_uvs = mesh.face_uvs;
                g.face_normals = mesh.face_normals;
                groups.push_back(std::move(g));
            }

            size_t global_face_offset = 0;
            for (const auto& group : groups) {
                int32_t mat_1b = (group.material_index >= 0) ? (group.material_index + 1) : 1;
                std::vector<uint8_t> rp_payload;
                wI32(rp_payload, static_cast<int32_t>(mi));
                wI32(rp_payload, mat_1b);
                size_t rp_node_idx = builder.begin_container(T_RENDER_PASS, rp_payload);

                std::vector<uint8_t> src_payload;
                wU32(src_payload, 0);
                size_t src_con_idx = builder.begin_container(T_RENDER_PASS_SOURCE_CONTAINER, src_payload);
                builder.add_leaf(T_RENDER_PASS_SOURCE, src_payload);
                builder.end_container(src_con_idx);

                std::vector<uint8_t> rpt_payload;
                wI32(rpt_payload, static_cast<int32_t>(group.faces.size()));
                for (size_t f = 0; f < group.faces.size(); ++f) {
                    uint32_t u0 = (f < group.face_uvs.size()) ? group.face_uvs[f][0] : group.faces[f][0];
                    uint32_t u1 = (f < group.face_uvs.size()) ? group.face_uvs[f][1] : group.faces[f][1];
                    uint32_t u2 = (f < group.face_uvs.size()) ? group.face_uvs[f][2] : group.faces[f][2];
                    wI32(rpt_payload, static_cast<int32_t>(global_face_offset + f));
                    wU32(rpt_payload, u0);
                    wU32(rpt_payload, u1);
                    wU32(rpt_payload, u2);
                }
                builder.add_leaf(T_RENDER_PASS_TRIANGLES, rpt_payload);

                builder.end_container(rp_node_idx);
                global_face_offset += group.faces.size();
            }
        }
        builder.end_container(rp_sec_idx);

        std::vector<uint8_t> ref_payload;
        wU32(ref_payload, static_cast<uint32_t>(model_dext + 1));
        builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);

        builder.end_container(model_node_idx);
        builder.end_container(model_sec_idx);
    } else {
        std::vector<uint8_t> msec_payload;
        wU32(msec_payload, 1);
        size_t model_sec_idx = builder.begin_container(T_MODEL_SECTION, msec_payload);
        std::vector<uint8_t> ref_payload;
        wU32(ref_payload, static_cast<uint32_t>(model_dext + 1));
        builder.add_leaf(T_DATA_EXTENSION_REFERENCE, ref_payload);
        builder.end_container(model_sec_idx);
    }

    // 11. Animation Section (0xca5e1205)
    if (!model.animations.empty()) {
        size_t anim_sec_idx = builder.begin_container(T_ANIMATION_SECTION);
        for (const auto& anim : model.animations) {
            size_t anim_node_idx = builder.begin_container(T_ANIMATION);
            builder.add_leaf(T_ANIMATION_HEADER);
            if (anim.tracks.empty()) {
                builder.add_leaf(T_ANIMATION_TRANSFORM_TRACK_SECTION);
            } else {
                size_t track_sec_idx = builder.begin_container(T_ANIMATION_TRANSFORM_TRACK_SECTION);
                for (const auto& track : anim.tracks) {
                    std::vector<uint8_t> trk_payload;
                    wI32(trk_payload, track.channel_id);
                    wI32(trk_payload, 0);
                    if (track.format == "split") {
                        wI32(trk_payload, 1);
                        wI32(trk_payload, track.position_interp_mode);
                        wI32(trk_payload, track.quaternion_interp_mode);
                        wI32(trk_payload, track.scale_shear_interp_mode);
                        wI32(trk_payload, static_cast<int32_t>(track.translations.size()));
                        wI32(trk_payload, static_cast<int32_t>(track.rotations.size()));
                        wI32(trk_payload, static_cast<int32_t>(track.scale_shears.size()));
                        for (int k = 0; k < 4; ++k) wI32(trk_payload, 0);
                        for (float t : track.translation_times) wF32(trk_payload, t);
                        for (float t : track.rotation_times) wF32(trk_payload, t);
                        for (float t : track.scale_shear_times) wF32(trk_payload, t);
                        for (const auto& tr : track.translations) wVec3(trk_payload, tr);
                        for (const auto& rot : track.rotations) wVec4(trk_payload, rot);
                        for (const auto& m : track.scale_shears) {
                            for (int k = 0; k < 9; ++k) wF32(trk_payload, m[k]);
                        }
                    } else {
                        wI32(trk_payload, 0);
                        for (size_t f = 0; f < track.times.size(); ++f) {
                            wF32(trk_payload, track.times[f]);
                            wVec3(trk_payload, (f < track.translations.size()) ? track.translations[f] : Vec3{});
                            wVec4(trk_payload, (f < track.rotations.size()) ? track.rotations[f] : Vec4{0,0,0,1});
                            for (int k = 0; k < 9; ++k) {
                                float val = (f < track.scale_shears.size()) ? track.scale_shears[f][k] : (k % 4 == 0 ? 1.0f : 0.0f);
                                wF32(trk_payload, val);
                            }
                        }
                    }
                    builder.add_leaf(T_ANIMATION_TRANSFORM_TRACK_KEYS, trk_payload);
                }
                builder.end_container(track_sec_idx);
            }
            builder.end_container(anim_node_idx);
        }
        builder.end_container(anim_sec_idx);
    } else {
        builder.add_leaf(T_ANIMATION_SECTION);
    }

    // 12. Null Terminator
    builder.add_leaf(T_NULL_TERMINATOR);

    // Assemble Sections
    auto sec0_data = build_section_0();

    uint32_t chunk_count = static_cast<uint32_t>(builder.flat_chunks.size());
    uint32_t chunk_table_size = chunk_count * 12;
    uint32_t payload_base_offset = 16 + chunk_table_size;

    std::vector<uint8_t> sec1_data;
    wU32(sec1_data, chunk_count);
    wU32(sec1_data, 0);
    wU32(sec1_data, payload_base_offset);
    wU32(sec1_data, 0);

    for (const auto& entry : builder.flat_chunks) {
        uint32_t abs_data_off = payload_base_offset + entry.rel_data_offset;
        wU32(sec1_data, entry.tag);
        wU32(sec1_data, abs_data_off);
        wU32(sec1_data, entry.child_budget);
    }
    sec1_data.insert(sec1_data.end(), builder.payload.begin(), builder.payload.end());
    size_t pad1 = (16 - (sec1_data.size() % 16)) % 16;
    if (pad1) sec1_data.insert(sec1_data.end(), pad1, 0);

    // Section 2: Footer / Relocations
    std::vector<uint8_t> sec2_data;
    wU32(sec2_data, 1); wU32(sec2_data, 0); wU32(sec2_data, 0x1C); wU32(sec2_data, 0);
    wU32(sec2_data, T_NULL_TERMINATOR); wU32(sec2_data, 0x1C); wU32(sec2_data, 0);
    size_t pad2 = (16 - (sec2_data.size() % 16)) % 16;
    if (pad2) sec2_data.insert(sec2_data.end(), pad2, 0);

    // Calculate section offsets
    uint32_t sec0_off = 0x9C;
    uint32_t sec1_off = sec0_off + static_cast<uint32_t>(sec0_data.size());
    uint32_t sec2_off = sec1_off + static_cast<uint32_t>(sec1_data.size());
    uint32_t total_size = sec2_off + static_cast<uint32_t>(sec2_data.size());

    uint32_t crc0 = calc_crc32(sec0_data.data(), sec0_data.size());
    uint32_t crc1 = calc_crc32(sec1_data.data(), sec1_data.size());
    uint32_t crc2 = calc_crc32(sec2_data.data(), sec2_data.size());

    std::vector<uint8_t> sec_table;
    wU32(sec_table, T_SECTION_HEADER); wU32(sec_table, 0); wU32(sec_table, sec0_off); wU32(sec_table, crc0); wU32(sec_table, 0);
    wU32(sec_table, T_SECTION_PAYLOAD); wU32(sec_table, 0); wU32(sec_table, sec1_off); wU32(sec_table, crc1); wU32(sec_table, 0);
    wU32(sec_table, T_SECTION_FOOTER); wU32(sec_table, 0); wU32(sec_table, sec2_off); wU32(sec_table, crc2); wU32(sec_table, 0);

    uint32_t hdr_crc = (crc0 ^ crc1 ^ crc2);
    std::vector<uint8_t> grn_hdr;
    wU32(grn_hdr, T_FILE_DIRECTORY);
    wU32(grn_hdr, 3);
    wU32(grn_hdr, hdr_crc);
    wU32(grn_hdr, 0);
    wU32(grn_hdr, total_size - 64);
    wU32(grn_hdr, 0);
    wU32(grn_hdr, 0);
    wU32(grn_hdr, 0);

    std::vector<uint8_t> full_file;
    full_file.reserve(total_size);
    full_file.insert(full_file.end(), DEFAULT_SIGNATURE, DEFAULT_SIGNATURE + 64);
    full_file.insert(full_file.end(), grn_hdr.begin(), grn_hdr.end());
    full_file.insert(full_file.end(), sec_table.begin(), sec_table.end());
    full_file.insert(full_file.end(), sec0_data.begin(), sec0_data.end());
    full_file.insert(full_file.end(), sec1_data.begin(), sec1_data.end());
    full_file.insert(full_file.end(), sec2_data.begin(), sec2_data.end());

    return full_file;
}

bool write_grn_file(const std::filesystem::path& path, const GrnModel& model) {
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    auto bytes = write_grn_memory(model);
    if (bytes.empty()) return false;

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return out.good();
}

} // namespace grn
