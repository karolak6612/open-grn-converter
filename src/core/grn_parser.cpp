/**
 * @file grn_parser.cpp
 * @brief Complete container and chunk parser for GRN binary files.
 */

#include "grn_parser.h"
#include "../codecs/dxt_codec.h"
#include "../codecs/vtex_codec.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <iostream>

namespace grn {

static inline uint32_t rU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static inline int32_t rI32(const uint8_t* p) {
    return static_cast<int32_t>(rU32(p));
}

static inline float rF32(const uint8_t* p) {
    float f = 0.0f;
    std::memcpy(&f, p, 4);
    return f;
}

static inline Vec3 rVec3(const uint8_t* p) {
    return Vec3{rF32(p), rF32(p + 4), rF32(p + 8)};
}

static inline Vec4 rVec4(const uint8_t* p) {
    return Vec4{rF32(p), rF32(p + 4), rF32(p + 8), rF32(p + 12)};
}

struct FlatChunk {
    uint32_t tag = 0;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
};

static std::pair<std::vector<GrnChunkNode>, size_t> build_chunk_tree(
    const std::vector<FlatChunk>& chunks, size_t start_idx, uint32_t budget)
{
    std::vector<GrnChunkNode> nodes;
    uint32_t consumed = 0;
    size_t i = start_idx;
    size_t n = chunks.size();

    while (consumed < budget && i < n) {
        const auto& entry = chunks[i];
        uint32_t child_budget = entry.data_size;
        i++;

        auto [children, next_i] = build_chunk_tree(chunks, i, child_budget);
        i = next_i;

        GrnChunkNode node;
        node.tag = entry.tag;
        node.data_offset = entry.data_offset;
        node.data_size = entry.data_size;
        node.children = std::move(children);
        nodes.push_back(std::move(node));

        consumed += 1 + child_budget;
    }
    return {nodes, i};
}

static void find_all(const std::vector<GrnChunkNode>& nodes, uint32_t tag, std::vector<const GrnChunkNode*>& out) {
    for (const auto& n : nodes) {
        if (n.tag == tag) out.push_back(&n);
        find_all(n.children, tag, out);
    }
}

static const GrnChunkNode* find_one(const std::vector<GrnChunkNode>& nodes, uint32_t tag) {
    for (const auto& n : nodes) {
        if (n.tag == tag) return &n;
        const auto* res = find_one(n.children, tag);
        if (res) return res;
    }
    return nullptr;
}

static uint32_t next_offset(const std::vector<uint32_t>& all_offsets, uint32_t off, uint32_t default_end) {
    auto it = std::upper_bound(all_offsets.begin(), all_offsets.end(), off);
    return (it != all_offsets.end()) ? *it : default_end;
}

static std::vector<Vec3> read_vec3_span(const uint8_t* data, size_t off, size_t end) {
    std::vector<Vec3> out;
    if (off >= end) return out;
    size_t count = (end - off) / 12;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        out.push_back(rVec3(data + off + i * 12));
    }
    return out;
}

static std::vector<std::string> decode_string_table(const uint8_t* data, size_t data_len, const GrnChunkNode& node) {
    std::vector<std::string> strings;
    size_t off = node.data_offset;
    if (off + 8 > data_len) return strings;

    int32_t num_strings = rI32(data + off);
    off += 8; // skip count and str_len

    for (int32_t i = 0; i < num_strings && off < data_len; ++i) {
        const char* start = reinterpret_cast<const char*>(data + off);
        size_t len = strnlen(start, data_len - off);
        strings.emplace_back(start, len);
        off += len + 1;
    }
    return strings;
}

using DataExtProps = std::unordered_map<std::string, std::string>;

static std::vector<DataExtProps> decode_data_extensions(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    const std::vector<std::string>& strings)
{
    std::vector<DataExtProps> out;
    const auto* dext_sec = find_one(roots, T_DATA_EXTENSION_SECTION);
    if (!dext_sec) return out;

    for (const auto& dext : dext_sec->children) {
        if (dext.tag != T_DATA_EXTENSION) continue;
        DataExtProps props;

        const auto* prop_sec = find_one(dext.children, T_DATA_EXTENSION_PROPERTY_SECTION);
        const auto& prop_nodes = prop_sec ? prop_sec->children : dext.children;

        for (const auto& prop : prop_nodes) {
            if (prop.tag != T_DATA_EXTENSION_PROPERTY) continue;
            if (prop.data_offset + 4 > data_len) continue;

            int32_t k_idx = rI32(data + prop.data_offset);
            std::string key = (k_idx >= 0 && static_cast<size_t>(k_idx) < strings.size())
                              ? strings[k_idx] : "";

            const auto* val_sec = find_one(prop.children, T_DATA_EXTENSION_VALUE_SECTION);
            const auto& val_nodes = val_sec ? val_sec->children : prop.children;
            const auto* val_node = find_one(val_nodes, T_DATA_EXTENSION_PROPERTY_VALUE);

            std::string val;
            if (val_node && val_node->data_offset + 8 <= data_len) {
                int32_t v_idx = rI32(data + val_node->data_offset + 4);
                if (v_idx >= 0 && static_cast<size_t>(v_idx) < strings.size()) {
                    val = strings[v_idx];
                }
            }
            props[key] = val;
        }
        out.push_back(std::move(props));
    }
    return out;
}

