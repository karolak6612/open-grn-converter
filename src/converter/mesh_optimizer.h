#pragma once

#include "../core/grn_types.h"
#include <vector>
#include <cstdint>
#include <string>

namespace grn {

/**
 * @brief Configuration parameters for mesh optimization and 16-bit safe partitioning.
 */
struct MeshOptimizerOptions {
    bool auto_split_16bit{ true };            ///< Automatically partition meshes with > max_vertices_per_part into 16-bit safe sub-meshes.
    uint32_t max_vertices_per_part{ 64000 };  ///< Maximum vertex threshold per sub-mesh (must be <= 65535, default 64000).
    bool decimate{ false };                   ///< Apply quadric error decimation.
    float target_decimate_ratio{ 0.7f };      ///< Target ratio of triangles to retain (0.1 to 1.0).
    uint32_t max_total_vertices{ 64000 };     ///< Optional maximum target vertex budget for decimation.
};

/**
 * @brief Automatically splits a high-poly mesh exceeding max_vertices into multiple 16-bit safe sub-meshes.
 *
 * Each generated sub-mesh has at most max_vertices vertices and remapped 16-bit-safe triangle indices.
 * Retains 100% of the original geometry, normals, UVs, and bone skinning weights without any visual loss.
 *
 * @param mesh The input mesh to inspect and partition.
 * @param max_vertices Maximum vertices permitted per sub-mesh (default 64,000, leaving headroom below 65,535).
 * @return A vector of one or more sub-meshes. If mesh.vertices.size() <= max_vertices, returns { mesh }.
 */
std::vector<GrnMesh> split_mesh_16bit(const GrnMesh& mesh, uint32_t max_vertices = 64000);

/**
 * @brief Simplifies a mesh using meshoptimizer quadric error metrics (QEM) to reduce polygon count.
 *
 * Preserves UV seams, normals, and interpolates bone skinning weights on collapsed vertices.
 *
 * @param mesh The mesh to simplify in-place.
 * @param target_ratio Target triangle fraction to keep (e.g. 0.5 for 50% triangles).
 * @param target_max_verts Maximum vertex limit (if vertices exceed this, decimation is applied).
 * @return true if simplification was performed, false if skipped or no reduction occurred.
 */
bool decimate_mesh(GrnMesh& mesh, float target_ratio = 0.7f, uint32_t target_max_verts = 64000);

/**
 * @brief Processes an entire GrnModel through the mesh optimizer pipeline.
 *
 * Decimates meshes if requested, then splits any mesh exceeding 16-bit vertex limits.
 */
void optimize_model_meshes(GrnModel& model, const MeshOptimizerOptions& options);

} // namespace grn
