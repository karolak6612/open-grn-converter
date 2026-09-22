/**
 * @file test_viewer_engine.cpp
 * @brief Unit and integration tests for 3D viewer components:
 *        OrbitCamera, GrnAnimSampler, and SkinningEngine.
 */

#include "core/grn_types.h"
#include "core/grn_parser.h"
#include "gui/viewer/camera.h"
#include "gui/viewer/grn_anim_sampler.h"
#include "gui/viewer/skinning_engine.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

static fs::path find_asset(const std::string& relPath) {
    std::vector<fs::path> candidates = {
        fs::current_path() / relPath,
        fs::current_path() / ".." / relPath,
        fs::current_path() / "test_grn" / relPath,
        fs::path("E:/Github/open-grn-converter") / relPath
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            return p;
        }
    }
    return {};
}

static void test_orbit_camera() {
    std::cout << "[TEST] OrbitCamera ..." << std::endl;

    grn::OrbitCamera cam;
    assert(cam.distance > 0.0f);

    // 1. Frame bounds
    QVector3D minBox(-10.0f, -20.0f, -5.0f);
    QVector3D maxBox(10.0f, 20.0f, 15.0f);
    cam.frameBounds(minBox, maxBox);

    QVector3D expectedTarget(0.0f, 0.0f, 5.0f);
    assert((cam.target - expectedTarget).length() < 1e-4f);
    assert(cam.distance > 20.0f);

    // 2. Eye vector validity
    QVector3D eye = cam.eye();
    assert(!std::isnan(eye.x()) && !std::isnan(eye.y()) && !std::isnan(eye.z()));
    float eyeDist = (eye - cam.target).length();
    assert(std::abs(eyeDist - cam.distance) < 1e-3f);
    (void)eyeDist;

    // 3. View matrix
    QMatrix4x4 v = cam.view();
    float det = v.determinant();
    assert(std::abs(std::abs(det) - 1.0f) < 1e-3f);
    (void)det;

    // 4. ViewProj matrix
    QMatrix4x4 vp = cam.viewProj(16.0f / 9.0f);
    assert(!std::isnan(vp(0, 0)) && vp.determinant() != 0.0f);

    // 5. Orbit
    float prevYaw = cam.yaw;
    cam.orbit(10.0f, 5.0f);
    assert(cam.yaw != prevYaw);
    (void)prevYaw;

    // 6. Zoom
    float prevDist = cam.distance;
    cam.zoom(0.8f);
    assert(cam.distance < prevDist);
    cam.zoom(1.25f);
    assert(std::abs(cam.distance - prevDist) < 1e-2f);
    (void)prevDist;

    // 7. Pan
    QVector3D prevTarget = cam.target;
    cam.pan(20.0f, -10.0f);
    assert(cam.target != prevTarget);
    (void)prevTarget;

    // 8. Reset
    cam.reset();
    assert(std::abs(cam.yaw - 0.6f) < 1e-4f);
    assert(std::abs(cam.pitch - 0.35f) < 1e-4f);

    std::cout << "  -> OrbitCamera passed" << std::endl;
}

