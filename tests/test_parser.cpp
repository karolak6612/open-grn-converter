/**
 * @file test_parser.cpp
 * @brief Unit tests for GRN chunk parser and model deserialization.
 */

#include "../src/core/grn_parser.h"
#include "../src/core/grn_writer.h"
#include <iostream>
#include <vector>
#include <cassert>

static void test_parser_synthetic_model() {
    std::cout << "[TEST] GRN Parser with Synthetic Model..." << std::endl;

    grn::GrnModel original;

    // Add bone
    grn::GrnBone bone;
    bone.name = "RootBone";
    bone.parent_index = -1;
    bone.position = {0.0f, 1.0f, 2.0f};
    bone.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    original.bones.push_back(bone);

    // Add mesh
    grn::GrnMesh mesh;
    mesh.name = "TestTriangle";
    mesh.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mesh.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    mesh.faces = {{{0, 1, 2}}};
    mesh.face_uvs = {{{0, 1, 2}}};
    mesh.face_normals = {{{0, 1, 2}}};
    original.meshes.push_back(mesh);

    // Add texture
    grn::GrnTexture tex;
    tex.name = "TestTexture";
    tex.width = 4;
    tex.height = 4;
    tex.format_code = 0; // RawRGBX
    tex.decoded_rgba.resize(4 * 4 * 4, 128);
    original.textures.push_back(tex);

    // Serialize
    auto bytes = grn::write_grn_memory(original);
    assert(!bytes.empty());
    assert(bytes.size() > 16);

    // Parse back
    auto parsed = grn::parse_grn_memory(bytes.data(), bytes.size());
    assert(parsed.has_value());
    assert(parsed->meshes.size() == 1);
    assert(parsed->meshes[0].vertices.size() == 3);
    assert(parsed->meshes[0].faces.size() == 1);
    assert(parsed->textures.size() == 1);
    assert(parsed->textures[0].width == 4);
    assert(parsed->textures[0].height == 4);

    std::cout << "  GRN parser synthetic model test passed." << std::endl;
}

int main() {
    std::cout << "=== Running Parser Unit Tests ===" << std::endl;
    test_parser_synthetic_model();
    std::cout << "All parser tests passed successfully." << std::endl;
    return 0;
}
