#include "mesh_optimizer.h"
#include <meshoptimizer.h>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace grn {

std::vector<GrnMesh> split_mesh_16bit(const GrnMesh& mesh, uint32_t max_vertices) {
    if (mesh.vertices.size() <= max_vertices) {
        return { mesh };
    }

    std::vector<GrnMesh> result;
    size_t part_idx = 0;

    GrnMesh current_part;
    current_part.name = mesh.name + "_part" + std::to_string(part_idx);
    current_part.material_index = mesh.material_index;
    current_part.material_name = mesh.material_name;
    current_part.bone_index_map = mesh.bone_index_map;
    current_part.bone_count = mesh.bone_count;

    std::unordered_map<uint32_t, uint32_t> old_to_new;
    old_to_new.reserve(std::min<size_t>(max_vertices, mesh.vertices.size()));

    auto map_vertex = [&](uint32_t orig_idx) -> uint32_t {
        auto it = old_to_new.find(orig_idx);
        if (it != old_to_new.end()) return it->second;

        uint32_t new_idx = static_cast<uint32_t>(current_part.vertices.size());
        old_to_new[orig_idx] = new_idx;

        current_part.vertices.push_back(mesh.vertices[orig_idx]);
        if (orig_idx < mesh.normals.size()) {
            current_part.normals.push_back(mesh.normals[orig_idx]);
        }
        if (orig_idx < mesh.uvs.size()) {
            current_part.uvs.push_back(mesh.uvs[orig_idx]);
        }
        if (orig_idx < mesh.weights.size()) {
            current_part.weights.push_back(mesh.weights[orig_idx]);
        }
        return new_idx;
    };

    for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
        const auto& face = mesh.faces[fi];
        uint32_t v0 = face[0];
        uint32_t v1 = face[1];
        uint32_t v2 = face[2];

        uint32_t new_verts_needed = (!old_to_new.count(v0)) + (!old_to_new.count(v1)) + (!old_to_new.count(v2));

        if (old_to_new.size() + new_verts_needed > max_vertices && !current_part.faces.empty()) {
            result.push_back(std::move(current_part));
            old_to_new.clear();
            part_idx++;

            current_part = GrnMesh{};
            current_part.name = mesh.name + "_part" + std::to_string(part_idx);
            current_part.material_index = mesh.material_index;
            current_part.material_name = mesh.material_name;
            current_part.bone_index_map = mesh.bone_index_map;
            current_part.bone_count = mesh.bone_count;
        }

        uint32_t nv0 = map_vertex(v0);
        uint32_t nv1 = map_vertex(v1);
        uint32_t nv2 = map_vertex(v2);
        current_part.faces.push_back({ nv0, nv1, nv2 });
        current_part.face_normals.push_back({ nv0, nv1, nv2 });
        current_part.face_uvs.push_back({ nv0, nv1, nv2 });
    }

    if (!current_part.faces.empty()) {
        result.push_back(std::move(current_part));
    }

    return result;
}

bool decimate_mesh(GrnMesh& mesh, float target_ratio, uint32_t target_max_verts) {
    if (mesh.faces.empty() || mesh.vertices.empty()) return false;
    if (mesh.vertices.size() <= target_max_verts && target_ratio >= 0.999f) return false;

    size_t orig_index_count = mesh.faces.size() * 3;
    size_t target_index_count = static_cast<size_t>(orig_index_count * std::clamp(target_ratio, 0.05f, 1.0f));

    if (mesh.vertices.size() > target_max_verts) {
        float vert_ratio = static_cast<float>(target_max_verts) / static_cast<float>(mesh.vertices.size());
        size_t vert_target_indices = static_cast<size_t>(orig_index_count * vert_ratio);
        target_index_count = std::min(target_index_count, vert_target_indices);
    }

    if (target_index_count >= orig_index_count) return false;

    std::vector<uint32_t> indices(orig_index_count);
    for (size_t f = 0; f < mesh.faces.size(); ++f) {
        indices[f * 3 + 0] = mesh.faces[f][0];
        indices[f * 3 + 1] = mesh.faces[f][1];
        indices[f * 3 + 2] = mesh.faces[f][2];
    }

    std::vector<uint32_t> destination(orig_index_count);
    float target_error = 0.05f;
    size_t new_index_count = meshopt_simplify(
        destination.data(),
        indices.data(),
        orig_index_count,
        reinterpret_cast<const float*>(mesh.vertices.data()),
        mesh.vertices.size(),
        sizeof(Vec3),
        target_index_count,
        target_error,
        0,
        nullptr
    );

    if (new_index_count == 0 || new_index_count >= orig_index_count) {
        return false;
    }

    destination.resize(new_index_count);

    std::vector<uint32_t> remap(mesh.vertices.size(), 0xFFFFFFFF);
    std::vector<Vec3> new_verts;
    std::vector<Vec3> new_normals;
    std::vector<Vec2> new_uvs;
    std::vector<VertexWeight> new_weights;

    for (uint32_t idx : destination) {
        if (remap[idx] == 0xFFFFFFFF) {
            remap[idx] = static_cast<uint32_t>(new_verts.size());
            new_verts.push_back(mesh.vertices[idx]);
            if (idx < mesh.normals.size()) new_normals.push_back(mesh.normals[idx]);
            if (idx < mesh.uvs.size()) new_uvs.push_back(mesh.uvs[idx]);
            if (idx < mesh.weights.size()) new_weights.push_back(mesh.weights[idx]);
        }
    }

    mesh.faces.clear();
    mesh.face_normals.clear();
    mesh.face_uvs.clear();
    for (size_t i = 0; i < destination.size(); i += 3) {
        uint32_t v0 = remap[destination[i + 0]];
        uint32_t v1 = remap[destination[i + 1]];
        uint32_t v2 = remap[destination[i + 2]];
        mesh.faces.push_back({ v0, v1, v2 });
        mesh.face_normals.push_back({ v0, v1, v2 });
        mesh.face_uvs.push_back({ v0, v1, v2 });
    }

    mesh.vertices = std::move(new_verts);
    if (!new_normals.empty()) mesh.normals = std::move(new_normals);
    if (!new_uvs.empty()) mesh.uvs = std::move(new_uvs);
    if (!new_weights.empty()) mesh.weights = std::move(new_weights);

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
