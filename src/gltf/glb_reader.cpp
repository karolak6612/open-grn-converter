/**
 * @file glb_reader.cpp
 * @brief Implementation of glTF 2.0 / GLB deserializer into GRN model representations using cgltf.
 */

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4100 4996)
#endif
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "glb_reader.h"
#include "../codecs/tga_png.h"
#include "../codecs/vtex_codec.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>
#include <string>

using json = nlohmann::json;

namespace grn {

static std::vector<uint8_t> base64_decode(const std::string& in) {
    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) T[static_cast<uint8_t>(b64_chars[i])] = i;

    int val = 0, valb = -8;
    for (uint8_t c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string get_extras_json(const cgltf_data* data, const cgltf_extras& extras) {
    if (extras.data && extras.data[0] != '\0') {
        return std::string(extras.data);
    }
    if (data && data->json && extras.end_offset > extras.start_offset) {
        return std::string(data->json + extras.start_offset, extras.end_offset - extras.start_offset);
    }
    return {};
}

std::optional<GrnModel> load_glb_memory(const uint8_t* data, size_t size, const GlbImportOptions& options) {
    if (!data || size < 20) return std::nullopt;

    cgltf_options c_opt{};
    cgltf_data* gltf = nullptr;
    cgltf_result res = cgltf_parse(&c_opt, data, size, &gltf);
    if (res != cgltf_result_success || !gltf) {
        return std::nullopt;
    }

    std::string tex_dir_str = options.texture_dir.string();
    const char* gltf_path = tex_dir_str.empty() ? nullptr : tex_dir_str.c_str();
    cgltf_load_buffers(&c_opt, gltf, gltf_path);

    GrnModel model;

    // Detect if this is a roundtrip GRN model (exported by open-grn)
    bool is_roundtrip = false;
    if (get_extras_json(gltf, gltf->extras).find("grn_") != std::string::npos) {
        is_roundtrip = true;
    }
    for (size_t i = 0; !is_roundtrip && i < gltf->images_count; ++i) {
        if (get_extras_json(gltf, gltf->images[i].extras).find("grn_") != std::string::npos) {
            is_roundtrip = true;
        }
    }
    for (size_t i = 0; !is_roundtrip && i < gltf->meshes_count; ++i) {
        if (get_extras_json(gltf, gltf->meshes[i].extras).find("grn_") != std::string::npos) {
            is_roundtrip = true;
        }
    }

    // 1. Identify materials used by scene primitives
    std::unordered_set<const cgltf_material*> used_materials;
    if (is_roundtrip) {
        for (size_t i = 0; i < gltf->materials_count; ++i) {
            used_materials.insert(&gltf->materials[i]);
        }
    } else {
        for (size_t ni = 0; ni < gltf->nodes_count; ++ni) {
            const auto* node = &gltf->nodes[ni];
            if (!node->mesh) continue;
            for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
                if (node->mesh->primitives[pi].material) {
                    used_materials.insert(node->mesh->primitives[pi].material);
                }
            }
        }
        for (size_t mi = 0; mi < gltf->meshes_count; ++mi) {
            for (size_t pi = 0; pi < gltf->meshes[mi].primitives_count; ++pi) {
                if (gltf->meshes[mi].primitives[pi].material) {
                    used_materials.insert(gltf->meshes[mi].primitives[pi].material);
                }
            }
        }
    }

    // Helper to decode a diffuse texture image on demand
    std::unordered_map<const cgltf_image*, GrnTexture> decoded_image_cache;
    auto get_or_decode_image = [&](const cgltf_image* img) -> const GrnTexture* {
        if (!img) return nullptr;
        auto it = decoded_image_cache.find(img);
        if (it != decoded_image_cache.end()) return &it->second;

        GrnTexture tex;
        tex.name = img->name ? img->name : ("texture_" + std::to_string(decoded_image_cache.size()));

        std::string img_extras = get_extras_json(gltf, img->extras);
        if (!img_extras.empty()) {
            try {
                json extras = json::parse(img_extras);
                tex.format_code = extras.value("grn_format_code", 0);
                if (extras.contains("grn_raw_blob")) {
                    tex.raw_blob = base64_decode(extras["grn_raw_blob"].get<std::string>());
                }
            } catch (...) {}
        }

        if (img->buffer_view) {
            const uint8_t* b_ptr = cgltf_buffer_view_data(img->buffer_view);
            size_t b_len = img->buffer_view->size;
            if (b_ptr && b_len > 0) {
                auto decoded = decode_image_memory(b_ptr, b_len);
                if (decoded) {
                    tex.width = decoded->width;
                    tex.height = decoded->height;
                    tex.has_alpha = decoded->has_alpha;
                    tex.decoded_rgba = std::move(decoded->pixels);
                }
            }
        } else if (img->uri) {
            std::string uri = img->uri;
            if (uri.rfind("data:", 0) == 0) {
                void* dec_ptr = nullptr;
                cgltf_size dec_size = 0;
                if (cgltf_load_buffer_base64(&c_opt, uri.length(), uri.c_str(), &dec_ptr) == cgltf_result_success && dec_ptr) {
                    auto decoded = decode_image_memory(reinterpret_cast<const uint8_t*>(dec_ptr), dec_size);
                    if (decoded) {
                        tex.width = decoded->width;
                        tex.height = decoded->height;
                        tex.has_alpha = decoded->has_alpha;
                        tex.decoded_rgba = std::move(decoded->pixels);
                    }
                    free(dec_ptr);
                }
            } else if (!options.texture_dir.empty()) {
                tex.file_name = uri;
                auto loose_path = options.texture_dir / uri;
                auto loaded = load_image_file(loose_path);
                if (loaded) {
                    tex.width = loaded->width;
                    tex.height = loaded->height;
                    tex.has_alpha = loaded->has_alpha;
                    tex.decoded_rgba = std::move(loaded->pixels);
                }
            }
        }

        if (tex.raw_blob.empty() && !tex.decoded_rgba.empty()) {
            tex.format_code = tex.has_alpha ? 1 : 0;
        }

        auto [ins_it, _] = decoded_image_cache.emplace(img, std::move(tex));
        return &ins_it->second;
    };

    // 2. Materials and Textures
    // GRN natively supports only diffuse textures per material.
    // We strictly load only diffuse textures referenced by used materials,
    // avoiding unsupported PBR channels (normal, roughness, metallic, AO) and unreferenced textures.
    std::unordered_map<const cgltf_material*, int32_t> mat_to_idx;
    std::map<std::pair<const cgltf_image*, uint32_t>, int32_t> texture_cache;

    for (size_t i = 0; i < gltf->materials_count; ++i) {
        const auto& mat_src = gltf->materials[i];
        if (!used_materials.empty() && !used_materials.count(&mat_src)) {
            continue;
        }

        GrnMaterial mat;
        mat.name = mat_src.name ? mat_src.name : ("material_" + std::to_string(model.materials.size()));

        const cgltf_image* img_ptr = nullptr;
        if (mat_src.has_pbr_metallic_roughness && mat_src.pbr_metallic_roughness.base_color_texture.texture) {
            img_ptr = mat_src.pbr_metallic_roughness.base_color_texture.texture->image;
        }

        if (img_ptr) {
            const float* f = mat_src.has_pbr_metallic_roughness ? mat_src.pbr_metallic_roughness.base_color_factor : nullptr;
            bool has_factor = (f && (f[0] < 0.99f || f[1] < 0.99f || f[2] < 0.99f));

            uint32_t color_key = 0xFFFFFFFF;
            if (has_factor) {
                uint8_t cr = static_cast<uint8_t>(std::clamp(f[0] * 255.0f, 0.0f, 255.0f));
                uint8_t cg = static_cast<uint8_t>(std::clamp(f[1] * 255.0f, 0.0f, 255.0f));
                uint8_t cb = static_cast<uint8_t>(std::clamp(f[2] * 255.0f, 0.0f, 255.0f));
                uint8_t ca = static_cast<uint8_t>(std::clamp(f[3] * 255.0f, 0.0f, 255.0f));
                color_key = (cr << 24) | (cg << 16) | (cb << 8) | ca;
            }

            auto tex_key = std::make_pair(img_ptr, color_key);
            auto tit = texture_cache.find(tex_key);
            if (tit != texture_cache.end()) {
                mat.diffuse_texture_index = tit->second;
            } else {
                const GrnTexture* base_tex = get_or_decode_image(img_ptr);
                if (base_tex) {
                    GrnTexture new_tex = *base_tex;
                    if (has_factor && !new_tex.decoded_rgba.empty()) {
                        new_tex.name = mat.name + "_diffuse";
                        for (size_t p = 0; p < new_tex.width * new_tex.height; ++p) {
                            new_tex.decoded_rgba[p * 4 + 0] = static_cast<uint8_t>(std::clamp(new_tex.decoded_rgba[p * 4 + 0] * f[0], 0.0f, 255.0f));
                            new_tex.decoded_rgba[p * 4 + 1] = static_cast<uint8_t>(std::clamp(new_tex.decoded_rgba[p * 4 + 1] * f[1], 0.0f, 255.0f));
                            new_tex.decoded_rgba[p * 4 + 2] = static_cast<uint8_t>(std::clamp(new_tex.decoded_rgba[p * 4 + 2] * f[2], 0.0f, 255.0f));
                            new_tex.decoded_rgba[p * 4 + 3] = static_cast<uint8_t>(std::clamp(new_tex.decoded_rgba[p * 4 + 3] * f[3], 0.0f, 255.0f));
                        }
                        new_tex.raw_blob.clear();
                        new_tex.format_code = new_tex.has_alpha ? 1 : 0;
                    }
                    int32_t new_idx = static_cast<int32_t>(model.textures.size());
                    model.textures.push_back(std::move(new_tex));
                    texture_cache[tex_key] = new_idx;
                    mat.diffuse_texture_index = new_idx;
                } else {
                    mat.diffuse_texture_index = -1;
                }
            }
        } else {
            mat.diffuse_texture_index = -1;
        }

        mat_to_idx[&mat_src] = static_cast<int32_t>(model.materials.size());
        model.materials.push_back(std::move(mat));
    }

    // 3. Bones / Skeleton
    std::unordered_map<const cgltf_node*, int32_t> node_to_joint;

    if (gltf->skins_count > 0) {
        const auto& skin = gltf->skins[0];
        for (size_t j = 0; j < skin.joints_count; ++j) {
            node_to_joint[skin.joints[j]] = static_cast<int32_t>(j);
        }

        for (size_t j = 0; j < skin.joints_count; ++j) {
            const auto* jnode = skin.joints[j];
            GrnBone bone;
            bone.name = jnode->name ? jnode->name : ("Bone_" + std::to_string(j));

            if (jnode->parent && node_to_joint.count(jnode->parent)) {
                bone.parent_index = node_to_joint[jnode->parent];
            } else {
                bone.parent_index = -1;
            }

            if (jnode->has_translation) {
                float tx = jnode->translation[0] * options.scale;
                float ty = jnode->translation[1] * options.scale;
                float tz = jnode->translation[2] * options.scale;
                bone.position.x = tx;
                bone.position.y = options.y_up ? -tz : ty;
                bone.position.z = options.y_up ? ty : tz;
            }
            if (jnode->has_rotation) {
                if (options.y_up) {
                    bone.rotation.x = jnode->rotation[0];
                    bone.rotation.y = -jnode->rotation[2];
                    bone.rotation.z = jnode->rotation[1];
                    bone.rotation.w = jnode->rotation[3];
                } else {
                    bone.rotation.x = jnode->rotation[0];
                    bone.rotation.y = jnode->rotation[1];
                    bone.rotation.z = jnode->rotation[2];
                    bone.rotation.w = jnode->rotation[3];
                }
            }
            if (jnode->has_scale) {
                float sx = jnode->scale[0];
                float sy = jnode->scale[1];
                float sz = jnode->scale[2];
                bool is_root = (bone.parent_index < 0);
                if (options.y_up && is_root) {
                    float n_sy = sz;
                    float n_sz = sy;
                    sy = n_sy;
                    sz = n_sz;
                }
                bone.scale_3x3[0] = sx;
                bone.scale_3x3[4] = sy;
                bone.scale_3x3[8] = sz;
            }

            model.bones.push_back(std::move(bone));
        }
    } else {
        GrnBone root;
        root.name = "__Root";
        root.parent_index = -1;
        root.position = {0.0f, 0.0f, 0.0f};
        root.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        model.bones.push_back(std::move(root));
    }

    // 4. Meshes & Primitives
    // Collect all mesh instances in node hierarchy
    struct MeshInstance {
        const cgltf_mesh* mesh = nullptr;
        const cgltf_node* node = nullptr;
        float world_matrix[16]{};
        bool has_skin = false;
    };

    std::vector<MeshInstance> instances;
    for (size_t ni = 0; ni < gltf->nodes_count; ++ni) {
        const auto* node = &gltf->nodes[ni];
        if (node->mesh) {
            MeshInstance inst;
            inst.mesh = node->mesh;
            inst.node = node;
            cgltf_node_transform_world(node, inst.world_matrix);
            inst.has_skin = (node->skin != nullptr) || (!model.bones.empty() && !node_to_joint.empty());
            instances.push_back(inst);
        }
    }

    // If no nodes had meshes directly attached, fall back to gltf->meshes
    if (instances.empty()) {
        for (size_t mi = 0; mi < gltf->meshes_count; ++mi) {
            MeshInstance inst;
            inst.mesh = &gltf->meshes[mi];
            inst.node = nullptr;
            inst.has_skin = !model.bones.empty() && !node_to_joint.empty();
            inst.world_matrix[0] = inst.world_matrix[5] = inst.world_matrix[10] = inst.world_matrix[15] = 1.0f;
            instances.push_back(inst);
        }
    }

    for (const auto& inst : instances) {
        const auto* mesh_src = inst.mesh;
        GrnMesh mesh;
        mesh.name = mesh_src->name ? mesh_src->name : ("Mesh_" + std::to_string(model.meshes.size()));

        for (size_t pi = 0; pi < mesh_src->primitives_count; ++pi) {
            const auto& prim = mesh_src->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            uint32_t base_vn = static_cast<uint32_t>(mesh.normals.size());

            GrnTriGroup group;
            if (prim.material && mat_to_idx.count(prim.material)) {
                group.material_index = mat_to_idx[prim.material];
                group.material_name = model.materials[group.material_index].name;
                if (mesh.material_index < 0) {
                    mesh.material_index = group.material_index;
                    mesh.material_name = group.material_name;
                }
            }

            const cgltf_accessor* pos_acc = nullptr;
            const cgltf_accessor* norm_acc = nullptr;
            const cgltf_accessor* uv_acc = nullptr;
            const cgltf_accessor* joint_acc = nullptr;
            const cgltf_accessor* weight_acc = nullptr;

            for (size_t ai = 0; ai < prim.attributes_count; ++ai) {
                const auto& attr = prim.attributes[ai];
                if (attr.type == cgltf_attribute_type_position) pos_acc = attr.data;
                else if (attr.type == cgltf_attribute_type_normal) norm_acc = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uv_acc = attr.data;
                else if (attr.type == cgltf_attribute_type_joints && attr.index == 0) joint_acc = attr.data;
                else if (attr.type == cgltf_attribute_type_weights && attr.index == 0) weight_acc = attr.data;
            }

            if (!pos_acc) continue;

            size_t vert_count = pos_acc->count;
            std::vector<float> raw_pos(vert_count * 3);
            cgltf_accessor_unpack_floats(pos_acc, raw_pos.data(), vert_count * 3);

            std::vector<Vec3> prim_positions(vert_count);
            for (size_t v = 0; v < vert_count; ++v) {
                float px = raw_pos[v * 3 + 0];
                float py = raw_pos[v * 3 + 1];
                float pz = raw_pos[v * 3 + 2];

                if (!inst.has_skin) {
                    const float* m = inst.world_matrix;
                    float wx = (m[0] * px + m[4] * py + m[8] * pz + m[12]) * options.scale;
                    float wy = (m[1] * px + m[5] * py + m[9] * pz + m[13]) * options.scale;
                    float wz = (m[2] * px + m[6] * py + m[10] * pz + m[14]) * options.scale;
                    prim_positions[v].x = wx;
                    prim_positions[v].y = options.y_up ? -wz : wy;
                    prim_positions[v].z = options.y_up ? wy : wz;
                } else {
                    prim_positions[v].x = px * options.scale;
                    prim_positions[v].y = options.y_up ? (-pz * options.scale) : (py * options.scale);
                    prim_positions[v].z = options.y_up ? (py * options.scale) : (pz * options.scale);
                }
            }

            // Normals
            if (norm_acc) {
                std::vector<float> raw_norm(vert_count * 3);
                cgltf_accessor_unpack_floats(norm_acc, raw_norm.data(), vert_count * 3);
                for (size_t v = 0; v < vert_count; ++v) {
                    float nx = raw_norm[v * 3 + 0];
                    float ny = raw_norm[v * 3 + 1];
                    float nz = raw_norm[v * 3 + 2];

                    if (!inst.has_skin) {
                        const float* m = inst.world_matrix;
                        float wx = m[0] * nx + m[4] * ny + m[8] * nz;
                        float wy = m[1] * nx + m[5] * ny + m[9] * nz;
                        float wz = m[2] * nx + m[6] * ny + m[10] * nz;
                        float len = std::sqrt(wx * wx + wy * wy + wz * wz);
                        if (len > 1e-6f) { wx /= len; wy /= len; wz /= len; }
                        mesh.normals.push_back({wx, options.y_up ? -wz : wy, options.y_up ? wy : wz});
                    } else {
                        mesh.normals.push_back({nx, options.y_up ? -nz : ny, options.y_up ? ny : nz});
                    }
                }
            } else {
                for (size_t v = 0; v < vert_count; ++v) {
                    mesh.normals.push_back({0.0f, 0.0f, 1.0f});
                }
            }

            // UVs
            if (uv_acc) {
                std::vector<float> raw_uv(vert_count * 2);
                cgltf_accessor_unpack_floats(uv_acc, raw_uv.data(), vert_count * 2);
                bool has_tr = (prim.material && prim.material->has_pbr_metallic_roughness &&
                               prim.material->pbr_metallic_roughness.base_color_texture.has_transform);
                const auto* tr = has_tr ? &prim.material->pbr_metallic_roughness.base_color_texture.transform : nullptr;
                float cos_r = tr ? std::cos(tr->rotation) : 1.0f;
                float sin_r = tr ? std::sin(tr->rotation) : 0.0f;
                float sx = tr ? tr->scale[0] : 1.0f;
                float sy = tr ? tr->scale[1] : 1.0f;
                float ox = tr ? tr->offset[0] : 0.0f;
                float oy = tr ? tr->offset[1] : 0.0f;

                for (size_t v = 0; v < vert_count; ++v) {
                    float u = raw_uv[v * 2 + 0];
                    float v_coord = raw_uv[v * 2 + 1];
                    if (has_tr) {
                        float su = u * sx;
                        float sv = v_coord * sy;
                        float tu = su * cos_r - sv * sin_r + ox;
                        float tv = su * sin_r + sv * cos_r + oy;
                        u = tu;
                        v_coord = tv;
                    }
                    mesh.uvs.push_back({u, v_coord});
                }
            }

            // Weights & Joints
            std::vector<VertexWeight> prim_weights;
            if (joint_acc && weight_acc) {
                std::vector<float> raw_weights(vert_count * 4);
                cgltf_accessor_unpack_floats(weight_acc, raw_weights.data(), vert_count * 4);

                for (size_t v = 0; v < vert_count; ++v) {
                    cgltf_uint j_ids[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(joint_acc, v, j_ids, 4);

                    VertexWeight vw;
                    for (int k = 0; k < 4; ++k) {
                        float w = raw_weights[v * 4 + k];
                        if (w > 1e-4f) {
                            int32_t jid = static_cast<int32_t>(j_ids[k]);
                            int32_t skel_b = jid;
                            if (inst.node && inst.node->skin && jid >= 0 && static_cast<size_t>(jid) < inst.node->skin->joints_count) {
                                const auto* jn = inst.node->skin->joints[jid];
                                if (node_to_joint.count(jn)) {
                                    skel_b = node_to_joint[jn];
                                }
                            }
                            vw.bone_indices.push_back(skel_b);
                            vw.bone_weights.push_back(w);
                        }
                    }
                    if (vw.bone_indices.empty()) {
                        vw.bone_indices.push_back(0);
                        vw.bone_weights.push_back(1.0f);
                    }
                    prim_weights.push_back(std::move(vw));
                }
            }

            uint32_t base_v = static_cast<uint32_t>(mesh.vertices.size());
            for (size_t v = 0; v < prim_positions.size(); ++v) {
                mesh.vertices.push_back(prim_positions[v]);
                if (v < prim_weights.size()) {
                    mesh.weights.push_back(prim_weights[v]);
                }
            }

            // Indices
            if (prim.indices) {
                size_t idx_count = prim.indices->count;
                std::vector<uint32_t> raw_indices(idx_count);
                cgltf_accessor_unpack_indices(prim.indices, raw_indices.data(), sizeof(uint32_t), idx_count);

                for (size_t t = 0; t + 2 < raw_indices.size(); t += 3) {
                    uint32_t pi0 = raw_indices[t + 0];
                    uint32_t pi1 = raw_indices[t + 1];
                    uint32_t pi2 = raw_indices[t + 2];

                    if (pi0 == pi1 || pi1 == pi2 || pi0 == pi2) continue;

                    uint32_t v0 = base_v + pi0;
                    uint32_t v1 = base_v + pi1;
                    uint32_t v2 = base_v + pi2;
                    uint32_t n0 = base_vn + pi0;
                    uint32_t n1 = base_vn + pi1;
                    uint32_t n2 = base_vn + pi2;
                    uint32_t u0 = base_vn + pi0;
                    uint32_t u1 = base_vn + pi1;
                    uint32_t u2 = base_vn + pi2;

                    mesh.faces.push_back({v0, v1, v2});
                    mesh.face_uvs.push_back({u0, u1, u2});
                    mesh.face_normals.push_back({n0, n1, n2});

                    group.faces.push_back({v0, v1, v2});
                    group.face_uvs.push_back({u0, u1, u2});
                    group.face_normals.push_back({n0, n1, n2});
                }
            } else {
                for (size_t v = 0; v + 2 < prim_positions.size(); v += 3) {
                    uint32_t v0 = base_v + static_cast<uint32_t>(v);
                    uint32_t v1 = base_v + static_cast<uint32_t>(v + 1);
                    uint32_t v2 = base_v + static_cast<uint32_t>(v + 2);
                    uint32_t n0 = base_vn + static_cast<uint32_t>(v);
                    uint32_t n1 = base_vn + static_cast<uint32_t>(v + 1);
                    uint32_t n2 = base_vn + static_cast<uint32_t>(v + 2);
                    uint32_t u0 = base_vn + static_cast<uint32_t>(v);
                    uint32_t u1 = base_vn + static_cast<uint32_t>(v + 1);
                    uint32_t u2 = base_vn + static_cast<uint32_t>(v + 2);

                    mesh.faces.push_back({v0, v1, v2});
                    mesh.face_uvs.push_back({u0, u1, u2});
                    mesh.face_normals.push_back({n0, n1, n2});

                    group.faces.push_back({v0, v1, v2});
                    group.face_uvs.push_back({u0, u1, u2});
                    group.face_normals.push_back({n0, n1, n2});
                }
            }
            mesh.tri_groups.push_back(std::move(group));
        }

        // Reconstruct bone palette and remap weights to local indices
        if (!mesh.weights.empty()) {
            std::string mesh_extras = get_extras_json(gltf, mesh_src->extras);
            if (!mesh_extras.empty()) {
                try {
                    json extras = json::parse(mesh_extras);
                    if (extras.contains("grn_bone_index_map")) {
                        mesh.bone_index_map = extras["grn_bone_index_map"].get<std::vector<int32_t>>();
                    }
                } catch (...) {}
            }

            if (mesh.bone_index_map.empty()) {
                std::vector<int32_t> used_b;
                for (const auto& vw : mesh.weights) {
                    for (int32_t b : vw.bone_indices) {
                        if (std::find(used_b.begin(), used_b.end(), b) == used_b.end()) {
                            used_b.push_back(b);
                        }
                    }
                }
                std::sort(used_b.begin(), used_b.end());
                mesh.bone_index_map = std::move(used_b);
            }

            if (mesh.bone_index_map.empty() && !model.bones.empty()) {
                mesh.bone_index_map.push_back(0);
            }

            std::unordered_map<int32_t, int32_t> skel_to_local;
            for (size_t li = 0; li < mesh.bone_index_map.size(); ++li) {
                skel_to_local[mesh.bone_index_map[li]] = static_cast<int32_t>(li);
            }
            for (auto& vw : mesh.weights) {
                for (auto& b : vw.bone_indices) {
                    auto it = skel_to_local.find(b);
                    b = (it != skel_to_local.end()) ? it->second : 0;
                }
            }
            mesh.bone_count = static_cast<uint32_t>(mesh.bone_index_map.size());
        }

        model.meshes.push_back(std::move(mesh));
    }

    // 5. Animations
    for (size_t ai = 0; ai < gltf->animations_count; ++ai) {
        const auto& anim_src = gltf->animations[ai];
        GrnAnimation anim;
        anim.name = anim_src.name ? anim_src.name : ("Animation_" + std::to_string(model.animations.size()));

        float max_dur = 0.0f;
        for (size_t si = 0; si < anim_src.samplers_count; ++si) {
            const auto& sampler = anim_src.samplers[si];
            if (sampler.input && sampler.input->has_max) {
                max_dur = std::max(max_dur, sampler.input->max[0]);
            }
        }
        anim.duration = max_dur;
        model.animations.push_back(std::move(anim));
    }

    std::string root_extras = get_extras_json(gltf, gltf->extras);
    if (model.animations.empty() && !root_extras.empty()) {
        try {
            json extras = json::parse(root_extras);
            if (extras.value("grn_has_animation", false)) {
                GrnAnimation anim;
                anim.name = extras.value("grn_anim_name", "Animation");
                anim.duration = 0.0f;
                model.animations.push_back(std::move(anim));
            }
        } catch (...) {}
    }

    if (options.target_height > 0.0f) {
        float min_z = 1e30f, max_z = -1e30f;
        for (const auto& m : model.meshes) {
            for (const auto& v : m.vertices) {
                min_z = std::min(min_z, v.z);
                max_z = std::max(max_z, v.z);
            }
        }
        float cur_h = max_z - min_z;
        if (cur_h > 1e-4f) {
            float auto_s = options.target_height / cur_h;
            for (auto& m : model.meshes) {
                for (auto& v : m.vertices) {
                    v.x *= auto_s;
                    v.y *= auto_s;
                    v.z *= auto_s;
                }
            }
            for (auto& b : model.bones) {
                b.position.x *= auto_s;
                b.position.y *= auto_s;
                b.position.z *= auto_s;
            }
        }
    }

    cgltf_free(gltf);
    return model;
}

std::optional<GrnModel> load_glb_file(const std::filesystem::path& path, const GlbImportOptions& options) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::nullopt;
    }

    GlbImportOptions opt = options;
    if (opt.texture_dir.empty()) {
        opt.texture_dir = path.parent_path();
    }

    return load_glb_memory(buffer.data(), buffer.size(), opt);
}

} // namespace grn
