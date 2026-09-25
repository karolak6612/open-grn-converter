/**
 * @file test_challenger_adversarial.cpp
 * @brief Empirical Challenger stress test harness for detect_is_z_up
 */

#include "converter/converter.h"
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <iomanip>

struct TestCaseResult {
    std::string category;
    std::string name;
    bool actual_is_z_up;
    std::string notes;
};

static std::vector<TestCaseResult> results;

static void record(const std::string& cat, const std::string& name, bool res, const std::string& notes = "") {
    results.push_back({cat, name, res, notes});
    std::cout << std::left << std::setw(25) << ("[" + cat + "]")
              << std::setw(35) << name
              << " -> " << (res ? "Z-up (true)" : "Y-up (false)")
              << (notes.empty() ? "" : " | " + notes)
              << std::endl;
}

static grn::GrnModel make_mesh_box(float min_x, float max_x,
                                   float min_y, float max_y,
                                   float min_z, float max_z) {
    grn::GrnModel m;
    grn::GrnMesh mesh;
    mesh.name = "Box";
    mesh.vertices = {
        {min_x, min_y, min_z}, {max_x, min_y, min_z},
        {max_x, max_y, min_z}, {min_x, max_y, min_z},
        {min_x, min_y, max_z}, {max_x, min_y, max_z},
        {max_x, max_y, max_z}, {min_x, max_y, max_z}
    };
    m.meshes.push_back(mesh);
    return m;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "   EMPIRICAL CHALLENGER ADVERSARIAL STRESS TEST: detect_is_z_up  \n";
    std::cout << "=================================================================\n";

    // -------------------------------------------------------------
    // 1. Extreme Aspect Ratios
    // -------------------------------------------------------------
    {
        // 1.1 Vertical needle grounded at Z=0, centered Y
        auto m = make_mesh_box(-0.01f, 0.01f, -0.01f, 0.01f, 0.0f, 1000.0f);
        record("Aspect Ratio", "VerticalNeedle_CenteredY", grn::detect_is_z_up(m), "Z in [0, 1000], Y in [-0.01, 0.01]");

        // 1.2 Vertical needle grounded at Z=0, offset Y in positive quadrant
        auto m2 = make_mesh_box(-0.01f, 0.01f, 5.0f, 5.02f, 0.0f, 1000.0f);
        record("Aspect Ratio", "VerticalNeedle_PositiveY", grn::detect_is_z_up(m2), "Z in [0, 1000], Y in [5.0, 5.02]");

        // 1.3 Horizontal needle along Y, centered at origin
        auto m3 = make_mesh_box(-0.01f, 0.01f, -500.0f, 500.0f, -0.01f, 0.01f);
        record("Aspect Ratio", "HorizontalNeedle_CenteredY", grn::detect_is_z_up(m3), "Y in [-500, 500], Z in [-0.01, 0.01]");

        // 1.4 Horizontal needle along Y, in positive Y quadrant (like a weapon/sword)
        auto m4 = make_mesh_box(-0.01f, 0.01f, 0.0f, 1000.0f, -0.01f, 0.01f);
        record("Aspect Ratio", "HorizontalNeedle_PositiveY", grn::detect_is_z_up(m4), "Y in [0, 1000], Z in [-0.01, 0.01]");

        // 1.5 Flat pancake XY with sub-threshold thickness (span_z = 0.005 < 0.01)
        auto m5 = make_mesh_box(-100.0f, 100.0f, -100.0f, 100.0f, 0.0f, 0.005f);
        record("Aspect Ratio", "FlatPancake_SubThresholdZ", grn::detect_is_z_up(m5), "XY in [-100, 100], Z span = 0.005 < 0.01");

        // 1.6 Flat pancake XZ with sub-threshold thickness (span_y = 0.005 < 0.01)
        auto m6 = make_mesh_box(-100.0f, 100.0f, 0.0f, 0.005f, 0.0f, 100.0f);
        record("Aspect Ratio", "FlatPancake_SubThresholdY", grn::detect_is_z_up(m6), "XZ in [0, 100], Y span = 0.005 < 0.01");

        // 1.7 Tiny submillimeter object (all spans < 0.01)
        auto m7 = make_mesh_box(0.0f, 0.005f, 0.0f, 0.005f, 0.0f, 0.005f);
        record("Aspect Ratio", "SubmillimeterCube", grn::detect_is_z_up(m7), "All spans = 0.005 < 0.01");
    }

    // -------------------------------------------------------------
    // 2. Degenerate Meshes
    // -------------------------------------------------------------
    {
        // 2.1 Completely empty
        grn::GrnModel m1;
        record("Degenerate", "EmptyModel", grn::detect_is_z_up(m1), "0 verts, 0 bones");

        // 2.2 Single vertex at origin
        grn::GrnModel m2;
        grn::GrnMesh mesh2;
        mesh2.vertices = {{0.f, 0.f, 0.f}};
        m2.meshes.push_back(mesh2);
        record("Degenerate", "SingleVertex_Origin", grn::detect_is_z_up(m2), "1 vert at (0,0,0)");

        // 2.3 Single vertex offset
        grn::GrnModel m3;
        grn::GrnMesh mesh3;
        mesh3.vertices = {{500.f, 500.f, 500.f}};
        m3.meshes.push_back(mesh3);
        record("Degenerate", "SingleVertex_Offset", grn::detect_is_z_up(m3), "1 vert at (500,500,500)");

        // 2.4 Multiple coincident vertices
        grn::GrnModel m4;
        grn::GrnMesh mesh4;
        mesh4.vertices.assign(50, {10.f, 20.f, 30.f});
        m4.meshes.push_back(mesh4);
        record("Degenerate", "CoincidentVertices", grn::detect_is_z_up(m4), "50 verts at (10,20,30)");

        // 2.5 Collinear along X axis
        grn::GrnModel m5;
        grn::GrnMesh mesh5;
        mesh5.vertices = {{0.f, 0.f, 0.f}, {50.f, 0.f, 0.f}, {100.f, 0.f, 0.f}};
        m5.meshes.push_back(mesh5);
        record("Degenerate", "CollinearX", grn::detect_is_z_up(m5), "span_x = 100, span_y=0, span_z=0");

        // 2.6 NaN vertex
        grn::GrnModel m6;
        grn::GrnMesh mesh6;
        mesh6.vertices = {{0.f, 0.f, 0.f}, {10.f, std::numeric_limits<float>::quiet_NaN(), 20.f}};
        m6.meshes.push_back(mesh6);
        record("Degenerate", "NaN_Vertex", grn::detect_is_z_up(m6), "Contains NaN coord");

        // 2.7 Inf vertex
        grn::GrnModel m7;
        grn::GrnMesh mesh7;
        mesh7.vertices = {{0.f, 0.f, 0.f}, {0.f, std::numeric_limits<float>::infinity(), 0.f}};
        m7.meshes.push_back(mesh7);
        record("Degenerate", "Infinity_Vertex", grn::detect_is_z_up(m7), "Contains +Inf coord");
    }

    // -------------------------------------------------------------
    // 3. Inverted Models (Upside Down)
    // -------------------------------------------------------------
    {
        // 3.1 Upside down biped, centered Y: Z in [-180, 0], Y in [-10, 10]
        auto m1 = make_mesh_box(-20.f, 20.f, -10.f, 10.f, -180.f, 0.0f);
        record("Inverted", "UpsideDown_Biped_CenteredY", grn::detect_is_z_up(m1), "Z in [-180, 0], Y in [-10, 10]");

        // 3.2 Upside down biped, positive Y: Z in [-180, 0], Y in [10, 30]
        auto m2 = make_mesh_box(-20.f, 20.f, 10.f, 30.f, -180.f, 0.0f);
        record("Inverted", "UpsideDown_Biped_PositiveY", grn::detect_is_z_up(m2), "Z in [-180, 0], Y in [10, 30]");

        // 3.3 Upside down quadruped, Z in [-60, 0], Y in [-50, 10]
        auto m3 = make_mesh_box(-15.f, 15.f, -50.f, 10.f, -60.f, 0.0f);
        record("Inverted", "UpsideDown_Quadruped_Z", grn::detect_is_z_up(m3), "Z in [-60, 0], Y in [-50, 10]");

        // 3.4 glTF biped upside down: Y in [-180, 0], Z in [-10, 10]
        auto m4 = make_mesh_box(-20.f, 20.f, -180.f, 0.0f, -10.f, 10.f);
        record("Inverted", "glTF_Biped_UpsideDown", grn::detect_is_z_up(m4), "Y in [-180, 0], Z in [-10, 10]");
    }

    // -------------------------------------------------------------
    // 4. Highly Offset Roots & Floating Objects
    // -------------------------------------------------------------
    {
        // 4.1 Floating bird, centered Y: Z in [100, 130], Y in [-15, 15]
        auto m1 = make_mesh_box(-20.f, 20.f, -15.f, 15.f, 100.f, 130.f);
        record("Floating/Offset", "Floating_Bird_CenteredY", grn::detect_is_z_up(m1), "Z in [100, 130], Y in [-15, 15]");

        // 4.2 Floating bird, offset positive Y: Z in [100, 150] (span 50), Y in [100, 130] (span 30)
        auto m2 = make_mesh_box(-20.f, 20.f, 100.f, 130.f, 100.f, 150.f);
        record("Floating/Offset", "Floating_Bird_PositiveY_Tall", grn::detect_is_z_up(m2), "Z in [100, 150], Y in [100, 130]");

        // 4.3 High altitude object, positive quadrant
        auto m3 = make_mesh_box(0.f, 10.f, 5000.f, 5020.f, 10000.f, 10100.f);
        record("Floating/Offset", "HighAltitude_PositiveQuadrant", grn::detect_is_z_up(m3), "Z in [10000, 10100], Y in [5000, 5020]");

        // 4.4 Model with root bone at origin (0,0,0) and mesh floating high
        grn::GrnModel m4 = make_mesh_box(-15.f, 15.f, -15.f, 15.f, 100.f, 150.f);
        grn::GrnBone root_b;
        root_b.name = "Root";
        root_b.position = {0.f, 0.f, 0.f};
        m4.bones.push_back(root_b);
        record("Floating/Offset", "MeshHigh_RootAtOrigin", grn::detect_is_z_up(m4), "Mesh Z in [100, 150], Root bone at (0,0,0)");
    }

    std::cout << "\n=================================================================\n";
    std::cout << "   STRESS TEST COMPLETE: " << results.size() << " cases evaluated\n";
    std::cout << "=================================================================\n";

    return 0;
}
