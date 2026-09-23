#include "mesh_optimizer.h"
#include <meshoptimizer.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace grn {

std::vector<GrnMesh> split_mesh_16bit(const GrnMesh& mesh, uint32_t max_vertices) {
    if (mesh.vertices.size() <= max_vertices) {
        return { mesh };
    }

    // Prepare groups of triangles to partition, preserving material bindings
    std::vector<GrnTriGroup> src_groups = mesh.tri_groups;
    if (src_groups.empty()) {
        GrnTriGroup g;
        g.material_index = mesh.material_index;
        g.material_name = mesh.material_name;
        g.faces = mesh.faces;
        g.face_normals = mesh.face_normals;
        g.face_uvs = mesh.face_uvs;
        src_groups.push_back(std::move(g));
    }

    std::vector<GrnMesh> result;
    size_t part_idx = 0;

    GrnMesh current_part;
    current_part.name = mesh.name + "_part" + std::to_string(part_idx);
    current_part.material_index = mesh.material_index;
    current_part.material_name = mesh.material_name;
    current_part.bone_index_map = mesh.bone_index_map;
    current_part.bone_count = mesh.bone_count;

    std::unordered_map<uint32_t, uint32_t> old_v_to_new_v;
    std::unordered_map<uint32_t, uint32_t> old_n_to_new_n;
    std::unordered_map<uint32_t, uint32_t> old_u_to_new_u;

    auto map_v = [&](uint32_t v) -> uint32_t {
        auto it = old_v_to_new_v.find(v);
        if (it != old_v_to_new_v.end()) return it->second;
        uint32_t nv = static_cast<uint32_t>(current_part.vertices.size());
        old_v_to_new_v[v] = nv;
        current_part.vertices.push_back(v < mesh.vertices.size() ? mesh.vertices[v] : Vec3{});
        if (v < mesh.weights.size()) {
            current_part.weights.push_back(mesh.weights[v]);
        }
        return nv;
    };

    auto map_n = [&](uint32_t n) -> uint32_t {
        if (mesh.normals.empty()) return 0;
        auto it = old_n_to_new_n.find(n);
        if (it != old_n_to_new_n.end()) return it->second;
        uint32_t nn = static_cast<uint32_t>(current_part.normals.size());
        old_n_to_new_n[n] = nn;
        current_part.normals.push_back(n < mesh.normals.size() ? mesh.normals[n] : Vec3{ 0.0f, 0.0f, 1.0f });
        return nn;
    };

    auto map_u = [&](uint32_t u) -> uint32_t {
        if (mesh.uvs.empty()) return 0;
        auto it = old_u_to_new_u.find(u);
        if (it != old_u_to_new_u.end()) return it->second;
        uint32_t nu = static_cast<uint32_t>(current_part.uvs.size());
        old_u_to_new_u[u] = nu;
        current_part.uvs.push_back(u < mesh.uvs.size() ? mesh.uvs[u] : Vec2{ 0.0f, 0.0f });
        return nu;
    };

    auto finalize_current_part = [&]() {
        if (!current_part.faces.empty()) {
            if (!current_part.tri_groups.empty()) {
                current_part.material_index = current_part.tri_groups[0].material_index;
                current_part.material_name = current_part.tri_groups[0].material_name;
            }

            // Compact bone palette to only bones actually referenced by this submesh's vertex weights.
            // This prevents massive rig models (e.g. centi with 1,905 bones) from overflowing
            // the DirectX 8/9 skinning bone palette limits in Sacred Gold!
            if (!current_part.weights.empty() && !mesh.bone_index_map.empty()) {
                std::vector<int32_t> used_local_bones;
                for (const auto& vw : current_part.weights) {
                    for (size_t wi = 0; wi < vw.bone_indices.size(); ++wi) {
                        if (wi < vw.bone_weights.size() && vw.bone_weights[wi] > 1e-4f) {
                            int32_t b = vw.bone_indices[wi];
                            if (std::find(used_local_bones.begin(), used_local_bones.end(), b) == used_local_bones.end()) {
                                used_local_bones.push_back(b);
                            }
                        }
                    }
                }
                std::sort(used_local_bones.begin(), used_local_bones.end());
                if (used_local_bones.empty()) used_local_bones.push_back(0);

                std::unordered_map<int32_t, int32_t> old_to_new_bone;
                std::vector<int32_t> compact_palette;
                compact_palette.reserve(used_local_bones.size());
                for (size_t bi = 0; bi < used_local_bones.size(); ++bi) {
                    int32_t old_b = used_local_bones[bi];
                    old_to_new_bone[old_b] = static_cast<int32_t>(bi);
                    int32_t global_bone = (old_b >= 0 && static_cast<size_t>(old_b) < mesh.bone_index_map.size())
                                          ? mesh.bone_index_map[old_b] : old_b;
                    compact_palette.push_back(global_bone);
                }

                for (auto& vw : current_part.weights) {
                    for (auto& b : vw.bone_indices) {
                        auto it = old_to_new_bone.find(b);
                        b = (it != old_to_new_bone.end()) ? it->second : 0;
                    }
                }
                current_part.bone_index_map = std::move(compact_palette);
                current_part.bone_count = static_cast<uint32_t>(current_part.bone_index_map.size());
            } else {
                current_part.bone_index_map = mesh.bone_index_map;
                current_part.bone_count = mesh.bone_count;
            }

            result.push_back(std::move(current_part));
            part_idx++;
        }
        current_part = GrnMesh{};
        current_part.name = mesh.name + "_part" + std::to_string(part_idx);
        current_part.material_index = mesh.material_index;
        current_part.material_name = mesh.material_name;
        old_v_to_new_v.clear();
        old_n_to_new_n.clear();
        old_u_to_new_u.clear();
    };

    for (const auto& group : src_groups) {
        if (group.faces.empty()) continue;

        // Collect unique vertex indices used by this entire group
        std::unordered_set<uint32_t> group_verts;
        for (const auto& f : group.faces) {
            group_verts.insert(f[0]);
            group_verts.insert(f[1]);
            group_verts.insert(f[2]);
        }

        // Count how many new vertices this group would add to current_part
        size_t new_verts_needed = 0;
        for (uint32_t v : group_verts) {
            if (!old_v_to_new_v.count(v)) ++new_verts_needed;
        }

        // Check if we can keep this whole group together
        if (old_v_to_new_v.size() + new_verts_needed <= max_vertices) {
            // Fits in current_part!
            GrnTriGroup active_group;
            active_group.material_index = group.material_index;
            active_group.material_name = group.material_name;

            for (size_t fi = 0; fi < group.faces.size(); ++fi) {
                const auto& f = group.faces[fi];
                const auto& fn = (fi < group.face_normals.size()) ? group.face_normals[fi] : f;
                const auto& fu = (fi < group.face_uvs.size()) ? group.face_uvs[fi] : f;

                uint32_t nv0 = map_v(f[0]);
                uint32_t nv1 = map_v(f[1]);
                uint32_t nv2 = map_v(f[2]);
                uint32_t nn0 = map_n(fn[0]);
                uint32_t nn1 = map_n(fn[1]);
                uint32_t nn2 = map_n(fn[2]);
                uint32_t nu0 = map_u(fu[0]);
                uint32_t nu1 = map_u(fu[1]);
                uint32_t nu2 = map_u(fu[2]);

                active_group.faces.push_back({ nv0, nv1, nv2 });
                active_group.face_normals.push_back({ nn0, nn1, nn2 });
                active_group.face_uvs.push_back({ nu0, nu1, nu2 });

                current_part.faces.push_back({ nv0, nv1, nv2 });
                current_part.face_normals.push_back({ nn0, nn1, nn2 });
                current_part.face_uvs.push_back({ nu0, nu1, nu2 });
            }

            current_part.tri_groups.push_back(std::move(active_group));
        } else if (!current_part.faces.empty() && group_verts.size() <= max_vertices) {
            // Doesn't fit in current_part, but fits completely in a fresh part!
            finalize_current_part();

            GrnTriGroup active_group;
            active_group.material_index = group.material_index;
            active_group.material_name = group.material_name;

            for (size_t fi = 0; fi < group.faces.size(); ++fi) {
                const auto& f = group.faces[fi];
                const auto& fn = (fi < group.face_normals.size()) ? group.face_normals[fi] : f;
                const auto& fu = (fi < group.face_uvs.size()) ? group.face_uvs[fi] : f;

                uint32_t nv0 = map_v(f[0]);
                uint32_t nv1 = map_v(f[1]);
                uint32_t nv2 = map_v(f[2]);
                uint32_t nn0 = map_n(fn[0]);
                uint32_t nn1 = map_n(fn[1]);
                uint32_t nn2 = map_n(fn[2]);
                uint32_t nu0 = map_u(fu[0]);
                uint32_t nu1 = map_u(fu[1]);
                uint32_t nu2 = map_u(fu[2]);

                active_group.faces.push_back({ nv0, nv1, nv2 });
                active_group.face_normals.push_back({ nn0, nn1, nn2 });
                active_group.face_uvs.push_back({ nu0, nu1, nu2 });

                current_part.faces.push_back({ nv0, nv1, nv2 });
                current_part.face_normals.push_back({ nn0, nn1, nn2 });
                current_part.face_uvs.push_back({ nu0, nu1, nu2 });
            }

            current_part.tri_groups.push_back(std::move(active_group));
        } else {
            // Group alone exceeds max_vertices, or current_part is already empty and group > max_vertices.
            // Split triangle-by-triangle while preserving material group slices.
            GrnTriGroup active_group;
            active_group.material_index = group.material_index;
            active_group.material_name = group.material_name;

            for (size_t fi = 0; fi < group.faces.size(); ++fi) {
                const auto& f = group.faces[fi];
                const auto& fn = (fi < group.face_normals.size()) ? group.face_normals[fi] : f;
                const auto& fu = (fi < group.face_uvs.size()) ? group.face_uvs[fi] : f;

                uint32_t tri_new_verts = (!old_v_to_new_v.count(f[0])) +
                                         (!old_v_to_new_v.count(f[1])) +
                                         (!old_v_to_new_v.count(f[2]));

                if (old_v_to_new_v.size() + tri_new_verts > max_vertices && !current_part.faces.empty()) {
                    if (!active_group.faces.empty()) {
                        current_part.tri_groups.push_back(std::move(active_group));
                        active_group = GrnTriGroup{};
                        active_group.material_index = group.material_index;
                        active_group.material_name = group.material_name;
                    }
                    finalize_current_part();
                    active_group.material_index = group.material_index;
                    active_group.material_name = group.material_name;
                }

                uint32_t nv0 = map_v(f[0]);
                uint32_t nv1 = map_v(f[1]);
                uint32_t nv2 = map_v(f[2]);
                uint32_t nn0 = map_n(fn[0]);
                uint32_t nn1 = map_n(fn[1]);
                uint32_t nn2 = map_n(fn[2]);
                uint32_t nu0 = map_u(fu[0]);
                uint32_t nu1 = map_u(fu[1]);
                uint32_t nu2 = map_u(fu[2]);

                active_group.faces.push_back({ nv0, nv1, nv2 });
                active_group.face_normals.push_back({ nn0, nn1, nn2 });
                active_group.face_uvs.push_back({ nu0, nu1, nu2 });

                current_part.faces.push_back({ nv0, nv1, nv2 });
                current_part.face_normals.push_back({ nn0, nn1, nn2 });
                current_part.face_uvs.push_back({ nu0, nu1, nu2 });
            }

            if (!active_group.faces.empty()) {
                current_part.tri_groups.push_back(std::move(active_group));
            }
        }
    }

    finalize_current_part();
    return result;
}

