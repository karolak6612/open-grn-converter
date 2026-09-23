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

static void test_parser_glb_rotor_weights() {
    std::filesystem::path rotor_path = "test_data/Gladiator_Rotor.glb";
    if (!std::filesystem::exists(rotor_path)) {
        std::cout << "  [SKIP] Gladiator_Rotor.glb not present." << std::endl;
        return;
    }
    std::cout << "[TEST] GLB Reader Gladiator_Rotor.glb bone weights..." << std::endl;
    grn::GlbImportOptions opt;
    auto model = grn::load_glb_file(rotor_path, opt);
    assert(model.has_value());
    assert(model->bones.size() == 81);

    // Check that rotor bones (joints 68..80) are referenced in mesh weights
    bool found_rotor_joint = false;
    for (const auto& mesh : model->meshes) {
        for (const auto& vw : mesh.weights) {
            for (int32_t b : vw.bone_indices) {
                if (b >= 68 && b <= 80) {
                    found_rotor_joint = true;
                    break;
                }
            }
            if (found_rotor_joint) break;
        }
        if (found_rotor_joint) break;
    }
    assert(found_rotor_joint && "Rotor bones 68..80 must be present in mesh weights!");
    std::cout << "  Gladiator_Rotor bone weights test passed (rotor joints 68..80 verified)." << std::endl;
}

static void inspect_centi_grn() {
    auto model = grn::parse_grn_file("test_data/centi.grn");
    if (model) {
        std::cout << "centi.grn has " << model->meshes.size() << " meshes:\n";
        size_t total_v = 0, total_t = 0;
        for (size_t i = 0; i < model->meshes.size(); ++i) {
            const auto& m = model->meshes[i];
            std::cout << "  Mesh " << i << " (" << m.name << "): "
                      << m.vertices.size() << " verts, " << m.faces.size() << " tris\n";
            total_v += m.vertices.size();
            total_t += m.faces.size();
        }
        std::cout << "Total: " << total_v << " verts, " << total_t << " tris\n";
    }
}

int main() {
    std::cout << "=== Running Parser Unit Tests ===" << std::endl;
    test_parser_synthetic_model();
    test_parser_synthetic_animation();
    test_parser_glb_rotor_weights();
    inspect_centi_grn();
    std::cout << "All parser tests passed successfully." << std::endl;
    return 0;
}
