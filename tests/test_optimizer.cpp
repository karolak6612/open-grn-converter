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

int main() {
    try {
        test_split_97k_mesh();
        test_model_optimizer_roundtrip();
        std::cout << "\n[PASS] All mesh optimizer tests passed successfully!\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] Exception: " << ex.what() << std::endl;
        return 1;
    }
}