bool decimate_mesh(GrnMesh& mesh, float target_ratio, uint32_t target_max_verts) {
    if (mesh.faces.empty() || mesh.vertices.empty()) return false;
    if (mesh.vertices.size() <= target_max_verts && target_ratio >= 0.999f) return false;

    // Determine target decimation ratio
    float effective_ratio = std::clamp(target_ratio, 0.05f, 1.0f);
    if (mesh.vertices.size() > target_max_verts) {
        float vert_ratio = static_cast<float>(target_max_verts) / static_cast<float>(mesh.vertices.size());
        effective_ratio = std::min(effective_ratio, vert_ratio);
    }
    if (effective_ratio >= 0.999f) return false;

    // Ensure tri_groups is populated
    std::vector<GrnTriGroup> groups = mesh.tri_groups;
    if (groups.empty()) {
        GrnTriGroup g;
        g.material_index = mesh.material_index;
        g.material_name = mesh.material_name;
        g.faces = mesh.faces;
        g.face_normals = mesh.face_normals;
        g.face_uvs = mesh.face_uvs;
        groups.push_back(std::move(g));
    }

    std::vector<GrnTriGroup> simplified_groups;
    bool any_simplified = false;

    for (auto& grp : groups) {
        if (grp.faces.size() < 16) {
            // Keep tiny primitives intact to prevent degenerate geometries
            simplified_groups.push_back(grp);
            continue;
        }

        size_t orig_idx_count = grp.faces.size() * 3;
        size_t target_idx_count = std::max(static_cast<size_t>(12), static_cast<size_t>(orig_idx_count * effective_ratio));
        if (target_idx_count >= orig_idx_count) {
            simplified_groups.push_back(grp);
            continue;
        }

        std::vector<uint32_t> indices(orig_idx_count);
        for (size_t f = 0; f < grp.faces.size(); ++f) {
            indices[f * 3 + 0] = grp.faces[f][0];
            indices[f * 3 + 1] = grp.faces[f][1];
            indices[f * 3 + 2] = grp.faces[f][2];
        }

        std::vector<uint32_t> dest(orig_idx_count);
        float target_error = 0.05f;
        size_t new_idx_count = meshopt_simplify(
            dest.data(),
            indices.data(),
            orig_idx_count,
            reinterpret_cast<const float*>(mesh.vertices.data()),
            mesh.vertices.size(),
            sizeof(Vec3),
            target_idx_count,
            target_error,
            0,
            nullptr
        );

        if (new_idx_count > 0 && new_idx_count < orig_idx_count) {
            dest.resize(new_idx_count);
            GrnTriGroup new_grp;
            new_grp.material_index = grp.material_index;
            new_grp.material_name = grp.material_name;

            for (size_t i = 0; i < dest.size(); i += 3) {
                uint32_t v0 = dest[i + 0];
                uint32_t v1 = dest[i + 1];
                uint32_t v2 = dest[i + 2];
                new_grp.faces.push_back({ v0, v1, v2 });

                uint32_t n0 = (v0 < mesh.normals.size()) ? v0 : 0;
                uint32_t n1 = (v1 < mesh.normals.size()) ? v1 : 0;
                uint32_t n2 = (v2 < mesh.normals.size()) ? v2 : 0;
                new_grp.face_normals.push_back({ n0, n1, n2 });

                uint32_t u0 = (v0 < mesh.uvs.size()) ? v0 : 0;
                uint32_t u1 = (v1 < mesh.uvs.size()) ? v1 : 0;
                uint32_t u2 = (v2 < mesh.uvs.size()) ? v2 : 0;
                new_grp.face_uvs.push_back({ u0, u1, u2 });
            }
            simplified_groups.push_back(std::move(new_grp));
            any_simplified = true;
        } else {
            simplified_groups.push_back(grp);
        }
    }

    if (!any_simplified) return false;

    // Compact vertices, normals, uvs, weights to only those referenced by simplified_groups
    std::unordered_map<uint32_t, uint32_t> old_v_to_new_v;
    std::unordered_map<uint32_t, uint32_t> old_n_to_new_n;
    std::unordered_map<uint32_t, uint32_t> old_u_to_new_u;

    std::vector<Vec3> new_verts;
    std::vector<Vec3> new_normals;
    std::vector<Vec2> new_uvs;
    std::vector<VertexWeight> new_weights;

    auto remap_v = [&](uint32_t v) -> uint32_t {
        auto it = old_v_to_new_v.find(v);
        if (it != old_v_to_new_v.end()) return it->second;
        uint32_t nv = static_cast<uint32_t>(new_verts.size());
        old_v_to_new_v[v] = nv;
        new_verts.push_back(v < mesh.vertices.size() ? mesh.vertices[v] : Vec3{ 0.0f, 0.0f, 0.0f });
        if (!mesh.weights.empty()) {
            new_weights.push_back(v < mesh.weights.size() ? mesh.weights[v] : VertexWeight{});
        }
        return nv;
    };

    auto remap_n = [&](uint32_t n) -> uint32_t {
        if (mesh.normals.empty()) return 0;
        auto it = old_n_to_new_n.find(n);
        if (it != old_n_to_new_n.end()) return it->second;
        uint32_t nn = static_cast<uint32_t>(new_normals.size());
        old_n_to_new_n[n] = nn;
        new_normals.push_back(n < mesh.normals.size() ? mesh.normals[n] : Vec3{ 0.0f, 1.0f, 0.0f });
        return nn;
    };

    auto remap_u = [&](uint32_t u) -> uint32_t {
        if (mesh.uvs.empty()) return 0;
        auto it = old_u_to_new_u.find(u);
        if (it != old_u_to_new_u.end()) return it->second;
        uint32_t nu = static_cast<uint32_t>(new_uvs.size());
        old_u_to_new_u[u] = nu;
        new_uvs.push_back(u < mesh.uvs.size() ? mesh.uvs[u] : Vec2{ 0.0f, 0.0f });
        return nu;
    };

    mesh.faces.clear();
    mesh.face_normals.clear();
    mesh.face_uvs.clear();
    for (auto& grp : simplified_groups) {
        for (size_t fi = 0; fi < grp.faces.size(); ++fi) {
            grp.faces[fi][0] = remap_v(grp.faces[fi][0]);
            grp.faces[fi][1] = remap_v(grp.faces[fi][1]);
            grp.faces[fi][2] = remap_v(grp.faces[fi][2]);

            if (fi < grp.face_normals.size()) {
                grp.face_normals[fi][0] = remap_n(grp.face_normals[fi][0]);
                grp.face_normals[fi][1] = remap_n(grp.face_normals[fi][1]);
                grp.face_normals[fi][2] = remap_n(grp.face_normals[fi][2]);
            }
            if (fi < grp.face_uvs.size()) {
                grp.face_uvs[fi][0] = remap_u(grp.face_uvs[fi][0]);
                grp.face_uvs[fi][1] = remap_u(grp.face_uvs[fi][1]);
                grp.face_uvs[fi][2] = remap_u(grp.face_uvs[fi][2]);
            }

            mesh.faces.push_back(grp.faces[fi]);
            if (fi < grp.face_normals.size()) mesh.face_normals.push_back(grp.face_normals[fi]);
            if (fi < grp.face_uvs.size()) mesh.face_uvs.push_back(grp.face_uvs[fi]);
        }
    }

    mesh.vertices = std::move(new_verts);
    if (!mesh.normals.empty()) mesh.normals = std::move(new_normals);
    if (!mesh.uvs.empty()) mesh.uvs = std::move(new_uvs);
    if (!mesh.weights.empty()) mesh.weights = std::move(new_weights);
    mesh.tri_groups = std::move(simplified_groups);

    return true;
}

void optimize_model_meshes(GrnModel& model, const MeshOptimizerOptions& options) {
    std::vector<GrnMesh> new_meshes;

    for (auto& mesh : model.meshes) {
        if (options.decimate) {
            decimate_mesh(mesh, options.target_decimate_ratio, options.max_total_vertices);
        }

        if (options.auto_split_16bit && mesh.vertices.size() > options.max_vertices_per_part) {
            auto parts = split_mesh_16bit(mesh, options.max_vertices_per_part);
            for (auto& p : parts) {
                new_meshes.push_back(std::move(p));
            }
        } else {
            new_meshes.push_back(std::move(mesh));
        }
    }

    model.meshes = std::move(new_meshes);
}

} // namespace grn
