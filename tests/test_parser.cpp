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

static void test_glb_reader_bone_weights_preservation() {
    std::cout << "[TEST] GLB Reader bone weights preservation..." << std::endl;
    grn::GrnModel original;
    // Add 85 bones
    for (int i = 0; i < 85; ++i) {
        grn::GrnBone bone;
        bone.name = "Bone_" + std::to_string(i);
        bone.parent_index = (i == 0) ? -1 : (i - 1);
        bone.position = {0.0f, static_cast<float>(i), 0.0f};
        bone.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        original.bones.push_back(bone);
    }
    // Add mesh with weights referencing higher-index bones (e.g. bone 75)
    grn::GrnMesh mesh;
    mesh.name = "TestRiggedMesh";
    mesh.vertices = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    mesh.uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    mesh.faces = {{{0, 1, 2}}};
    mesh.face_uvs = {{{0, 1, 2}}};
    mesh.face_normals = {{{0, 1, 2}}};

    grn::VertexWeight w;
    w.bone_indices = {75};
    w.bone_weights = {1.0f};
    mesh.weights = {w, w, w};
    original.meshes.push_back(mesh);

    grn::GlbExportOptions exp_opts;
    auto glb_bytes = grn::export_grn_to_glb_memory(original, exp_opts);
    assert(!glb_bytes.empty());

    grn::GlbImportOptions imp_opts;
    auto imported = grn::load_glb_memory(glb_bytes.data(), glb_bytes.size(), imp_opts);
    assert(imported.has_value());
    assert(imported->bones.size() == 85);

    bool found_joint_75 = false;
    for (const auto& m : imported->meshes) {
        for (const auto& vw : m.weights) {
            for (int32_t b : vw.bone_indices) {
                if (b == 75) {
                    found_joint_75 = true;
                    break;
                }
            }
            if (found_joint_75) break;
        }
        if (found_joint_75) break;
    }
    assert(found_joint_75 && "Bone weight for joint 75 must be preserved in mesh weights!");
    std::cout << "  GLB Reader bone weights preservation test passed." << std::endl;
}

static void test_wolf_diagnostics() {
    std::filesystem::path wolf_path = "E:/Sacred_Unpacked/wolf/0016_WOLF.GRN";
    std::filesystem::path run_path = "E:/Sacred_Unpacked/wolf/1633_WOLF_RUN_BH.GRN";
    if (!std::filesystem::exists(wolf_path) || !std::filesystem::exists(run_path)) return;

    std::cout << "[DIAGNOSTIC] Parsing wolf and run animation..." << std::endl;
    auto wolf = grn::parse_grn_file(wolf_path);
    auto run = grn::parse_grn_file(run_path);
    if (!wolf || !run) {
        std::cout << "Failed to parse wolf files!" << std::endl;
        return;
    }

    std::cout << "Wolf bones (" << wolf->bones.size() << "):" << std::endl;
    for (size_t i = 0; i < std::min(size_t(10), wolf->bones.size()); ++i) {
        std::cout << "  [" << i << "] parent=" << wolf->bones[i].parent_index 
                  << " name='" << wolf->bones[i].name 
                  << "' pos=(" << wolf->bones[i].position.x << "," << wolf->bones[i].position.y << "," << wolf->bones[i].position.z << ")"
                  << " rot=(" << wolf->bones[i].rotation.x << "," << wolf->bones[i].rotation.y << "," << wolf->bones[i].rotation.z << "," << wolf->bones[i].rotation.w << ")\n";
    }

    std::filesystem::path dying_path = "E:/Sacred_Unpacked/wolf/1630_WOLF_DYING_A.GRN";
    std::filesystem::path hit_path = "E:/Sacred_Unpacked/wolf/1631_WOLF_HIT_A.GRN";
    auto dying = grn::parse_grn_file(dying_path);
    auto hit = grn::parse_grn_file(hit_path);
    std::cout << "Dying parsed: " << (dying.has_value() ? "YES" : "NO")
              << " anims=" << (dying ? dying->animations.size() : 0)
              << " bones=" << (dying ? dying->bones.size() : 0) << std::endl;
    if (dying && !dying->animations.empty()) {
        std::cout << "  Dying anim tracks=" << dying->animations[0].tracks.size()
                  << " duration=" << dying->animations[0].duration << std::endl;
    }
    std::cout << "Hit parsed: " << (hit.has_value() ? "YES" : "NO")
              << " anims=" << (hit ? hit->animations.size() : 0)
              << " bones=" << (hit ? hit->bones.size() : 0) << std::endl;
    if (hit && !hit->animations.empty()) {
        std::cout << "  Hit anim tracks=" << hit->animations[0].tracks.size()
                  << " duration=" << hit->animations[0].duration << std::endl;
    }
}

int main() {
    std::cout << "=== Running Parser Unit Tests ===" << std::endl;
    test_parser_synthetic_model();
    test_parser_synthetic_animation();
    test_glb_reader_bone_weights_preservation();
    test_wolf_diagnostics();
    std::cout << "All parser tests passed successfully." << std::endl;
    return 0;
}