static std::optional<int32_t> get_dext_ref(const uint8_t* data, size_t data_len, const GrnChunkNode& node) {
    const auto* ref = find_one(node.children, T_DATA_EXTENSION_REFERENCE);
    if (!ref || ref->data_offset + 4 > data_len) return std::nullopt;
    int32_t idx = rI32(data + ref->data_offset);
    return (idx > 0) ? std::optional<int32_t>(idx - 1) : std::nullopt;
}

static std::string dext_lookup(const std::vector<DataExtProps>& dexts, std::optional<int32_t> idx, const std::string& key) {
    if (!idx.has_value() || *idx < 0 || static_cast<size_t>(*idx) >= dexts.size()) return "";
    auto it = dexts[*idx].find(key);
    return (it != dexts[*idx].end()) ? it->second : "";
}

static std::vector<std::optional<int32_t>> decode_transform_channels(
    const uint8_t* data, size_t data_len, const std::vector<GrnChunkNode>& roots)
{
    std::vector<std::optional<int32_t>> channels;
    const auto* tc_sec = find_one(roots, T_TRANSFORM_CHANNEL_SECTION);
    if (!tc_sec) return channels;

    for (const auto& tc : tc_sec->children) {
        if (tc.tag == T_TRANSFORM_CHANNEL) {
            channels.push_back(get_dext_ref(data, data_len, tc));
        }
    }
    return channels;
}

static std::vector<GrnBone> decode_bones(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    const std::vector<DataExtProps>& dexts,
    const std::vector<uint32_t>& all_offsets,
    uint32_t sec_end)
{
    struct RawBone {
        int32_t parent = -1;
        Vec3 pos;
        Vec4 rot;
        std::array<float, 9> scale{};
    };

    std::vector<RawBone> raw_bones;
    const auto* skel_sec = find_one(roots, T_SKELETON_SECTION);
    if (skel_sec) {
        const auto* skeleton = find_one(skel_sec->children, T_SKELETON);
        const auto* bone_sec = skeleton ? find_one(skeleton->children, T_BONE_SECTION) : nullptr;
        if (bone_sec) {
            for (const auto& bn : bone_sec->children) {
                if (bn.tag == T_BONE && bn.data_offset + 68 <= data_len) {
                    RawBone b;
                    size_t off = bn.data_offset;
                    b.parent = rI32(data + off);
                    b.pos = rVec3(data + off + 4);
                    b.rot = rVec4(data + off + 16);
                    for (int k = 0; k < 9; ++k) {
                        b.scale[k] = rF32(data + off + 32 + k * 4);
                    }
                    raw_bones.push_back(b);
                }
            }
        }
    }

    auto tc_list = decode_transform_channels(data, data_len, roots);

    // Form skeleton bone channels
    std::vector<int32_t> fbc_indices;
    const auto* form_sec = find_one(roots, T_FORM_SECTION);
    if (form_sec) {
        const auto* form = find_one(form_sec->children, T_FORM);
        const auto* fsk_sec = form ? find_one(form->children, T_FORM_SKELETON_SECTION) : nullptr;
        const auto* fsk = fsk_sec ? find_one(fsk_sec->children, T_FORM_SKELETON) : nullptr;
        const auto* fbc = fsk ? find_one(fsk->children, T_FORM_BONE_CHANNELS) : nullptr;
        if (fbc) {
            size_t off = fbc->data_offset;
            size_t end = next_offset(all_offsets, static_cast<uint32_t>(off), sec_end);
            size_t count = (end > off) ? (end - off) / 4 : 0;
            for (size_t i = 0; i < count && off + 4 <= data_len; ++i) {
                fbc_indices.push_back(rI32(data + off));
                off += 4;
            }
        }
    }

    std::vector<GrnBone> bones;
    bones.reserve(raw_bones.size());
    for (size_t i = 0; i < raw_bones.size(); ++i) {
        std::optional<int32_t> dext_idx;
        if (i < fbc_indices.size()) {
            int32_t tc_1b = fbc_indices[i];
            if (tc_1b > 0 && static_cast<size_t>(tc_1b) <= tc_list.size()) {
                dext_idx = tc_list[tc_1b - 1];
            }
        }

        std::string name = dext_lookup(dexts, dext_idx, "__ObjectName");
        if (name.empty()) name = "Bone_" + std::to_string(i);

        GrnBone bone;
        bone.index = static_cast<int32_t>(i);
        bone.name = name;
        bone.parent_index = raw_bones[i].parent;
        bone.position = raw_bones[i].pos;
        bone.rotation = raw_bones[i].rot;
        bone.scale_3x3 = raw_bones[i].scale;
        bones.push_back(std::move(bone));
    }
    return bones;
}

