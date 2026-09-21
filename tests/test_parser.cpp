/**
 * @file test_parser.cpp
 * @brief Unit tests for GRN chunk parser and model deserialization.
 */

#include "../src/core/grn_parser.h"
#include "../src/core/grn_writer.h"
#include "../src/gltf/glb_writer.h"
#include "../src/gltf/glb_reader.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <unordered_set>

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

static void test_parser_synthetic_animation() {
    std::cout << "[TEST] GRN Parser with Synthetic Skeletal Animation..." << std::endl;

    grn::GrnModel original;

    // Add bone
    grn::GrnBone bone;
    bone.name = "Bone_Spine";
    bone.parent_index = -1;
    bone.position = {0.0f, 0.0f, 0.0f};
    bone.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    original.bones.push_back(bone);

    // Add animation with a split track
    grn::GrnAnimation anim;
    anim.name = "Synthetic_Action";
    anim.duration = 1.0f;

    grn::AnimTrack track;
    track.bone_name = "Bone_Spine";
    track.channel_id = 1;
    track.format = "split";
    track.translation_times = {0.0f, 0.5f, 1.0f};
    track.translations = {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    track.rotation_times = {0.0f, 1.0f};
    track.rotations = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.707f, 0.707f}};

    anim.tracks.push_back(std::move(track));
    original.animations.push_back(std::move(anim));

    // Serialize
    auto bytes = grn::write_grn_memory(original);
    assert(!bytes.empty());

    // Parse back
    auto parsed = grn::parse_grn_memory(bytes.data(), bytes.size());
    assert(parsed.has_value());
    assert(parsed->bones.size() == 1);
    assert(parsed->bones[0].name == "Bone_Spine");
    assert(parsed->animations.size() == 1);
    assert(parsed->animations[0].name == "Synthetic_Action");
    assert(parsed->animations[0].tracks.size() == 1);
    assert(parsed->animations[0].tracks[0].bone_name == "Bone_Spine");
    assert(parsed->animations[0].tracks[0].translations.size() == 3);
    assert(parsed->animations[0].tracks[0].rotations.size() == 2);

    std::cout << "  GRN parser synthetic animation test passed." << std::endl;
}

int main() {
    std::cout << "=== Running Parser Unit Tests ===" << std::endl;
    test_parser_synthetic_model();
    test_parser_synthetic_animation();
    std::cout << "All parser tests passed successfully." << std::endl;
    return 0;
}
