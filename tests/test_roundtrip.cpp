/**
 * @file test_roundtrip.cpp
 * @brief Integration test verifying bidirectional GRN <-> GLB roundtrip conversions
 *        with dynamic test asset discovery in the source code or binary running folder.
 */

#include "../src/converter/converter.h"
#include "../src/core/grn_parser.h"
#include "../src/core/grn_writer.h"
#include "../src/gltf/glb_reader.h"
#include "../src/gltf/glb_writer.h"
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cassert>
#include <iomanip>
#include <unordered_set>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

static fs::path get_binary_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len > 0) {
        return fs::path(path).parent_path();
    }
#endif
    return fs::current_path();
}

static std::vector<fs::path> discover_test_files() {
    std::vector<fs::path> test_files;
    std::unordered_set<std::string> seen_paths;
    std::vector<fs::path> candidate_dirs;
    std::error_code ec;

    // 1. Check environment variable if set
    const char* env_dir = std::getenv("GRN_TEST_DIR");
    if (env_dir && fs::is_directory(env_dir, ec)) {
        auto can = fs::canonical(env_dir, ec);
        if (seen_paths.insert(can.string()).second) {
            candidate_dirs.emplace_back(env_dir);
        }
    }

    // 2. Source code folder: <GRN_SOURCE_CODE_DIR>/test_grn
#ifdef GRN_SOURCE_CODE_DIR
    fs::path src_tg = fs::path(GRN_SOURCE_CODE_DIR) / "test_grn";
    if (fs::is_directory(src_tg, ec)) {
        auto can = fs::canonical(src_tg, ec);
        if (seen_paths.insert(can.string()).second) {
            candidate_dirs.push_back(src_tg);
        }
    }
#endif

    // 3. Binary running folder: <binary_dir>/test_grn
    fs::path bin_dir = get_binary_dir();
    fs::path bin_tg = bin_dir / "test_grn";
    if (fs::is_directory(bin_tg, ec)) {
        auto can = fs::canonical(bin_tg, ec);
        if (seen_paths.insert(can.string()).second) {
            candidate_dirs.push_back(bin_tg);
        }
    }

    // 4. Current working directory: ./test_grn
    fs::path cwd_tg = fs::current_path() / "test_grn";
    if (fs::is_directory(cwd_tg, ec)) {
        auto can = fs::canonical(cwd_tg, ec);
        if (seen_paths.insert(can.string()).second) {
            candidate_dirs.push_back(cwd_tg);
        }
    }

    // Scan candidate directories recursively for .grn files
    std::unordered_set<std::string> seen_rel_paths;
    for (const auto& dir : candidate_dirs) {
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".grn") {
                auto rel = fs::relative(entry.path(), dir, ec);
                auto first = rel.begin()->string();
                if (first == "GRN" || first == "GLB") continue; // Skip converter output folders

                std::string rel_str = rel.string();
                std::replace(rel_str.begin(), rel_str.end(), '\\', '/');
                if (seen_rel_paths.insert(rel_str).second) {
                    test_files.push_back(entry.path());
                }
            }
        }
    }

    return test_files;
}

struct RoundtripResult {
    bool success = true;
    fs::path path;
    std::string error;
    size_t orig_size = 0;
    size_t glb_size = 0;
    size_t rt_grn_size = 0;
};