static std::vector<GrnMesh> decode_meshes(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    const std::vector<DataExtProps>& dexts,
    const std::vector<uint32_t>& all_offsets,
    uint32_t sec_end)
{
    std::vector<GrnMesh> meshes;
    const auto* mesh_sec = find_one(roots, T_MESH_SECTION);
    if (!mesh_sec) return meshes;

    for (const auto& mn : mesh_sec->children) {
        if (mn.tag != T_MESH) continue;

        GrnMesh mesh;
        const auto* vset_sec = find_one(mn.children, T_MESH_VERTEX_SET_SECTION);
        const auto* vset = vset_sec ? find_one(vset_sec->children, T_MESH_VERTEX_SET) : nullptr;

        if (vset) {
            const auto* v_node = find_one(vset->children, T_MESH_VERTICES);
            if (v_node) {
                uint32_t end = next_offset(all_offsets, v_node->data_offset, sec_end);
                mesh.vertices = read_vec3_span(data, v_node->data_offset, end);
            }

            const auto* n_node = find_one(vset->children, T_MESH_NORMALS);
            if (n_node) {
                uint32_t end = next_offset(all_offsets, n_node->data_offset, sec_end);
                mesh.normals = read_vec3_span(data, n_node->data_offset, end);
            }

            const auto* field_sec = find_one(vset->children, T_MESH_FIELD_SECTION);
            const auto* f_node = field_sec ? find_one(field_sec->children, T_MESH_FIELD) : nullptr;
            if (f_node) {
                uint32_t end = next_offset(all_offsets, f_node->data_offset, sec_end);
                if (f_node->data_offset + 4 < end) {
                    auto uv_raw = read_vec3_span(data, f_node->data_offset + 4, end);
                    mesh.uvs.reserve(uv_raw.size());
                    for (const auto& v : uv_raw) {
                        mesh.uvs.push_back({v.x, v.y});
                    }
                }
            }
        }

        // Triangles
        const auto* t_node = find_one(mn.children, T_MESH_TRIANGLES);
        if (t_node) {
            uint32_t end = next_offset(all_offsets, t_node->data_offset, sec_end);
            size_t off = t_node->data_offset;
            size_t fc = (end > off) ? (end - off) / 24 : 0;
            mesh.faces.reserve(fc);
            mesh.face_normals.reserve(fc);
            for (size_t i = 0; i < fc && off + 24 <= data_len; ++i) {
                uint32_t v0 = rU32(data + off + 0);
                uint32_t v1 = rU32(data + off + 4);
                uint32_t v2 = rU32(data + off + 8);
                uint32_t n0 = rU32(data + off + 12);
                uint32_t n1 = rU32(data + off + 16);
                uint32_t n2 = rU32(data + off + 20);
                mesh.faces.push_back({{v0, v1, v2}});
                mesh.face_normals.push_back({{n0, n1, n2}});
                off += 24;
            }
        }

        // Weights
        const auto* w_node = find_one(mn.children, T_MESH_WEIGHTS);
        if (w_node && w_node->data_offset + 12 <= data_len) {
            size_t off = w_node->data_offset;
            int32_t w_count = rI32(data + off);
            int32_t highest_bone = rI32(data + off + 4);
            mesh.bone_count = (highest_bone >= 0) ? static_cast<uint32_t>(highest_bone + 1) : 0;
            off += 12;

            if (w_count > 0 && w_count < 200000) {
                mesh.weights.reserve(w_count);
                for (int32_t i = 0; i < w_count && off + 4 <= data_len; ++i) {
                    int32_t bc = rI32(data + off);
                    off += 4;
                    VertexWeight vw;
                    for (int32_t b = 0; b < bc && off + 8 <= data_len; ++b) {
                        vw.bone_indices.push_back(rI32(data + off));
                        vw.bone_weights.push_back(rF32(data + off + 4));
                        off += 8;
                    }
                    mesh.weights.push_back(std::move(vw));
                }
            }
        }

        auto dext_idx = get_dext_ref(data, data_len, mn);
        mesh.name = dext_lookup(dexts, dext_idx, "__ObjectName");
        if (mesh.name.empty()) mesh.name = "Mesh_" + std::to_string(meshes.size());

        meshes.push_back(std::move(mesh));
    }
    return meshes;
}

