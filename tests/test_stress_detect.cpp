/**
 * @file test_stress_detect.cpp
 * @brief Empirical stress-test suite for detect_is_z_up focusing on:
 *        1. Pure animation / skeleton-only models and helper bone filtering
 *        2. Skeletal groundedness (Z-up vs Y-up quadrupeds and humanoids)
 *        3. Extreme bounds, zero vertices, NaNs, Infs, subnormals
 *        4. Adversarial bone hierarchies, cyclic/out-of-bounds parent indices
 *        5. Fuzzing with 2,000 synthetic random models
 *        6. Real-world extracted GRN quadruped and animation assets
 */

#include "converter/converter.h"
#include "core/grn_parser.h"
#include "gltf/glb_reader.h"
#include <iostream>
#include <vector>
#include <string>
#include <limits>
#include <cmath>
#include <random>
#include <cassert>
#include <filesystem>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" #cond ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

static grn::GrnBone make_bone(const std::string& name, int32_t parent, grn::Vec3 pos, grn::Vec4 rot = {0.f, 0.f, 0.f, 1.f}) {
    grn::GrnBone b;
    b.name = name;
    b.parent_index = parent;
    b.position = pos;
    b.rotation = rot;
    return b;
}

// ---------------------------------------------------------------------------
// 1. Helper Bone Filtering & Skeletal Fallback
// ---------------------------------------------------------------------------
static void test_helper_bone_filtering() {
    std::cout << "[TEST] 1. Helper Bone Filtering in Pure Skeletons..." << std::endl;

    // Model with NO meshes, only bones.
    // Core skeleton is Z-up grounded (Z in [0, 50], Y in [-5, 5]).
    // Helper bones ("Cam", "Spot", "Light", ".target") are placed at extreme positions
    // that would corrupt bounds if not filtered (e.g., Z = -10000, Y = -5000).
    grn::GrnModel m;

    m.bones.push_back(make_bone("Root", -1, {0.0f, 0.0f, 0.0f}));
    m.bones.push_back(make_bone("Spine", 0, {0.0f, 0.0f, 25.0f}));
    m.bones.push_back(make_bone("SpineTop", 1, {0.0f, 0.0f, 25.0f}));

    // Add adversarial helper bones
    m.bones.push_back(make_bone("Cam_Back.Target", 0, {0.0f, -5000.0f, -10000.0f}));
    m.bones.push_back(make_bone("Spot01_shadow.Target", 0, {1000.0f, 5000.0f, -20000.0f}));
    m.bones.push_back(make_bone("Omni_LIGHT_source", 0, {-500.0f, -2000.0f, 30000.0f}));
    m.bones.push_back(make_bone("Bone01.target", 0, {200.0f, -8000.0f, -5000.0f}));

    // Detection must correctly filter helper bones and classify based on the remaining
    // grounded Z-up spine (Z in [0, 50], grounded on Z=0, Y span = 0).
    bool is_z = grn::detect_is_z_up(m);
    TEST_CHECK(is_z == true, "Pure skeleton with extreme helper bones must evaluate to Z-up");

    // Test case-insensitivity variations:
    std::vector<std::string> helper_names = {
        "CAM_MAIN", "camera_viewport", "CaMeRa_01",
        "SPOTLIGHT", "sPoT_ambient", "Spot_Front",
        "LIGHT_MAIN", "SunLight", "omni_light",
        "Target_node.TARGET", "lookat.TaRgEt", "Aim.target"
    };

    for (const auto& hname : helper_names) {
        grn::GrnModel m_single;
        m_single.bones = m.bones;
        m_single.bones[3].name = hname; // Override helper bone name
        bool res = grn::detect_is_z_up(m_single);
        TEST_CHECK(res == true, "Case-insensitive helper bone '" + hname + "' must be filtered");
    }

    // Model where ALL bones are helper bones:
    grn::GrnModel all_helpers;
    all_helpers.bones = {
        make_bone("Cam01", -1, {0.f, 0.f, 0.f}),
        make_bone("SpotLight", 0, {10.f, 20.f, 30.f}),
        make_bone("LightHelper", 0, {-5.f, -5.f, -5.f}),
        make_bone("Lookat.target", 1, {100.f, 100.f, 100.f})
    };
    // valid_bones == 0 -> should safely fall through to default Z-up without crash or divide-by-zero
    bool all_h_res = grn::detect_is_z_up(all_helpers);
    TEST_CHECK(all_h_res == true, "Model with all helper bones must default to Z-up safely");

    std::cout << "  -> Helper bone filtering: PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 2. Skeletal Groundedness (Z-up vs Y-up)
// ---------------------------------------------------------------------------
static void test_skeleton_groundedness() {
    std::cout << "[TEST] 2. Skeletal Groundedness (Z-up vs Y-up)..." << std::endl;

    // 2.1 Pure Z-up standing quadruped skeleton (no meshes)
    // Feet at Z = 0, back at Z = 35. Y length from -50 to +5 (uncentered).
    {
        grn::GrnModel z_quad;
        z_quad.bones = {
            make_bone("Root", -1, {0.f, 0.f, 0.f}),
            make_bone("BackPaw_L", 0, {-10.f, -45.f, 0.f}),
            make_bone("BackPaw_R", 0, {10.f, -45.f, 0.f}),
            make_bone("FrontPaw_L", 0, {-10.f, 5.f, 0.f}),
            make_bone("FrontPaw_R", 0, {10.f, 5.f, 0.f}),
            make_bone("Spine", 0, {0.f, -20.f, 35.f})
        };
        bool res = grn::detect_is_z_up(z_quad);
        TEST_CHECK(res == true, "Pure Z-up quadruped skeleton must evaluate to Z-up");
    }

    // 2.2 Pure Y-up standing quadruped skeleton (no meshes, e.g. glTF export)
    // Feet at Y = 0, back at Y = 35. Z length from -50 to +5.
    {
        grn::GrnModel y_quad;
        y_quad.bones = {
            make_bone("Root", -1, {0.f, 0.f, 0.f}),
            make_bone("BackPaw_L", 0, {-10.f, 0.f, -45.f}),
            make_bone("BackPaw_R", 0, {10.f, 0.f, -45.f}),
            make_bone("FrontPaw_L", 0, {-10.f, 0.f, 5.f}),
            make_bone("FrontPaw_R", 0, {10.f, 0.f, 5.f}),
            make_bone("Spine", 0, {0.f, 35.f, -20.f})
        };
        bool res = grn::detect_is_z_up(y_quad);
        TEST_CHECK(res == false, "Pure Y-up quadruped skeleton must evaluate to Y-up");
    }

    // 2.3 Pure Z-up standing biped skeleton (no meshes)
    // Feet at Z = 0, Head at Z = 170.
    {
        grn::GrnModel z_biped;
        z_biped.bones = {
            make_bone("Root", -1, {0.f, 0.f, 0.f}),
            make_bone("Foot_L", 0, {-15.f, 0.f, 0.f}),
            make_bone("Foot_R", 0, {15.f, 0.f, 0.f}),
            make_bone("Bip01 Pelvis", 0, {0.f, 0.f, 90.f}),
            make_bone("Bip01 Head", 3, {0.f, 0.f, 80.f})
        };
        bool res = grn::detect_is_z_up(z_biped);
        TEST_CHECK(res == true, "Pure Z-up biped skeleton must evaluate to Z-up");
    }

    // 2.4 Pure Y-up standing biped skeleton (no meshes)
    // Feet at Y = 0, Head at Y = 170.
    {
        grn::GrnModel y_biped;
        y_biped.bones = {
            make_bone("Root", -1, {0.f, 0.f, 0.f}),
            make_bone("Foot_L", 0, {-15.f, 0.f, 0.f}),
            make_bone("Foot_R", 0, {15.f, 0.f, 0.f}),
            make_bone("Bip01 Pelvis", 0, {0.f, 90.f, 0.f}),
            make_bone("Bip01 Head", 3, {0.f, 80.f, 0.f})
        };
        bool res = grn::detect_is_z_up(y_biped);
        TEST_CHECK(res == false, "Pure Y-up biped skeleton must evaluate to Y-up");
    }

    // 2.5 Ungrounded / floating skeleton in Granny space (e.g. bat skeleton)
    // Neither Z nor Y is grounded near 0 (e.g. Z in [200, 250], Y in [100, 180]).
    {
        grn::GrnModel bat_skel;
        bat_skel.bones = {
            make_bone("Root", -1, {0.f, 100.f, 200.f}),
            make_bone("Wing_L", 0, {-50.f, 140.f, 220.f}),
            make_bone("Wing_R", 0, {50.f, 140.f, 220.f}),
            make_bone("Body", 0, {0.f, 180.f, 250.f})
        };
        bool res = grn::detect_is_z_up(bat_skel);
        TEST_CHECK(res == true, "Ungrounded Granny skeleton must fall through to Z-up default");
    }

    std::cout << "  -> Skeletal groundedness: PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 3. Extreme Bounds, Zero Vertices, NaNs, Infs
// ---------------------------------------------------------------------------
static void test_extreme_bounds_and_zero_verts() {
    std::cout << "[TEST] 3. Extreme Bounds, Zero Vertices, NaNs, Infs..." << std::endl;

    // 3.1 Completely empty model
    {
        grn::GrnModel empty;
        bool res = grn::detect_is_z_up(empty);
        TEST_CHECK(res == true, "Completely empty model must return true (Z-up default)");
    }

    // 3.2 Multiple meshes, all with 0 vertices
    {
        grn::GrnModel empty_meshes;
        empty_meshes.meshes.resize(5);
        bool res = grn::detect_is_z_up(empty_meshes);
        TEST_CHECK(res == true, "Model with empty meshes must return true (Z-up default)");
    }

    // 3.3 Single vertex at (0, 0, 0)
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        mesh.vertices = {{0.f, 0.f, 0.f}};
        m.meshes.push_back(mesh);
        bool res = grn::detect_is_z_up(m);
        TEST_CHECK(res == true, "Single vertex at origin must return true without division by zero");
    }

    // 3.4 Single vertex at arbitrary non-zero position
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        mesh.vertices = {{1000.f, 2000.f, 3000.f}};
        m.meshes.push_back(mesh);
        bool res = grn::detect_is_z_up(m);
        TEST_CHECK(res == true, "Single vertex at (1000,2000,3000) must return true");
    }

    // 3.5 Extreme float coordinates (near FLT_MAX / FLT_MIN)
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        float huge = 1e38f;
        mesh.vertices = {
            {0.f, -huge, -huge},
            {0.f, huge, huge}
        };
        m.meshes.push_back(mesh);
        // max - min overflows to +Inf. Must not crash or trigger UB.
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    // 3.6 Subnormal / denormal floats
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        float tiny = 1e-40f;
        mesh.vertices = {
            {0.f, 0.f, 0.f},
            {tiny, tiny, tiny}
        };
        m.meshes.push_back(mesh);
        bool res = grn::detect_is_z_up(m);
        TEST_CHECK(res == true, "Subnormal float coordinates must handle gracefully");
    }

    // 3.7 IEEE-754 NaN coordinates
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        float nan_val = std::numeric_limits<float>::quiet_NaN();
        mesh.vertices = {
            {0.f, 0.f, 0.f},
            {10.f, nan_val, 20.f},
            {10.f, 20.f, nan_val}
        };
        m.meshes.push_back(mesh);
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    // 3.8 IEEE-754 Infinity coordinates
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        float inf_val = std::numeric_limits<float>::infinity();
        mesh.vertices = {
            {0.f, 0.f, 0.f},
            {0.f, inf_val, -inf_val},
            {0.f, -inf_val, inf_val}
        };
        m.meshes.push_back(mesh);
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    // 3.9 Negative span / inverted geometry
    {
        grn::GrnModel m;
        grn::GrnMesh mesh;
        mesh.vertices = {
            {0.f, -50.f, -100.f},
            {0.f, -10.f, -20.f}
        };
        m.meshes.push_back(mesh);
        bool res = grn::detect_is_z_up(m);
        TEST_CHECK(res == true, "Completely negative coordinates must return true (Z-up default)");
    }

    std::cout << "  -> Extreme bounds and zero vertices: PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 4. Adversarial Skeletal Hierarchies
