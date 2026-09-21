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

static void test_parser_test_data() {
    std::filesystem::path medusa_path = "test_data/GRN/MEDUSA.GRN";
    std::filesystem::path anim_path = "test_data/GRN/WOGG_ATTACK_STAB_A.GRN";

    if (std::filesystem::exists(medusa_path)) {
        auto medusa = grn::parse_grn_file(medusa_path);
        assert(medusa.has_value());
        std::cout << "  MEDUSA.GRN: bones=" << medusa->bones.size()
                  << ", meshes=" << medusa->meshes.size()
                  << ", textures=" << medusa->textures.size()
                  << ", animations=" << medusa->animations.size() << std::endl;
        if (!medusa->bones.empty()) {
            std::cout << "    First 5 bones: ";
            for (size_t i = 0; i < std::min(size_t(5), medusa->bones.size()); ++i) {
                std::cout << medusa->bones[i].name << ", ";
            }
            std::cout << std::endl;
        }
        if (!medusa->animations.empty()) {
            std::cout << "    Animation[0]: name=" << medusa->animations[0].name
                      << ", tracks=" << medusa->animations[0].tracks.size() << std::endl;
        }
    }

    if (std::filesystem::exists(anim_path)) {
        auto anim = grn::parse_grn_file(anim_path);
        assert(anim.has_value());
        std::cout << "  WOGG_ATTACK_STAB_A.GRN: bones=" << anim->bones.size()
                  << ", meshes=" << anim->meshes.size()
                  << ", textures=" << anim->textures.size()
                  << ", animations=" << anim->animations.size() << std::endl;
        if (!anim->animations.empty()) {
            std::cout << "    Animation[0]: name=" << anim->animations[0].name
                      << ", duration=" << anim->animations[0].duration
                      << ", tracks=" << anim->animations[0].tracks.size() << std::endl;
            if (!anim->animations[0].tracks.empty()) {
                std::cout << "    First 5 tracks: ";
                for (size_t i = 0; i < std::min(size_t(5), anim->animations[0].tracks.size()); ++i) {
                    const auto& trk = anim->animations[0].tracks[i];
                    std::cout << trk.bone_name << " (ch=" << trk.channel_id << ", f=" << trk.format
                              << ", times=" << (trk.format == "split" ? trk.translation_times.size() : trk.times.size())
                              << "), ";
                }
                std::cout << std::endl;
            }
        }
    }

    if (std::filesystem::exists(medusa_path) && std::filesystem::exists(anim_path)) {
        auto medusa_opt = grn::parse_grn_file(medusa_path);
        auto anim_opt = grn::parse_grn_file(anim_path);
        assert(medusa_opt.has_value() && anim_opt.has_value());

        size_t match_by_name = 0;
        size_t match_by_ch = 0;
        std::unordered_set<std::string> medusa_bone_names;
        for (const auto& b : medusa_opt->bones) {
            medusa_bone_names.insert(b.name);
        }

        if (!anim_opt->animations.empty()) {
            for (const auto& trk : anim_opt->animations[0].tracks) {
                if (medusa_bone_names.count(trk.bone_name)) {
                    match_by_name++;
                }
                if (trk.channel_id > 0 && static_cast<size_t>(trk.channel_id - 1) < medusa_opt->bones.size()) {
                    match_by_ch++;
                }
            }
        }
        if (!anim_opt->animations.empty()) {
            std::cout << "    Searching for tail tracks in WOGG:" << std::endl;
            for (const auto& trk : anim_opt->animations[0].tracks) {
                if (trk.bone_name == "Dummy01" || trk.bone_name == "Bone01" || trk.bone_name == "Bone02" || trk.bone_name == "Root") {
                    std::cout << "      Found track: name='" << trk.bone_name << "', ch=" << trk.channel_id << std::endl;
                }
            }
        }

        // Replace/add anim to medusa
        medusa_opt->animations.clear();
        for (auto& a : anim_opt->animations) {
            a.name = "Attack_Stab";
            medusa_opt->animations.push_back(a);
        }

        grn::GlbExportOptions exp_opt;
        exp_opt.embed_textures = true;
        std::filesystem::path out_glb = std::filesystem::temp_directory_path() / "test_medusa_anim.glb";
        bool ok = grn::export_grn_to_glb_file(out_glb, *medusa_opt, exp_opt);
        std::cout << "  Exported medusa with animation to GLB: " << (ok ? "SUCCESS" : "FAILED") << std::endl;
        std::error_code ec;
        std::filesystem::remove(out_glb, ec);
    }
}

int main() {
    std::cout << "=== Running Parser Unit Tests ===" << std::endl;
    test_parser_synthetic_model();
    test_parser_test_data();
    std::cout << "All parser tests passed successfully." << std::endl;
    return 0;
}