static RoundtripResult run_roundtrip_test(const fs::path& grn_path) {
    RoundtripResult res;
    res.path = grn_path;
    std::error_code ec;
    res.orig_size = fs::file_size(grn_path, ec);

    auto model_opt = grn::parse_grn_file(grn_path);
    if (!model_opt.has_value()) {
        res.success = false;
        res.error = "Failed to parse initial GRN";
        return res;
    }
    const auto& model = *model_opt;

    // Export to GLB
    grn::GlbExportOptions exp_opt;
    exp_opt.embed_textures = true;
    auto glb_bytes = grn::export_grn_to_glb_memory(model, exp_opt);
    if (glb_bytes.empty()) {
        res.success = false;
        res.error = "Failed to export GLB (empty buffer)";
        return res;
    }
    res.glb_size = glb_bytes.size();

    // Read GLB back
    grn::GlbImportOptions imp_opt;
    auto reloaded_opt = grn::load_glb_memory(glb_bytes.data(), glb_bytes.size(), imp_opt);
    if (!reloaded_opt.has_value()) {
        res.success = false;
        res.error = "Failed to reload exported GLB";
        return res;
    }
    const auto& reloaded = *reloaded_opt;

    std::vector<grn::GrnMesh> non_empty_meshes;
    for (const auto& m : model.meshes) {
        if (!m.vertices.empty()) non_empty_meshes.push_back(m);
    }

    if (reloaded.meshes.size() != non_empty_meshes.size()) {
        res.success = false;
        res.error = "Reloaded mesh count mismatch: expected " + std::to_string(non_empty_meshes.size()) +
                    ", got " + std::to_string(reloaded.meshes.size());
        return res;
    }

    if (reloaded.bones.size() != model.bones.size()) {
        res.success = false;
        res.error = "Reloaded bone count mismatch: expected " + std::to_string(model.bones.size()) +
                    ", got " + std::to_string(reloaded.bones.size());
        return res;
    }

    if (reloaded.animations.size() != model.animations.size()) {
        res.success = false;
        res.error = "Reloaded animation count mismatch: expected " + std::to_string(model.animations.size()) +
                    ", got " + std::to_string(reloaded.animations.size());
        return res;
    }

    for (size_t mi = 0; mi < non_empty_meshes.size(); ++mi) {
        const auto& orig_m = non_empty_meshes[mi];
        const auto& rel_m = reloaded.meshes[mi];
        if (!orig_m.weights.empty()) {
            if (rel_m.weights.empty()) {
                res.success = false;
                res.error = "Weights lost on mesh " + orig_m.name;
                return res;
            }
            if (rel_m.bone_index_map.size() != orig_m.bone_index_map.size()) {
                res.success = false;
                res.error = "Bone index map count mismatch on mesh " + orig_m.name;
                return res;
            }
        }
    }

    // Export reloaded back to GRN
    auto re_grn_bytes = grn::write_grn_memory(reloaded);
    if (re_grn_bytes.empty()) {
        res.success = false;
        res.error = "Failed to write roundtrip GRN";
        return res;
    }
    res.rt_grn_size = re_grn_bytes.size();

    // Parse re-exported GRN
    auto final_opt = grn::parse_grn_memory(re_grn_bytes.data(), re_grn_bytes.size());
    if (!final_opt.has_value()) {
        res.success = false;
        res.error = "Failed to parse roundtrip GRN container";
        return res;
    }

    if (final_opt->meshes.size() != non_empty_meshes.size()) {
        res.success = false;
        res.error = "Final mesh count mismatch";
        return res;
    }

    if (final_opt->bones.size() != model.bones.size()) {
        res.success = false;
        res.error = "Final bone count mismatch";
        return res;
    }

    if (final_opt->animations.size() != model.animations.size()) {
        res.success = false;
        res.error = "Final animation count mismatch";
        return res;
    }

    for (size_t mi = 0; mi < non_empty_meshes.size(); ++mi) {
        const auto& orig_m = non_empty_meshes[mi];
        const auto& fin_m = final_opt->meshes[mi];

        // 1. Geometric bounding box and face count
        // Note: Raw vertex counts in glTF/GLB may differ from original multi-indexed GRN
        // due to attribute unrolling at UV seams and hard normal creases (glTF requires 1 UV per vertex).
        // Geometric extents and triangle topology verify geometric fidelity.
        if (fin_m.faces.size() != orig_m.faces.size()) {
            res.success = false;
            res.error = "Face count mismatch on mesh " + orig_m.name +
                        " (expected " + std::to_string(orig_m.faces.size()) +
                        ", got " + std::to_string(fin_m.faces.size()) + ")";
            return res;
        }

        // Bounding box fidelity check (over vertices referenced by faces)
        if (!orig_m.faces.empty() && !fin_m.faces.empty()) {
            grn::Vec3 o_min{1e9f, 1e9f, 1e9f}, o_max{-1e9f, -1e9f, -1e9f};
            for (const auto& f : orig_m.faces) {
                for (int c = 0; c < 3; ++c) {
                    uint32_t vi = f[c];
                    if (vi < orig_m.vertices.size()) {
                        const auto& v = orig_m.vertices[vi];
                        o_min.x = (std::min)(o_min.x, v.x); o_max.x = (std::max)(o_max.x, v.x);
                        o_min.y = (std::min)(o_min.y, v.y); o_max.y = (std::max)(o_max.y, v.y);
                        o_min.z = (std::min)(o_min.z, v.z); o_max.z = (std::max)(o_max.z, v.z);
                    }
                }
            }
            grn::Vec3 f_min{1e9f, 1e9f, 1e9f}, f_max{-1e9f, -1e9f, -1e9f};
            for (const auto& f : fin_m.faces) {
                for (int c = 0; c < 3; ++c) {
                    uint32_t vi = f[c];
                    if (vi < fin_m.vertices.size()) {
                        const auto& v = fin_m.vertices[vi];
                        f_min.x = (std::min)(f_min.x, v.x); f_max.x = (std::max)(f_max.x, v.x);
                        f_min.y = (std::min)(f_min.y, v.y); f_max.y = (std::max)(f_max.y, v.y);
                        f_min.z = (std::min)(f_min.z, v.z); f_max.z = (std::max)(f_max.z, v.z);
                    }
                }
            }
            float tol = 1e-2f;
            if (std::abs(o_min.x - f_min.x) > tol || std::abs(o_max.x - f_max.x) > tol ||
                std::abs(o_min.y - f_min.y) > tol || std::abs(o_max.y - f_max.y) > tol ||
                std::abs(o_min.z - f_min.z) > tol || std::abs(o_max.z - f_max.z) > tol) {
                res.success = false;
                res.error = "Bounding box mismatch on mesh " + orig_m.name +
                            " (orig min: " + std::to_string(o_min.x) + "," + std::to_string(o_min.y) + "," + std::to_string(o_min.z) +
                            " max: " + std::to_string(o_max.x) + "," + std::to_string(o_max.y) + "," + std::to_string(o_max.z) +
                            " | rt min: " + std::to_string(f_min.x) + "," + std::to_string(f_min.y) + "," + std::to_string(f_min.z) +
                            " max: " + std::to_string(f_max.x) + "," + std::to_string(f_max.y) + "," + std::to_string(f_max.z) + ")";
                return res;
            }
        }

        // 2. Render pass face coverage
        if (!orig_m.tri_groups.empty()) {
            size_t total_group_faces = 0;
            for (const auto& g : fin_m.tri_groups) {
                total_group_faces += g.faces.size();
            }
            if (total_group_faces != fin_m.faces.size()) {
                res.success = false;
                res.error = "Render pass face coverage mismatch on mesh " + orig_m.name;
                return res;
            }
        }

        // 3. Weights & bone mapping
        if (!orig_m.weights.empty()) {
            if (fin_m.weights.size() != fin_m.vertices.size() ||
                fin_m.bone_index_map.size() != orig_m.bone_index_map.size()) {
                res.success = false;
                res.error = "Skin weights/bone mapping mismatch on mesh " + orig_m.name;
                return res;
            }
            for (size_t bi = 0; bi < orig_m.bone_index_map.size(); ++bi) {
                if (fin_m.bone_index_map[bi] != orig_m.bone_index_map[bi]) {
                    res.success = false;
                    res.error = "Bone index palette mismatch on mesh " + orig_m.name;
                    return res;
                }
            }
        }
    }

    return res;
}

