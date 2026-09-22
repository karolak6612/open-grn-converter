/**
 * @file test_optimizer.cpp
 * @brief Automated unit test for mesh optimizer and 16-bit vertex partitioner.
 */

#include "../src/converter/mesh_optimizer.h"
#include "../src/core/grn_writer.h"
#include "../src/core/grn_parser.h"
#include "../src/gltf/glb_writer.h"

#include <iostream>
#include <cassert>
#include <vector>

static void test_split_97k_mesh() {
    std::cout << "[TEST] 16-bit Mesh Partitioner (97k vertices)..." << std::endl;

    grn::GrnMesh mesh;
    mesh.name = "HighPolyHero";

    // Build 97,000 vertices and triangles
    const uint32_t num_verts = 97000;
    mesh.vertices.resize(num_verts);
    mesh.normals.resize(num_verts);
    mesh.uvs.resize(num_verts);
    mesh.weights.resize(num_verts);

    for (uint32_t i = 0; i < num_verts; ++i) {
        mesh.vertices[i] = { static_cast<float>(i), 0.0f, 0.0f };
        mesh.normals[i] = { 0.0f, 1.0f, 0.0f };
        mesh.uvs[i] = { static_cast<float>(i) / static_cast<float>(num_verts), 0.5f };
        mesh.weights[i].bone_indices.push_back(static_cast<int32_t>(i % 10));
        mesh.weights[i].bone_weights.push_back(1.0f);
    }

    // Connect vertices into triangles: (0, 1, 2), (2, 3, 4), ...
    const uint32_t num_tris = (num_verts - 2) / 2;
    mesh.faces.reserve(num_tris);
    for (uint32_t i = 0; i < num_tris; ++i) {
        mesh.faces.push_back({ i * 2, i * 2 + 1, i * 2 + 2 });
    }
    mesh.face_uvs = mesh.faces;
    mesh.face_normals = mesh.faces;

    assert(mesh.vertices.size() == 97000);
    assert(mesh.faces.size() == num_tris);

    // Run partitioner with 64,000 threshold
    auto submeshes = grn::split_mesh_16bit(mesh, 64000);

    std::cout << "  Original: 97,000 vertices, " << mesh.faces.size() << " triangles.\n";
    std::cout << "  Partitioned into " << submeshes.size() << " sub-meshes.\n";

    assert(submeshes.size() >= 2);

    size_t total_faces = 0;
    for (size_t i = 0; i < submeshes.size(); ++i) {
        const auto& sm = submeshes[i];
        std::cout << "    Sub-mesh " << i << " (" << sm.name << "): "
                  << sm.vertices.size() << " vertices, "
                  << sm.faces.size() << " triangles.\n";

        // Every submesh must strictly obey 16-bit Granny limit
        assert(sm.vertices.size() <= 64000);
        assert(!sm.faces.empty());
        total_faces += sm.faces.size();

        // Check index validity
        for (const auto& f : sm.faces) {
            if (f[0] >= sm.vertices.size() || f[1] >= sm.vertices.size() || f[2] >= sm.vertices.size()) {
                throw std::runtime_error("Invalid triangle index in sub-mesh");
            }
        }

        // Check normals, uvs, and weights consistency
        assert(sm.normals.size() == sm.vertices.size());
        assert(sm.uvs.size() == sm.vertices.size());
        assert(sm.weights.size() == sm.vertices.size());
    }

    // Export sample 97k GLB fixture for UI and converter verification
    grn::GrnModel m97k;
    m97k.meshes.push_back(mesh);
    grn::GlbExportOptions exp_opts;
    grn::export_grn_to_glb_file("test_grn/GLB/HERO_97K.glb", m97k, exp_opts);
}