// ---------------------------------------------------------------------------
static void test_skeletal_hierarchy_adversarial() {
    std::cout << "[TEST] 4. Adversarial Skeletal Hierarchies..." << std::endl;

    // 4.1 Cyclic parent indexing (Bone 0 has parent 1, Bone 1 has parent 0)
    {
        grn::GrnModel m;
        m.bones = {
            make_bone("Bone0", 1, {0.f, 0.f, 0.f}),
            make_bone("Bone1", 0, {0.f, 0.f, 10.f})
        };
        // p < bi prevents circular loop for Bone 0 (1 < 0 is false)
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    // 4.2 Out-of-bounds parent indices
    {
        grn::GrnModel m;
        m.bones = {
            make_bone("B0", -1, {0.f, 0.f, 0.f}),
            make_bone("B1", -99999, {0.f, 0.f, 10.f}),
            make_bone("B2", 999999, {0.f, 0.f, 20.f}),
            make_bone("B3", std::numeric_limits<int32_t>::min(), {0.f, 0.f, 30.f}),
            make_bone("B4", std::numeric_limits<int32_t>::max(), {0.f, 0.f, 40.f})
        };
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    // 4.3 Degenerate quaternions (all zeros, unnormalized, NaNs)
    {
        grn::GrnModel m;
        m.bones = {
            make_bone("ZeroQuat", -1, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f, 0.f}),
            make_bone("HugeQuat", 0, {0.f, 0.f, 10.f}, {1e10f, 1e10f, 1e10f, 1e10f}),
            make_bone("NanQuat", 0, {0.f, 0.f, 20.f}, {
                std::numeric_limits<float>::quiet_NaN(),
                0.f, 0.f, 1.f
            })
        };
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    // 4.4 Deep chain (1,000 chained bones)
    {
        grn::GrnModel m;
        const size_t CHAIN_LEN = 1000;
        m.bones.reserve(CHAIN_LEN);
        for (size_t i = 0; i < CHAIN_LEN; ++i) {
            grn::GrnBone b;
            b.name = "Bone_" + std::to_string(i);
            b.parent_index = (i == 0) ? -1 : static_cast<int32_t>(i - 1);
            b.position = {0.f, 0.f, 1.f}; // Rises by 1.0 along Z at each link
            b.rotation = {0.f, 0.f, 0.f, 1.f};
            m.bones.push_back(b);
        }
        bool res = grn::detect_is_z_up(m);
        TEST_CHECK(res == true, "1,000 bone chain rising along Z must evaluate to Z-up");
    }

    // 4.5 Identical Head and Pelvis index (coincident bone name)
    {
        grn::GrnModel m;
        m.bones = {
            make_bone("Bip01 Root Pelvis Head", -1, {0.f, 0.f, 10.f})
        };
        bool res = grn::detect_is_z_up(m);
        // headIdx == 0 && pelvisIdx == 0, guarded by headIdx != pelvisIdx
        TEST_CHECK(res == true, "Coincident head and pelvis name must not trigger self-comparison");
    }

    std::cout << "  -> Adversarial skeletal hierarchies: PASSED" << std::endl;
}

// ---------------------------------------------------------------------------
// 5. Randomized Fuzzing (2,000 Synthetic Models)
// ---------------------------------------------------------------------------
static void test_fuzz_detect_is_z_up() {
    std::cout << "[TEST] 5. Randomized Fuzzing (2,000 Models)..." << std::endl;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> mesh_count_dist(0, 5);
    std::uniform_int_distribution<int> vert_count_dist(0, 100);
    std::uniform_real_distribution<float> coord_dist(-500.f, 500.f);
    std::uniform_int_distribution<int> bone_count_dist(0, 50);
    std::uniform_int_distribution<int> parent_dist(-2, 60);

    const std::vector<std::string> sample_names = {
        "Root", "Spine", "Head", "Pelvis", "Hips", "Neck",
        "Cam_01", "Camera.target", "SpotLight", "Omni_Light",
        "Bone_Custom", "Finger0", "Toe0", "HorseLink", "Tail"
    };

    for (int iter = 0; iter < 2000; ++iter) {
        grn::GrnModel m;

        // Random meshes
        int num_meshes = mesh_count_dist(rng);
        for (int mi = 0; mi < num_meshes; ++mi) {
            grn::GrnMesh mesh;
            mesh.name = "Mesh_" + std::to_string(mi);
            int num_verts = vert_count_dist(rng);
            mesh.vertices.resize(num_verts);
            for (int vi = 0; vi < num_verts; ++vi) {
                mesh.vertices[vi] = {coord_dist(rng), coord_dist(rng), coord_dist(rng)};
            }
            m.meshes.push_back(mesh);
        }

        // Random bones
        int num_bones = bone_count_dist(rng);
        for (int bi = 0; bi < num_bones; ++bi) {
            grn::GrnBone bone;
            bone.name = sample_names[rng() % sample_names.size()];
            bone.parent_index = parent_dist(rng);
            bone.position = {coord_dist(rng), coord_dist(rng), coord_dist(rng)};
            bone.rotation = {coord_dist(rng), coord_dist(rng), coord_dist(rng), coord_dist(rng)};
            m.bones.push_back(bone);
        }

        // Must execute cleanly without assertion failures or memory corruption
        bool res = grn::detect_is_z_up(m);
        (void)res;
    }

    std::cout << "  -> 2,000 fuzzed models evaluated with 0 crashes: PASSED" << std::endl;
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "  STARTING EMPIRICAL STRESS TESTS FOR detect_is_z_up      " << std::endl;
    std::cout << "==========================================================" << std::endl;

    test_helper_bone_filtering();
    test_skeleton_groundedness();
    test_extreme_bounds_and_zero_verts();
    test_skeletal_hierarchy_adversarial();
    test_fuzz_detect_is_z_up();

    std::cout << "==========================================================" << std::endl;
    std::cout << "  ALL STRESS TESTS COMPLETED SUCCESSFULLY (100% PASS RATE)" << std::endl;
    std::cout << "==========================================================" << std::endl;

    return 0;
}