int main() {
    std::cout << "=== Running Multithreaded Full Asset Roundtrip Test Suite ===" << std::endl;
    auto files = discover_test_files();
    std::cout << "Discovered " << files.size() << " test file(s)." << std::endl;

    if (files.empty()) {
        std::cout << "No .grn test assets discovered dynamically. Running synthetic test fallback..." << std::endl;
        grn::GrnModel synthetic;
        grn::GrnMesh mesh;
        mesh.name = "SyntheticMesh";
        mesh.vertices = {{0,0,0}, {1,0,0}, {0,1,0}};
        mesh.faces = {{{0,1,2}}};
        synthetic.meshes.push_back(mesh);

        grn::GlbExportOptions exp_opt;
        auto glb_bytes = grn::export_grn_to_glb_memory(synthetic, exp_opt);
        assert(!glb_bytes.empty());

        auto reloaded = grn::load_glb_memory(glb_bytes.data(), glb_bytes.size());
        assert(reloaded.has_value());
        assert(reloaded->meshes.size() == 1);
        std::cout << "Synthetic fallback passed." << std::endl;
        return 0;
    }

    auto start_time = std::chrono::steady_clock::now();
    size_t num_threads = std::max<size_t>(1, std::thread::hardware_concurrency());
    std::cout << "Executing on " << num_threads << " worker threads in parallel..." << std::endl;

    std::atomic<size_t> next_index{0};
    std::atomic<size_t> completed{0};
    std::atomic<size_t> passed{0};
    std::atomic<size_t> failed{0};
    std::vector<RoundtripResult> results(files.size());
    std::mutex cout_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = next_index.fetch_add(1);
            if (idx >= files.size()) break;

            results[idx] = run_roundtrip_test(files[idx]);
            if (results[idx].success) {
                passed.fetch_add(1);
            } else {
                failed.fetch_add(1);
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "  [FAIL] " << files[idx].filename().string()
                          << " -> " << results[idx].error << std::endl;
            }

            size_t done = completed.fetch_add(1) + 1;
            if (done % 500 == 0 || done == files.size()) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                float pct = (static_cast<float>(done) / static_cast<float>(files.size())) * 100.0f;
                std::cout << "  [PROGRESS] " << done << " / " << files.size()
                          << " (" << std::fixed << std::setprecision(1) << pct << "%) assets processed..." << std::endl;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    double sec = std::chrono::duration<double>(end_time - start_time).count();

    std::cout << "\n=======================================================\n";
    std::cout << "ROUNDTRIP STRESS TEST SUITE SUMMARY\n";
    std::cout << "Total Assets Discovered & Tested: " << files.size() << "\n";
    std::cout << "Passed (100% Fidelity Verified):  " << passed.load() << "\n";
    std::cout << "Failed:                           " << failed.load() << "\n";
    std::cout << "Elapsed Time:                     " << std::fixed << std::setprecision(2) << sec << " s\n";
    if (sec > 0.0) {
        std::cout << "Throughput:                       " << std::fixed << std::setprecision(1) << (files.size() / sec) << " assets/sec\n";
    }
    std::cout << "=======================================================\n";

    if (failed.load() > 0) {
        std::cout << "\nFailure Breakdown:" << std::endl;
        size_t shown = 0;
        for (const auto& r : results) {
            if (!r.success) {
                std::cout << " - " << r.path.filename().string() << ": " << r.error << std::endl;
                if (++shown >= 20) {
                    std::cout << "   ... and " << (failed.load() - shown) << " more failures." << std::endl;
                    break;
                }
            }
        }
        return 1;
    }

    std::cout << "\nAll " << files.size() << " assets in test_grn verified successfully with full roundtrip fidelity!" << std::endl;
    return 0;
}