static void test_grn_anim_sampler_synthetic() {
    std::cout << "[TEST] GrnAnimSampler synthetic ..." << std::endl;

    grn::GrnAnimation anim;
    anim.name = "TestClip";
    anim.duration = 2.0f;
    anim.fps = 30.0f;

    grn::AnimTrack track;
    track.channel_id = 1;
    track.bone_name = "Root";

    // Position: linear curve from (0,0,0) to (10, 20, 30) over [0, 2]s
    track.position_interp_mode = 1; // Linear
    track.translation_times = { 0.0f, 2.0f };
    track.translations = { { 0.0f, 0.0f, 0.0f }, { 10.0f, 20.0f, 30.0f } };

    // Rotation: constant identity quaternion
    track.quaternion_interp_mode = 0; // Constant
    track.rotation_times = { 0.0f };
    track.rotations = { { 0.0f, 0.0f, 0.0f, 1.0f } };

    // Scale: constant identity scale
    track.scale_shear_interp_mode = 0;
    track.scale_shear_times = { 0.0f };
    track.scale_shears = { { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f } };

    anim.tracks.push_back(track);

    std::vector<grn::GrnBone> bones(1);
    bones[0].name = "Root";
    bones[0].parent_index = -1;

    grn::GrnAnimSampler sampler;
    sampler.setAnimation(anim, bones);
    assert(sampler.hasAnimation());
    assert(std::abs(sampler.duration() - 2.0f) < 1e-4f);

    grn::Vec3 pos;
    grn::Vec4 rot;
    std::array<float, 9> scale;

    // Sample at t = 0.0
    sampler.sampleBone(0, bones[0].position, bones[0].rotation, bones[0].scale_3x3, 0.0f, pos, rot, scale);
    assert(std::abs(pos.x - 0.0f) < 1e-3f);
    assert(std::abs(pos.y - 0.0f) < 1e-3f);
    assert(std::abs(pos.z - 0.0f) < 1e-3f);
    assert(std::abs(rot.w - 1.0f) < 1e-3f);

    // Sample at t = 1.0 (halfway)
    sampler.sampleBone(0, bones[0].position, bones[0].rotation, bones[0].scale_3x3, 1.0f, pos, rot, scale);
    assert(std::abs(pos.x - 5.0f) < 1e-2f);
    assert(std::abs(pos.y - 10.0f) < 1e-2f);
    assert(std::abs(pos.z - 15.0f) < 1e-2f);

    // Sample at t = 2.0 (end)
    sampler.sampleBone(0, bones[0].position, bones[0].rotation, bones[0].scale_3x3, 2.0f, pos, rot, scale);
    assert(std::abs(pos.x - 10.0f) < 1e-2f);
    assert(std::abs(pos.y - 20.0f) < 1e-2f);
    assert(std::abs(pos.z - 30.0f) < 1e-2f);

    std::cout << "  -> GrnAnimSampler synthetic passed" << std::endl;
}

static void test_skinning_engine_synthetic() {
    std::cout << "[TEST] SkinningEngine synthetic chain ..." << std::endl;

    grn::GrnModel model;
    model.bones.resize(2);

    // Bone 0: Root at origin
    model.bones[0].name = "Root";
    model.bones[0].parent_index = -1;
    model.bones[0].position = { 0.0f, 0.0f, 0.0f };
    model.bones[0].rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    model.bones[0].scale_3x3 = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    // Bone 1: Child offset along Z by +10.0
    model.bones[1].name = "Child";
    model.bones[1].parent_index = 0;
    model.bones[1].position = { 0.0f, 0.0f, 10.0f };
    model.bones[1].rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    model.bones[1].scale_3x3 = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    // Mesh with 2 vertices:
    // v0: at (0,0,0) skinned 100% to Root
    // v1: at (0,0,10) skinned 100% to Child
    grn::GrnMesh mesh;
    mesh.name = "TestMesh";
    mesh.vertices = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 10.0f } };
    mesh.weights.resize(2);
    mesh.weights[0].bone_indices = { 0 };
    mesh.weights[0].bone_weights = { 1.0f };
    mesh.weights[1].bone_indices = { 1 };
    mesh.weights[1].bone_weights = { 1.0f };
    model.meshes.push_back(mesh);

    grn::SkinningEngine engine;
    engine.setModel(&model);

    // Evaluate rest pose
    engine.evaluate(0.0f);
    const auto& positions = engine.skinnedPositions();
    assert(positions.size() == 1);
    assert(positions[0].size() == 2);
    (void)positions;

    assert((positions[0][0] - QVector3D(0.0f, 0.0f, 0.0f)).length() < 1e-4f);
    assert((positions[0][1] - QVector3D(0.0f, 0.0f, 10.0f)).length() < 1e-4f);

    // Verify bounds
    QVector3D minB, maxB;
    engine.computeBounds(minB, maxB);
    assert(std::abs(minB.z() - 0.0f) < 1e-3f);
    assert(std::abs(maxB.z() - 10.0f) < 1e-3f);

    std::cout << "  -> SkinningEngine synthetic chain passed" << std::endl;
}