static std::vector<GrnTexture> decode_textures(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    const std::vector<DataExtProps>& dexts,
    const std::vector<uint32_t>& all_offsets,
    uint32_t sec_end)
{
    std::vector<GrnTexture> textures;
    const auto* tex_sec = find_one(roots, T_TEXTURE_SECTION);
    if (!tex_sec) return textures;

    for (const auto& tmap : tex_sec->children) {
        if (tmap.tag != T_TEXTURE_MAP) continue;

        GrnTexture tex;
        const auto* img_sec = find_one(tmap.children, T_TEXTURE_IMAGE_SECTION);
        const auto* img = img_sec ? find_one(img_sec->children, T_TEXTURE_MAP_IMAGE) : nullptr;

        if (img && img->data_offset + 12 <= data_len) {
            size_t off = img->data_offset;
            tex.width = static_cast<uint32_t>(rI32(data + off));
            tex.height = static_cast<uint32_t>(rI32(data + off + 4));
            tex.format_code = static_cast<uint32_t>(rI32(data + off + 8));

            uint32_t end = next_offset(all_offsets, img->data_offset, sec_end);
            size_t trailing = (end > off + 12) ? (end - (off + 12)) : 0;

            if (trailing >= 4 && std::memcmp(data + off + 12, "BIKi", 4) == 0) {
                // VTex Video Texture
                tex.format_str = "vtex";
                tex.raw_blob.assign(data + off + 12, data + end);
                auto decoded = decode_vtex(tex.raw_blob.data(), tex.raw_blob.size(),
                                          tex.width, tex.height, tex.format_code);
                if (decoded) {
                    tex.decoded_rgba = std::move(decoded->pixels);
                    tex.has_alpha = decoded->has_alpha;
                    tex.is_placeholder = decoded->is_placeholder;
                }
            } else if (tex.format_code == 8 && tex.width > 0 && tex.height > 0) {
                // DXT1 / BC1 Compressed
                tex.format_str = "dxt1";
                size_t dxt_len = ((tex.width + 3) / 4) * ((tex.height + 3) / 4) * 8;
                if (trailing >= dxt_len) {
                    tex.raw_blob.assign(data + off + 12, data + off + 12 + dxt_len);
                    tex.decoded_rgba = decode_dxt1(tex.raw_blob.data(), tex.raw_blob.size(),
                                                   tex.width, tex.height);
                }
            } else if ((tex.format_code == 0 || tex.format_code == 1 || tex.format_code == 6) &&
                       tex.width > 0 && tex.height > 0) {
                // Raw uncompressed pixels
                tex.format_str = "raw";
                size_t bpp = (tex.format_code == 6) ? 3 : 4;
                size_t raw_len = tex.width * tex.height * bpp;
                if (trailing >= raw_len) {
                    tex.raw_blob.assign(data + off + 12, data + off + 12 + raw_len);
                    tex.decoded_rgba.resize(tex.width * tex.height * 4);
                    if (bpp == 4) {
                        std::memcpy(tex.decoded_rgba.data(), tex.raw_blob.data(), raw_len);
                        tex.has_alpha = (tex.format_code == 1);
                    } else {
                        // 24-bit RGB to 32-bit RGBA
                        for (size_t p = 0; p < tex.width * tex.height; ++p) {
                            tex.decoded_rgba[p * 4 + 0] = tex.raw_blob[p * 3 + 0];
                            tex.decoded_rgba[p * 4 + 1] = tex.raw_blob[p * 3 + 1];
                            tex.decoded_rgba[p * 4 + 2] = tex.raw_blob[p * 3 + 2];
                            tex.decoded_rgba[p * 4 + 3] = 255;
                        }
                    }
                }
            } else {
                // External reference
                tex.format_str = "external";
                tex.is_external = true;
                if (trailing > 0) {
                    tex.raw_blob.assign(data + off + 12, data + end);
                }
            }
        }

        auto dext_idx = get_dext_ref(data, data_len, tmap);
        tex.name = dext_lookup(dexts, dext_idx, "__ObjectName");
        tex.file_name = dext_lookup(dexts, dext_idx, "__FileName");
        if (tex.name.empty()) tex.name = "Texture_" + std::to_string(textures.size());

        textures.push_back(std::move(tex));
    }
    return textures;
}