static void test_model_optimizer_roundtrip() {
    std::cout << "[TEST] Full Model 16-bit Optimization & GRN Serialization..." << std::endl;

    grn::GrnModel model;
    grn::GrnMesh mesh;
    mesh.name = "Armor_97k";

    const uint32_t num_verts = 70000;
    mesh.vertices.resize(num_verts);
    mesh.normals.resize(num_verts);
    mesh.uvs.resize(num_verts);
    for (uint32_t i = 0; i < num_verts; ++i) {
        mesh.vertices[i] = { 0.0f, static_cast<float>(i), 0.0f };
        mesh.normals[i] = { 0.0f, 0.0f, 1.0f };
        mesh.uvs[i] = { 0.0f, 0.0f };
    }
    const uint32_t num_tris = (num_verts - 2) / 2;
    for (uint32_t i = 0; i < num_tris; ++i) {
        mesh.faces.push_back({ i * 2, i * 2 + 1, i * 2 + 2 });
    }
    mesh.face_uvs = mesh.faces;
    mesh.face_normals = mesh.faces;
    model.meshes.push_back(mesh);

    grn::MeshOptimizerOptions opts;
    opts.auto_split_16bit = true;
    opts.max_vertices_per_part = 64000;

    grn::optimize_model_meshes(model, opts);

    if (model.meshes.size() < 2) throw std::runtime_error("Expected at least 2 submeshes");
    for (const auto& m : model.meshes) {
        if (m.vertices.size() > 64000) throw std::runtime_error("Submesh exceeds 64000 vertices");
    }

    // Verify Granny 1.2b writer succeeds and parser reads it back
    auto grn_data = grn::write_grn_memory(model);
    assert(!grn_data.empty());

    auto parsed = grn::parse_grn_memory(grn_data.data(), grn_data.size());
    assert(parsed.has_value());
    assert(parsed->meshes.size() == model.meshes.size());

    std::cout << "  ✓ Successfully serialized and parsed " << parsed->meshes.size()
              << " partitioned Granny 1.2b meshes.\n";
}

static void test_multi_material_tri_groups_split() {
    std::cout << "[TEST] Multi-material tri_groups 16-bit partitioning..." << std::endl;

    grn::GrnMesh mesh;
    mesh.name = "Centipede_MultiMat";

    // 3 groups: 30k (mat 0), 20k (mat 1), 35k (mat 2) => total 85k vertices
    const uint32_t count0 = 30000;
    const uint32_t count1 = 20000;
    const uint32_t count2 = 35000;
    const uint32_t total_verts = count0 + count1 + count2;

    mesh.vertices.resize(total_verts);
    mesh.normals.resize(total_verts);
    mesh.uvs.resize(total_verts);
    mesh.weights.resize(total_verts);

    for (uint32_t i = 0; i < total_verts; ++i) {
        mesh.vertices[i] = { static_cast<float>(i), 0.0f, 0.0f };
        mesh.normals[i] = { 0.0f, 1.0f, 0.0f };
        mesh.uvs[i] = { 0.25f, 0.75f };
        mesh.weights[i].bone_indices.push_back(0);
        mesh.weights[i].bone_weights.push_back(1.0f);
    }

    auto make_group = [&](uint32_t start_v, uint32_t num_v, int32_t mat_id, const std::string& mat_name) {
        grn::GrnTriGroup g;
        g.material_index = mat_id;
        g.material_name = mat_name;
        uint32_t num_t = (num_v - 2) / 2;
        for (uint32_t t = 0; t < num_t; ++t) {
            uint32_t v0 = start_v + t * 2;
            uint32_t v1 = start_v + t * 2 + 1;
            uint32_t v2 = start_v + t * 2 + 2;
            g.faces.push_back({ v0, v1, v2 });
            g.face_normals.push_back({ v0, v1, v2 });
            g.face_uvs.push_back({ v0, v1, v2 });

            mesh.faces.push_back({ v0, v1, v2 });
            mesh.face_normals.push_back({ v0, v1, v2 });
            mesh.face_uvs.push_back({ v0, v1, v2 });
        }
        return g;
    };

    mesh.tri_groups.push_back(make_group(0, count0, 0, "legs"));
    mesh.tri_groups.push_back(make_group(count0, count1, 1, "body"));
    mesh.tri_groups.push_back(make_group(count0 + count1, count2, 2, "arm"));

    auto parts = grn::split_mesh_16bit(mesh, 64000);
    assert(parts.size() == 2);

    // Part 0 should cleanly contain legs (30k) + body (20k) = 50k <= 64k
    assert(parts[0].vertices.size() == count0 + count1);
    assert(parts[0].tri_groups.size() == 2);
    assert(parts[0].tri_groups[0].material_name == "legs");
    assert(parts[0].tri_groups[1].material_name == "body");

    // Part 1 should cleanly contain arm (35k) <= 64k
    assert(parts[1].vertices.size() == count2);
    assert(parts[1].tri_groups.size() == 1);
    assert(parts[1].tri_groups[0].material_name == "arm");

    std::cout << "  ✓ Multi-material tri_groups partitioned with 0 cuts: "
              << "Part 0 has " << parts[0].tri_groups.size() << " groups (" << parts[0].vertices.size() << " verts), "
              << "Part 1 has " << parts[1].tri_groups.size() << " groups (" << parts[1].vertices.size() << " verts).\n";
}

int main() {
    try {
        test_split_97k_mesh();
        test_model_optimizer_roundtrip();
        test_multi_material_tri_groups_split();
        std::cout << "\n[PASS] All mesh optimizer tests passed successfully!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] Exception: " << ex.what() << std::endl;
        return 1;
    }
}