static void test_viewer_real_assets() {
    std::cout << "[TEST] Viewer with real Granny assets ..." << std::endl;

    fs::path barbPath = find_asset("test_grn/GRN_ORIGINAL/BARBARIAN.grn");
    if (barbPath.empty()) {
        std::cout << "  (Skipping real asset test: BARBARIAN.grn not found)" << std::endl;
        return;
    }

    auto barbModel = grn::parse_grn_file(barbPath);
    assert(barbModel.has_value());
    assert(!barbModel->meshes.empty());
    assert(!barbModel->bones.empty());

    grn::SkinningEngine engine;
    engine.setModel(&*barbModel);

    // 1. Rest pose evaluation: all skinned positions must match original mesh vertices
    engine.evaluate(0.0f);
    const auto& positions = engine.skinnedPositions();
    assert(positions.size() == barbModel->meshes.size());

    for (size_t mi = 0; mi < barbModel->meshes.size(); ++mi) {
        const auto& origVerts = barbModel->meshes[mi].vertices;
        const auto& skinVerts = positions[mi];
        assert(skinVerts.size() == origVerts.size());
        for (size_t vi = 0; vi < origVerts.size(); ++vi) {
            QVector3D orig(origVerts[vi].x, origVerts[vi].y, origVerts[vi].z);
            float diff = (skinVerts[vi] - orig).length();
            assert(diff < 1e-3f);
            (void)diff;
        }
    }

    // 2. Load animation clip
    fs::path animPath = find_asset("test_grn/GRN_ORIGINAL/DELV_ATTACK_SPECIAL03.grn");
    if (!animPath.empty()) {
        auto animModel = grn::parse_grn_file(animPath);
        if (animModel && !animModel->animations.empty()) {
            engine.setAnimation(&animModel->animations[0]);
            assert(engine.hasAnimation());
            assert(engine.duration() > 0.0f);

            // Evaluate at t = 0.5s
            engine.evaluate(0.5f);
            const auto& animPos = engine.skinnedPositions();
            for (const auto& meshPos : animPos) {
                for (const auto& p : meshPos) {
                    assert(!std::isnan(p.x()) && !std::isnan(p.y()) && !std::isnan(p.z()));
                    assert(!std::isinf(p.x()) && !std::isinf(p.y()) && !std::isinf(p.z()));
                    (void)p;
                }
            }

            QVector3D minB, maxB;
            engine.computeBounds(minB, maxB);
            assert(!std::isnan(minB.x()) && !std::isnan(maxB.x()));
            assert(minB.x() < maxB.x());
            assert(minB.y() < maxB.y());
            assert(minB.z() < maxB.z());
        }
    }

    std::cout << "  -> Viewer real assets passed" << std::endl;
}

static void test_skinning_engine_scale() {
    std::cout << "[TEST] SkinningEngine dynamic scale ..." << std::endl;

    grn::GrnModel model;
    grn::GrnMesh mesh;
    mesh.name = "box";
    mesh.vertices = { { -1.0f, -2.0f, -3.0f }, { 1.0f, 2.0f, 3.0f } };
    mesh.faces = { { 0, 1, 0 } };
    model.meshes.push_back(mesh);

    grn::SkinningEngine engine;
    engine.setModel(&model);

    // Default scale 1.0
    engine.evaluate(0.0f);
    QVector3D minB, maxB;
    engine.computeBounds(minB, maxB);
    assert(std::abs(minB.x() - (-1.0f)) < 1e-4f);
    assert(std::abs(maxB.z() - 3.0f) < 1e-4f);

    // Scale 2.5x
    engine.setScale(2.5f);
    assert(std::abs(engine.scale() - 2.5f) < 1e-5f);
    engine.evaluate(0.0f);
    engine.computeBounds(minB, maxB);
    assert(std::abs(minB.x() - (-2.5f)) < 1e-4f);
    assert(std::abs(maxB.z() - 7.5f) < 1e-4f);

    // Verify positions
    const auto& pos = engine.skinnedPositions();
    assert(std::abs(pos[0][0].x() - (-2.5f)) < 1e-4f);
    assert(std::abs(pos[0][1].z() - 7.5f) < 1e-4f);
    (void)pos;

    std::cout << "  -> SkinningEngine scale passed" << std::endl;
}

int main() {
    try {
        test_orbit_camera();
        test_grn_anim_sampler_synthetic();
        test_skinning_engine_synthetic();
        test_viewer_real_assets();
        test_skinning_engine_scale();
        std::cout << "\nALL VIEWER ENGINE TESTS PASSED!" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FATAL ERROR: " << ex.what() << std::endl;
        return 1;
    }
}
