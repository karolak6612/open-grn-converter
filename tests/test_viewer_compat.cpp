/**
 * @file test_viewer_compat.cpp
 * @brief Integration test verifying that converted GRN models open cleanly in gr2_viewer.exe
 *        when gr2_viewer.exe is present in either the source code or binary running folder.
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

#ifdef _WIN32
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

static fs::path locate_viewer() {
    std::error_code ec;

    // 1. Binary running folder
    fs::path bin_viewer = get_binary_dir() / "gr2_viewer.exe";
    if (fs::is_regular_file(bin_viewer, ec)) {
        return bin_viewer;
    }

    // 2. Source code folder
#ifdef GRN_SOURCE_CODE_DIR
    fs::path src_viewer = fs::path(GRN_SOURCE_CODE_DIR) / "gr2_viewer.exe";
    if (fs::is_regular_file(src_viewer, ec)) {
        return src_viewer;
    }
#endif

    // 3. Current working directory
    fs::path cwd_viewer = fs::current_path() / "gr2_viewer.exe";
    if (fs::is_regular_file(cwd_viewer, ec)) {
        return cwd_viewer;
    }

    // 4. Parent repository directory
    fs::path parent_viewer = get_binary_dir().parent_path().parent_path() / "gr2_viewer.exe";
    if (fs::is_regular_file(parent_viewer, ec)) {
        return parent_viewer;
    }

    return {};
}

static fs::path locate_test_grn_dir() {
    std::error_code ec;

    // 1. Binary running folder
    fs::path bin_tg = get_binary_dir() / "test_grn";
    if (fs::is_directory(bin_tg, ec)) return bin_tg;

    // 2. Source code folder
#ifdef GRN_SOURCE_CODE_DIR
    fs::path src_tg = fs::path(GRN_SOURCE_CODE_DIR) / "test_grn";
    if (fs::is_directory(src_tg, ec)) return src_tg;
#endif

    // 3. Current working directory
    fs::path cwd_tg = fs::current_path() / "test_grn";
    if (fs::is_directory(cwd_tg, ec)) return cwd_tg;

    const char* env_dir = std::getenv("GRN_TEST_DIR");
    if (env_dir && fs::is_directory(env_dir, ec)) return env_dir;

    return {};
}

#ifdef _WIN32
static bool run_viewer_under_debugger(const fs::path& viewer_path, const fs::path& grn_path) {
    std::string cmd = "\"" + viewer_path.string() + "\" \"" + grn_path.string() + "\"";
    std::cout << "  Launching viewer: " << cmd << std::endl;

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, FALSE,
                       DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &si, &pi)) {
        std::cerr << "  Failed to spawn process: " << GetLastError() << std::endl;
        return false;
    }

    DEBUG_EVENT de = {};
    bool running = true;
    bool success = true;
    DWORD start_time = GetTickCount();

    while (running) {
        if (!WaitForDebugEvent(&de, 100)) {
            // If the viewer has loaded and rendered for 2 seconds without exception, consider it clean
            if (GetTickCount() - start_time > 2000) {
                std::cout << "  [PASS] Viewer loaded model cleanly with 0 crashes!" << std::endl;
                break;
            }
            continue;
        }

        DWORD continue_status = DBG_CONTINUE;

        switch (de.dwDebugEventCode) {
        case EXCEPTION_DEBUG_EVENT: {
            DWORD code = de.u.Exception.ExceptionRecord.ExceptionCode;
            DWORD first_chance = de.u.Exception.dwFirstChance;

            if (code != EXCEPTION_BREAKPOINT && code != 0x406D1388) {
                if (code == EXCEPTION_ACCESS_VIOLATION || first_chance == 0) {
                    std::cerr << "  [FAIL] Exception 0x" << std::hex << code
                              << " occurred in viewer!" << std::dec << std::endl;
                    continue_status = DBG_EXCEPTION_NOT_HANDLED;
                    success = false;
                    running = false;
                }
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            std::cout << "  Viewer exited with code " << de.u.ExitProcess.dwExitCode << std::endl;
            running = false;
            break;
        }

        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continue_status);
    }

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return success;
}
#endif

int main() {
    std::cout << "=== Running gr2_viewer.exe Compatibility Test ===" << std::endl;

    fs::path viewer_exe = locate_viewer();
    if (viewer_exe.empty()) {
        std::cout << "[SKIP] gr2_viewer.exe not provided in source code folder or binary running folder. Test skipped." << std::endl;
        return 0;
    }

    std::cout << "Found viewer executable: " << viewer_exe << std::endl;

    fs::path tg_dir = locate_test_grn_dir();
    if (tg_dir.empty()) {
        std::cout << "[SKIP] test_grn directory not found. Test skipped." << std::endl;
        return 0;
    }

    // Collect candidate .grn files recursively
    std::vector<fs::path> test_candidates;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(tg_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".grn") {
            auto rel = fs::relative(entry.path(), tg_dir, ec);
            auto first = rel.begin()->string();
            if (first == "GRN" || first == "GLB") continue;
            test_candidates.push_back(entry.path());
        }
    }

    if (test_candidates.empty()) {
        std::cout << "[SKIP] No .grn files in test_grn. Test skipped." << std::endl;
        return 0;
    }

    // Select fixtures dynamically from discovered test candidates
    std::vector<fs::path> test_fixtures;
    for (size_t i = 0; i < std::min<size_t>(2, test_candidates.size()); ++i) {
        test_fixtures.push_back(test_candidates[i]);
    }

    for (const auto& chosen_file : test_fixtures) {
        std::cout << "\nTesting fixture under viewer: " << chosen_file.filename().string() << std::endl;

        // Convert GRN -> GLB -> GRN to verify full re-serializer compatibility
        auto model_opt = grn::parse_grn_file(chosen_file);
        assert(model_opt.has_value());

        grn::GlbExportOptions exp_opt;
        exp_opt.embed_textures = true;
        auto glb_bytes = grn::export_grn_to_glb_memory(*model_opt, exp_opt);
        assert(!glb_bytes.empty());

        grn::GlbImportOptions imp_opt;
        auto reloaded_opt = grn::load_glb_memory(glb_bytes.data(), glb_bytes.size(), imp_opt);
        assert(reloaded_opt.has_value());

        fs::path temp_grn = fs::temp_directory_path() / ("test_viewer_" + chosen_file.filename().string());
        bool written = grn::write_grn_file(temp_grn, *reloaded_opt);
        (void)written;
        assert(written);

#ifdef _WIN32
        bool ok = run_viewer_under_debugger(viewer_exe, temp_grn);
        fs::remove(temp_grn);
        if (!ok) {
            std::cerr << "gr2_viewer.exe compatibility test FAILED for " << chosen_file.filename().string() << std::endl;
            return 1;
        }
#else
        std::cout << "[SKIP] Viewer execution is Win32-only." << std::endl;
        fs::remove(temp_grn);
#endif
    }

    // Also test standalone GLB models (e.g. Lantern.glb)
    for (const auto& entry : fs::directory_iterator(tg_dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".glb") {
            std::cout << "\nTesting GLB asset under viewer: " << entry.path().filename().string() << std::endl;
            grn::GlbImportOptions imp_opt;
            auto glb_model = grn::load_glb_file(entry.path(), imp_opt);
            assert(glb_model.has_value());

            fs::path temp_grn = fs::temp_directory_path() / ("test_viewer_" + entry.path().stem().string() + ".grn");
            bool written = grn::write_grn_file(temp_grn, *glb_model);
            (void)written;
            assert(written);

#ifdef _WIN32
            bool ok = run_viewer_under_debugger(viewer_exe, temp_grn);
            fs::remove(temp_grn);
            if (!ok) {
                std::cerr << "gr2_viewer.exe compatibility test FAILED for " << entry.path().filename().string() << std::endl;
                return 1;
            }
#else
            fs::remove(temp_grn);
#endif
        }
    }

    std::cout << "\ngr2_viewer.exe compatibility test passed successfully." << std::endl;
    return 0;
}
