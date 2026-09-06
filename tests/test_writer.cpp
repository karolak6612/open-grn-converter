/**
 * @file test_writer.cpp
 * @brief Unit tests for GRN binary chunk serialization and double-write prevention.
 */

#include "../src/core/grn_writer.h"
#include "../src/core/grn_parser.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <filesystem>

namespace fs = std::filesystem;

static void test_writer_no_double_write() {
    std::cout << "[TEST] GRN Writer Double-Write Elimination..." << std::endl;

    grn::GrnModel model;

    // Create a 64x64 texture (16,384 bytes uncompressed RGBA)
    grn::GrnTexture tex;
    tex.name = "Texture_NoBloat";
    tex.width = 64;
    tex.height = 64;
    tex.format_code = 0; // RawRGBX
    tex.decoded_rgba.resize(64 * 64 * 4, 0xAA);
    model.textures.push_back(tex);

    auto bytes = grn::write_grn_memory(model);
    assert(!bytes.empty());

    // In a double-write scenario, the 16KB texture payload would be duplicated
    // resulting in > 32KB. With proper empty container payload, it must be < 20KB.
    std::cout << "  Serialized size for 64x64 texture: " << bytes.size() << " bytes." << std::endl;
    assert(bytes.size() < 20000);

    // Verify chunk structure
    auto parsed = grn::parse_grn_memory(bytes.data(), bytes.size());
    assert(parsed.has_value());
    assert(parsed->textures.size() == 1);
    assert(parsed->textures[0].width == 64);
    assert(parsed->textures[0].height == 64);

    std::cout << "  Double-write elimination test passed." << std::endl;
}

static void test_writer_file_io() {
    std::cout << "[TEST] GRN Writer File I/O..." << std::endl;

    grn::GrnModel model;
    grn::GrnMesh mesh;
    mesh.name = "Quad";
    mesh.vertices = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}};
    mesh.normals = {{0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}};
    mesh.uvs = {{0,0}, {1,0}, {1,1}, {0,1}};
    mesh.faces = {{{0,1,2}}, {{0,2,3}}};
    mesh.face_uvs = mesh.faces;
    mesh.face_normals = mesh.faces;
    model.meshes.push_back(mesh);

    fs::path temp_file = fs::temp_directory_path() / "test_writer_out.grn";
    bool written = grn::write_grn_file(temp_file, model);
    assert(written);

    auto loaded = grn::parse_grn_file(temp_file);
    assert(loaded.has_value());
    assert(loaded->meshes.size() == 1);
    assert(loaded->meshes[0].vertices.size() == 4);
    assert(loaded->meshes[0].faces.size() == 2);

    fs::remove(temp_file);
    std::cout << "  File I/O test passed." << std::endl;
}

int main() {
    std::cout << "=== Running Writer Unit Tests ===" << std::endl;
    test_writer_no_double_write();
    test_writer_file_io();
    std::cout << "All writer tests passed successfully." << std::endl;
    return 0;
}