static std::vector<GrnMaterial> decode_materials(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    const std::vector<DataExtProps>& dexts)
{
    std::vector<GrnMaterial> materials;
    const auto* mat_sec = find_one(roots, T_MATERIAL_SECTION);
    if (!mat_sec) return materials;

    for (const auto& mat_node : mat_sec->children) {
        if (mat_node.tag != T_MATERIAL) continue;

        GrnMaterial mat;
        const auto* diff = find_one(mat_node.children, T_MATERIAL_SIMPLE_DIFFUSE_TEXTURE);
        if (diff && diff->data_offset + 8 <= data_len) {
            int32_t t_1b = rI32(data + diff->data_offset + 4);
            mat.diffuse_texture_index = (t_1b > 0) ? (t_1b - 1) : -1;
        }

        auto dext_idx = get_dext_ref(data, data_len, mat_node);
        mat.name = dext_lookup(dexts, dext_idx, "__ObjectName");
        if (mat.name.empty()) mat.name = "Material_" + std::to_string(materials.size());

        materials.push_back(std::move(mat));
    }
    return materials;
}

static void link_model_and_form(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    std::vector<GrnMesh>& meshes,
    const std::vector<GrnMaterial>& materials,
    const std::vector<uint32_t>& all_offsets,
    uint32_t sec_end)
{
    // Decode form mesh mapping
    std::vector<int32_t> form_mesh_map;
    std::vector<std::vector<int32_t>> bone_bindings;

    const auto* form_sec = find_one(roots, T_FORM_SECTION);
    if (form_sec) {
        const auto* form = find_one(form_sec->children, T_FORM);
        const auto* fm_sec = form ? find_one(form->children, T_FORM_MESH_SECTION) : nullptr;
        if (fm_sec) {
            for (const auto& fm : fm_sec->children) {
                if (fm.tag == T_FORM_MESH && fm.data_offset + 4 <= data_len) {
                    form_mesh_map.push_back(rI32(data + fm.data_offset) - 1);
                    std::vector<int32_t> b_list;
                    const auto* fmb_sec = find_one(fm.children, T_FORM_MESH_BONE_SECTION);
                    if (fmb_sec) {
                        for (const auto& c : fmb_sec->children) {
                            if (c.tag == T_FORM_MESH_BONE && c.data_offset + 4 <= data_len) {
                                b_list.push_back(rI32(data + c.data_offset));
                            }
                        }
                    }
                    bone_bindings.push_back(std::move(b_list));
                }
            }
        }
    }

    for (size_t fmi = 0; fmi < form_mesh_map.size(); ++fmi) {
        int32_t mi = form_mesh_map[fmi];
        if (mi >= 0 && static_cast<size_t>(mi) < meshes.size()) {
            if (fmi < bone_bindings.size() && !bone_bindings[fmi].empty()) {
                meshes[mi].bone_index_map = bone_bindings[fmi];
            }
        }
    }

    // Link materials and render passes
    const auto* model_sec = find_one(roots, T_MODEL_SECTION);
    if (!model_sec) return;
    const auto* model_node = find_one(model_sec->children, T_MODEL);
    if (!model_node) return;

    const auto* rp_sec = find_one(model_node->children, T_RENDER_PASS_SECTION);
    const auto& rp_nodes = rp_sec ? rp_sec->children : model_node->children;

    for (const auto& rp : rp_nodes) {
        if (rp.tag != T_RENDER_PASS || rp.data_offset + 8 > data_len) continue;

        int32_t fm_idx = rI32(data + rp.data_offset);
        int32_t mat_1b = rI32(data + rp.data_offset + 4);

        int32_t mesh_idx = (fm_idx >= 0 && static_cast<size_t>(fm_idx) < form_mesh_map.size())
                           ? form_mesh_map[fm_idx] : -1;

        if (mesh_idx >= 0 && static_cast<size_t>(mesh_idx) < meshes.size()) {
            auto& mesh = meshes[mesh_idx];
            int32_t mat_idx = (mat_1b > 0) ? (mat_1b - 1) : -1;
            std::string mat_name;
            if (mat_idx >= 0 && static_cast<size_t>(mat_idx) < materials.size()) {
                mesh.material_index = mat_idx;
                mesh.material_name = materials[mat_idx].name;
                mat_name = materials[mat_idx].name;
            }

            GrnTriGroup group;
            group.material_index = mat_idx;
            group.material_name = mat_name;

            // Decode render pass UV triangle indices
            const auto* rpt = find_one(rp.children, T_RENDER_PASS_TRIANGLES);
            if (rpt && rpt->data_offset + 4 <= data_len) {
                size_t off = rpt->data_offset;
                int32_t count = rI32(data + off);
                off += 4;
                uint32_t end = next_offset(all_offsets, rpt->data_offset, sec_end);
                size_t avail = (end > off) ? (end - off) : 0;
                size_t stride = 16;
                if (count > 0 && avail >= static_cast<size_t>(count) * 4) {
                    size_t s = avail / count;
                    if (s == 4 || s == 16 || s == 28) {
                        stride = s;
                    }
                }

                mesh.face_uvs.resize(mesh.faces.size(), {{0, 0, 0}});
                for (int32_t f = 0; f < count && off + stride <= data_len; ++f) {
                    int32_t face_idx = rI32(data + off);
                    uint32_t u0 = 0, u1 = 0, u2 = 0;
                    if (stride == 28) {
                        // 2 texture channels: channel 0 UVs for vertices 0, 1, 2
                        u0 = rU32(data + off + 4);
                        u1 = rU32(data + off + 12);
                        u2 = rU32(data + off + 20);
                    } else if (stride == 16) {
                        // 1 texture channel
                        u0 = rU32(data + off + 4);
                        u1 = rU32(data + off + 8);
                        u2 = rU32(data + off + 12);
                    }
                    if (face_idx >= 0 && static_cast<size_t>(face_idx) < mesh.face_uvs.size()) {
                        mesh.face_uvs[face_idx] = {{u0, u1, u2}};
                    }
                    if (face_idx >= 0 && static_cast<size_t>(face_idx) < mesh.faces.size()) {
                        group.faces.push_back(mesh.faces[face_idx]);
                        group.face_uvs.push_back({{u0, u1, u2}});
                        if (face_idx < static_cast<int32_t>(mesh.face_normals.size())) {
                            group.face_normals.push_back(mesh.face_normals[face_idx]);
                        } else {
                            group.face_normals.push_back(mesh.faces[face_idx]);
                        }
                    }
                    off += stride;
                }
            }
            if (!group.faces.empty()) {
                mesh.tri_groups.push_back(std::move(group));
            }
        }
    }

    // Ensure that all faces in mesh.faces are covered by tri_groups
    for (auto& mesh : meshes) {
        if (mesh.faces.empty()) continue;

        if (mesh.tri_groups.empty()) {
            GrnTriGroup group;
            group.material_index = mesh.material_index;
            group.material_name = mesh.material_name;
            group.faces = mesh.faces;
            group.face_uvs = mesh.face_uvs;
            group.face_normals = mesh.face_normals;
            mesh.tri_groups.push_back(std::move(group));
        } else {
            size_t total_covered = 0;
            for (const auto& g : mesh.tri_groups) {
                total_covered += g.faces.size();
            }
            if (total_covered < mesh.faces.size()) {
                if (mesh.tri_groups.size() == 1) {
                    mesh.tri_groups[0].faces = mesh.faces;
                    mesh.tri_groups[0].face_uvs = mesh.face_uvs;
                    mesh.tri_groups[0].face_normals = mesh.face_normals;
                } else {
                    std::set<std::array<uint32_t, 3>> covered_set;
                    for (const auto& g : mesh.tri_groups) {
                        for (const auto& f : g.faces) covered_set.insert(f);
                    }
                    GrnTriGroup remainder;
                    remainder.material_index = mesh.material_index;
                    remainder.material_name = mesh.material_name;
                    for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
                        if (!covered_set.count(mesh.faces[fi])) {
                            remainder.faces.push_back(mesh.faces[fi]);
                            if (fi < mesh.face_uvs.size()) remainder.face_uvs.push_back(mesh.face_uvs[fi]);
                            else remainder.face_uvs.push_back(mesh.faces[fi]);
                            if (fi < mesh.face_normals.size()) remainder.face_normals.push_back(mesh.face_normals[fi]);
                            else remainder.face_normals.push_back(mesh.faces[fi]);
                        }
                    }
                    if (!remainder.faces.empty()) {
                        mesh.tri_groups.push_back(std::move(remainder));
                    }
                }
            }
        }
    }
}

/**
 * @brief Decodes animation clips and transform tracks from the GRN container.
 * @param data Pointer to the section payload bytes.
 * @param data_len Length of the section payload buffer.
 * @param roots Root hierarchy chunk nodes.
 * @param dexts Decoded data extension properties for string lookups.
 * @param tc_list Decoded transform channel references.
 * @param all_offsets Sorted list of chunk data offsets for boundary checks.
 * @param sec_end End boundary offset of the section payload.
 * @return Vector of decoded animation structures.
 */
static std::vector<GrnAnimation> decode_animations(
    const uint8_t* data, size_t data_len,
    const std::vector<GrnChunkNode>& roots,
    const std::vector<DataExtProps>& dexts,
    const std::vector<std::optional<int32_t>>& tc_list,
    const std::vector<uint32_t>& all_offsets,
    uint32_t sec_end)
{
    std::vector<GrnAnimation> animations;
    const auto* anim_sec = find_one(roots, T_ANIMATION_SECTION);
    if (!anim_sec) return animations;

    // Check for animation chunks under the section
    for (const auto& anim_node : anim_sec->children) {
        if (anim_node.tag != T_ANIMATION) continue;

        GrnAnimation anim;
        auto dext_idx = get_dext_ref(data, data_len, anim_node);
        anim.name = dext_lookup(dexts, dext_idx, "__ObjectName");
        if (anim.name.empty()) anim.name = "Animation";
        anim.duration = 0.0f;
        anim.fps = 30.0f;

        const auto* atts = find_one(anim_node.children, T_ANIMATION_TRANSFORM_TRACK_SECTION);
        if (atts) {
            float max_time = 0.0f;
            for (const auto& track_node : atts->children) {
                if (track_node.tag != T_ANIMATION_TRANSFORM_TRACK_KEYS || track_node.data_offset + 12 > data_len) {
                    continue;
                }
                size_t off = track_node.data_offset;
                int32_t channel_id = rI32(data + off);
                int32_t format_flag = rI32(data + off + 8);
                uint32_t end = next_offset(all_offsets, static_cast<uint32_t>(off), sec_end);

                std::string bone_name;
                int32_t tc_idx = channel_id - 1;
                if (tc_idx >= 0 && static_cast<size_t>(tc_idx) < tc_list.size()) {
                    auto b_dext = tc_list[tc_idx];
                    bone_name = dext_lookup(dexts, b_dext, "__ObjectName");
                }
                if (bone_name.empty()) bone_name = "Bone_" + std::to_string(channel_id);

                AnimTrack track;
                track.channel_id = channel_id;
                track.bone_name = bone_name;

                if (format_flag != 0 && off + 0x34 <= data_len) {
                    track.format = "split";
                    track.position_interp_mode = rI32(data + off + 0x0C);
                    track.quaternion_interp_mode = rI32(data + off + 0x10);
                    track.scale_shear_interp_mode = rI32(data + off + 0x14);
                    int32_t tc = rI32(data + off + 0x18);
                    int32_t rc = rI32(data + off + 0x1C);
                    int32_t sc = rI32(data + off + 0x20);

                    size_t p = off + 0x34;
                    for (int32_t i = 0; i < tc && p + 4 <= data_len; ++i) {
                        float t = rF32(data + p);
                        track.translation_times.push_back(t);
                        max_time = std::max(max_time, t);
                        p += 4;
                    }
                    for (int32_t i = 0; i < rc && p + 4 <= data_len; ++i) {
                        float t = rF32(data + p);
                        track.rotation_times.push_back(t);
                        max_time = std::max(max_time, t);
                        p += 4;
                    }
                    for (int32_t i = 0; i < sc && p + 4 <= data_len; ++i) {
                        float t = rF32(data + p);
                        track.scale_shear_times.push_back(t);
                        max_time = std::max(max_time, t);
                        p += 4;
                    }
                    for (int32_t i = 0; i < tc && p + 12 <= data_len; ++i) {
                        track.translations.push_back(rVec3(data + p));
                        p += 12;
                    }
                    for (int32_t i = 0; i < rc && p + 16 <= data_len; ++i) {
                        track.rotations.push_back(rVec4(data + p));
                        p += 16;
                    }
                    for (int32_t i = 0; i < sc && p + 36 <= data_len; ++i) {
                        std::array<float, 9> m{};
                        for (int k = 0; k < 9; ++k) m[k] = rF32(data + p + k * 4);
                        track.scale_shears.push_back(m);
                        p += 36;
                    }
                } else {
                    track.format = "interleaved";
                    size_t HEADER = 12, FRAME = 68;
                    size_t span = (end > off) ? (end - off) : 0;
                    size_t num_frames = (span > HEADER) ? (span - HEADER) / FRAME : 0;
                    size_t p = off + HEADER;
                    for (size_t f = 0; f < num_frames && p + FRAME <= data_len; ++f) {
                        float t = rF32(data + p);
                        track.times.push_back(t);
                        max_time = std::max(max_time, t);
                        track.translations.push_back(rVec3(data + p + 4));
                        track.rotations.push_back(rVec4(data + p + 16));
                        std::array<float, 9> m{};
                        for (int k = 0; k < 9; ++k) m[k] = rF32(data + p + 32 + k * 4);
                        track.scale_shears.push_back(m);
                        p += FRAME;
                    }
                }
                anim.tracks.push_back(std::move(track));
            }
            anim.duration = max_time;
        }
        animations.push_back(std::move(anim));
    }
    return animations;
}

std::optional<GrnModel> parse_grn_memory(const uint8_t* data, size_t size) {
    if (!data || size < 0x60) return std::nullopt;

    GrnModel model;
    model.raw_file_bytes.assign(data, data + size);

    // Verify header signature at 0x40
    uint32_t magic = rU32(data + 0x40);
    uint32_t sec_count = rU32(data + 0x44);

    if (magic != T_FILE_DIRECTORY || sec_count == 0 || sec_count > 16) {
        return std::nullopt;
    }

    // Read section directory entries at 0x60
    struct SectionDesc {
        uint32_t tag = 0;
        uint32_t offset = 0;
        uint32_t end_offset = 0;
    };
    std::vector<SectionDesc> sections;
    for (uint32_t i = 0; i < sec_count; ++i) {
        size_t off = 0x60 + i * 20;
        if (off + 20 > size) break;
        SectionDesc s;
        s.tag = rU32(data + off);
        s.offset = rU32(data + off + 8);
        sections.push_back(s);
    }

    for (size_t i = 0; i < sections.size(); ++i) {
        sections[i].end_offset = (i + 1 < sections.size())
                                 ? sections[i + 1].offset
                                 : static_cast<uint32_t>(size);
    }

    // Locate Payload Section (0xca5e0103)
    const SectionDesc* payload_sec = nullptr;
    for (const auto& s : sections) {
        if (s.tag == T_SECTION_PAYLOAD) {
            payload_sec = &s;
            break;
        }
    }

    if (!payload_sec || payload_sec->offset >= size) {
        return std::nullopt;
    }

    const uint8_t* sec_data = data + payload_sec->offset;
    size_t sec_len = payload_sec->end_offset - payload_sec->offset;
    if (sec_len < 16) return std::nullopt;

    uint32_t chunk_count = rU32(sec_data);
    if (chunk_count == 0 || 16 + chunk_count * 12 > sec_len) {
        return std::nullopt;
    }

    std::vector<FlatChunk> flat_chunks;
    flat_chunks.reserve(chunk_count);
    std::vector<uint32_t> all_offsets;
    all_offsets.reserve(chunk_count);

    for (uint32_t c = 0; c < chunk_count; ++c) {
        size_t coff = 16 + c * 12;
        FlatChunk fc;
        fc.tag = rU32(sec_data + coff);
        fc.data_offset = rU32(sec_data + coff + 4);
        fc.data_size = rU32(sec_data + coff + 8);
        flat_chunks.push_back(fc);
        all_offsets.push_back(fc.data_offset);
    }
    std::sort(all_offsets.begin(), all_offsets.end());

    // Build hierarchy tree
    auto [roots, _] = build_chunk_tree(flat_chunks, 0, chunk_count);
    model.root_nodes = std::move(roots);

    // Decode String Table
    std::vector<std::string> strings;
    const auto* st_node = find_one(model.root_nodes, T_STRING_TABLE);
    if (st_node) {
        strings = decode_string_table(sec_data, sec_len, *st_node);
    }

    // Decode Data Extensions
    auto dexts = decode_data_extensions(sec_data, sec_len, model.root_nodes, strings);

    // Decode Core Entities
    uint32_t sec_end_u32 = static_cast<uint32_t>(sec_len);
    model.bones = decode_bones(sec_data, sec_len, model.root_nodes, dexts, all_offsets, sec_end_u32);
    model.meshes = decode_meshes(sec_data, sec_len, model.root_nodes, dexts, all_offsets, sec_end_u32);
    model.textures = decode_textures(sec_data, sec_len, model.root_nodes, dexts, all_offsets, sec_end_u32);
    model.materials = decode_materials(sec_data, sec_len, model.root_nodes, dexts);

    // Link Model and Form
    link_model_and_form(sec_data, sec_len, model.root_nodes, model.meshes, model.materials, all_offsets, sec_end_u32);

    // Decode Animations
    auto tc_list = decode_transform_channels(sec_data, sec_len, model.root_nodes);
    model.animations = decode_animations(sec_data, sec_len, model.root_nodes, dexts, tc_list, all_offsets, sec_end_u32);

    return model;
}

std::optional<GrnModel> parse_grn_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::nullopt;
    }

    return parse_grn_memory(buffer.data(), buffer.size());
}

} // namespace grn
