/**
 * @file converter.cpp
 * @brief Implementation of high-level conversion pipeline orchestrator.
 */

#include "converter.h"
#include "../core/grn_parser.h"
#include "../core/grn_writer.h"
#include "../gltf/glb_writer.h"
#include "../gltf/glb_reader.h"
#include "../codecs/vtex_codec.h"
#include "../codecs/tga_png.h"
#include "mesh_optimizer.h"
#include "anim_optimizer.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_set>

namespace grn {

static void resolve_model_textures(GrnModel& model, const std::filesystem::path& model_path, const ConversionOptions& options) {
    std::vector<std::filesystem::path> search_dirs;
    std::error_code ec;

    if (!options.textures_dir.empty() && std::filesystem::is_directory(options.textures_dir, ec)) {
        search_dirs.push_back(options.textures_dir);
    }

    auto model_dir = std::filesystem::absolute(model_path).parent_path();
    if (std::filesystem::is_directory(model_dir, ec)) {
        search_dirs.push_back(model_dir);
        for (const auto& sub : {"textures", "Textures", "TEXTURES", "maps", "Maps", "MAPS"}) {
            auto sub_path = model_dir / sub;
            if (std::filesystem::is_directory(sub_path, ec)) {
                search_dirs.push_back(sub_path);
            }
        }
    }

    for (auto& tex : model.textures) {
        // Only resolve if texture has no embedded pixel data or is flagged as placeholder
        if (!tex.decoded_rgba.empty() && !tex.is_placeholder) continue;

        std::vector<std::string> names_to_check;
        if (!tex.file_name.empty()) names_to_check.push_back(tex.file_name);
        if (!tex.name.empty() && tex.name != tex.file_name) names_to_check.push_back(tex.name);

        bool found = false;
        for (const auto& dir : search_dirs) {
            for (const auto& fname : names_to_check) {
                // Direct file check
                auto p = dir / fname;
                if (std::filesystem::is_regular_file(p, ec)) {
                    auto img = load_image_file(p);
                    if (img && !img->pixels.empty()) {
                        tex.decoded_rgba = std::move(img->pixels);
                        tex.width = img->width;
                        tex.height = img->height;
                        tex.has_alpha = img->has_alpha;
                        tex.is_placeholder = false;
                        found = true;
                        break;
                    }
                }

                // Check with common texture extensions if fname has no extension or different extension
                std::string stem = std::filesystem::path(fname).stem().string();
                for (const auto& ext : {".png", ".tga", ".bmp", ".jpg", ".jpeg", ".PNG", ".TGA"}) {
                    auto p_ext = dir / (stem + ext);
                    if (std::filesystem::is_regular_file(p_ext, ec)) {
                        auto img = load_image_file(p_ext);
                        if (img && !img->pixels.empty()) {
                            tex.decoded_rgba = std::move(img->pixels);
                            tex.width = img->width;
                            tex.height = img->height;
                            tex.has_alpha = img->has_alpha;
                            tex.is_placeholder = false;
                            found = true;
                            break;
                        }
                    }
                }
                if (found) break;
            }
            if (found) break;
        }
    }
}

static void apply_pink_hue_filter(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height) {
    for (size_t i = 0; i < width * height; ++i) {
        float r = static_cast<float>(rgba[i * 4 + 0]);
        float g = static_cast<float>(rgba[i * 4 + 1]);
        float b = static_cast<float>(rgba[i * 4 + 2]);

        r = std::clamp(r * 1.3f + 40.0f, 0.0f, 255.0f);
        g = std::clamp(g * 0.55f, 0.0f, 255.0f);
        b = std::clamp(b * 1.2f + 30.0f, 0.0f, 255.0f);

        rgba[i * 4 + 0] = static_cast<uint8_t>(r);
        rgba[i * 4 + 1] = static_cast<uint8_t>(g);
        rgba[i * 4 + 2] = static_cast<uint8_t>(b);
    }
}

bool convert_file(const std::filesystem::path& input,
                  const std::filesystem::path& output,
                  const ConversionOptions& options,
                  ProgressCallback callback) {
    if (!std::filesystem::exists(input)) {
        if (callback) callback(input.string(), 0.0f, false, "Input file does not exist: " + input.string());
        return false;
    }

    std::string in_ext = input.extension().string();
    std::transform(in_ext.begin(), in_ext.end(), in_ext.begin(), ::tolower);

    ConversionDirection dir = options.direction;
    if (dir == ConversionDirection::Auto) {
        if (in_ext == ".grn") dir = ConversionDirection::GrnToGlb;
        else if (in_ext == ".glb" || in_ext == ".gltf") dir = ConversionDirection::GlbToGrn;
        else {
            if (callback) callback(input.string(), 0.0f, false, "Unrecognized extension: " + in_ext);
            return false;
        }
    }

    std::filesystem::path out_path = output;
    if (out_path.empty()) {
        out_path = input;
        out_path.replace_extension(dir == ConversionDirection::GrnToGlb ? ".glb" : ".grn");
    }

    if (dir == ConversionDirection::GrnToGlb) {
        if (callback) callback(input.filename().string(), 0.1f, true, "Parsing GRN container: " + input.string());

        auto model = parse_grn_file(input);
        if (!model) {
            if (callback) callback(input.filename().string(), 0.0f, false, "Failed to parse GRN file");
            return false;
        }

        // Collect all animation files to merge (excluding the base model input file)
        std::vector<std::filesystem::path> anim_list;
        for (const auto& a : options.anim_files) {
            if (!a.empty() && a != input && std::find(anim_list.begin(), anim_list.end(), a) == anim_list.end()) {
                anim_list.push_back(a);
            }
        }
        if (!options.anim_file.empty() && options.anim_file != input && std::find(anim_list.begin(), anim_list.end(), options.anim_file) == anim_list.end()) {
            anim_list.push_back(options.anim_file);
        }

        if (!anim_list.empty()) {
            // Remove empty placeholder animations (0 tracks) if adding real animations
            model->animations.erase(
                std::remove_if(model->animations.begin(), model->animations.end(),
                               [](const GrnAnimation& a) { return a.tracks.empty(); }),
                model->animations.end());

            for (const auto& apath : anim_list) {
                if (!std::filesystem::exists(apath)) {
                    if (callback) callback(apath.filename().string(), 0.2f, false, "Animation file not found: " + apath.string());
                    continue;
                }
                auto anim_model = parse_grn_file(apath);
                if (!anim_model || anim_model->animations.empty()) {
                    if (callback) callback(apath.filename().string(), 0.2f, false, "Failed to parse animation file: " + apath.string());
                    continue;
                }
                for (auto& a : anim_model->animations) {
                    if (a.name.empty() || a.name == "Animation") {
                        a.name = apath.stem().string();
                    }
                    if (callback) callback(apath.filename().string(), 0.3f, true,
                        "Integrating animation '" + a.name + "' (" + std::to_string(a.tracks.size()) + " tracks, " +
                        std::to_string(a.duration) + "s)");
                    // Granny 1.2b tracks directly map to model bone slots (no rebase needed)

                    // Ensure track bone names match target bone names if needed
                    for (auto& track : a.tracks) {
                        for (const auto& tb : model->bones) {
                            if (_stricmp(track.bone_name.c_str(), tb.name.c_str()) == 0) {
                                track.bone_name = tb.name;
                                break;
                            }
                        }
                    }
                    model->animations.push_back(std::move(a));
                }
            }
        }

        resolve_model_textures(*model, input, options);

        if (options.tint_pink) {
            for (auto& tex : model->textures) {
                if (!tex.decoded_rgba.empty()) {
                    apply_pink_hue_filter(tex.decoded_rgba, tex.width, tex.height);
                }
            }
        }

        GlbExportOptions exp_opt;
        exp_opt.y_up = options.y_up;
        exp_opt.embed_textures = options.embed_textures;
        exp_opt.loose_texture_dir = out_path.parent_path();
        exp_opt.loose_texture_format = options.texture_format;
        exp_opt.model_stem = out_path.stem().string();

        if (callback) callback(input.filename().string(), 0.6f, true, "Serializing glTF 2.0 / GLB: " + out_path.string());

        bool ok = export_grn_to_glb_file(out_path, *model, exp_opt);
        if (callback) {
            std::error_code ec;
            auto fsz = std::filesystem::file_size(out_path, ec);
            if (ok) callback(out_path.filename().string(), 1.0f, true, "Successfully converted GRN -> GLB (" + std::to_string(ec ? 0 : fsz) + " bytes)");
            else callback(out_path.filename().string(), 0.0f, false, "Failed writing GLB file");
        }
        return ok;
    } else {
        if (callback) callback(input.filename().string(), 0.1f, true, "Parsing GLB container: " + input.string());

        GlbImportOptions imp_opt;
        imp_opt.y_up = options.y_up;
        imp_opt.scale = options.scale;
        imp_opt.target_height = options.target_height;
        imp_opt.texture_dir = input.parent_path();
        imp_opt.prefer_vtex = options.vtex_enabled;

        auto model = load_glb_file(input, imp_opt);
        if (!model) {
            if (callback) callback(input.filename().string(), 0.0f, false, "Failed to parse GLB file");
            return false;
        }

        float min_x = 1e30f, max_x = -1e30f;
        float min_y = 1e30f, max_y = -1e30f;
        float min_z = 1e30f, max_z = -1e30f;
        for (const auto& m : model->meshes) {
            for (const auto& v : m.vertices) {
                min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
                min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
                min_z = std::min(min_z, v.z); max_z = std::max(max_z, v.z);
            }
        }
        if (!model->meshes.empty() && max_x >= min_x) {
            std::ostringstream ss;
            ss << "Dimensions: W=" << std::fixed << std::setprecision(2) << (max_x - min_x)
               << ", D=" << (max_y - min_y)
               << ", H=" << (max_z - min_z) << " units";
            if (callback) callback(input.filename().string(), 0.3f, true, ss.str());
        }

        for (auto& tex : model->textures) {
            if (options.tint_pink && !tex.decoded_rgba.empty()) {
                apply_pink_hue_filter(tex.decoded_rgba, tex.width, tex.height);
            }
            if (tex.raw_blob.empty() && !tex.decoded_rgba.empty()) {
                if (options.vtex_enabled) {
                    auto [fmt, vtex_data] = encode_vtex(tex.decoded_rgba.data(), tex.width, tex.height);
                    if (!vtex_data.empty()) {
                        tex.format_code = fmt;
                        tex.raw_blob = std::move(vtex_data);
                    }
                }
            }
        }

        // 16-bit safe mesh partitioning & optimization
        MeshOptimizerOptions opt_opts;
        opt_opts.auto_split_16bit = options.auto_split_16bit;
        opt_opts.max_vertices_per_part = options.max_vertices_16bit;
        opt_opts.decimate = options.optimize_vertices;
        opt_opts.max_total_vertices = options.decimate_target_verts;

        size_t orig_mesh_count = model->meshes.size();
        optimize_model_meshes(*model, opt_opts);

        if (model->meshes.size() > orig_mesh_count && callback) {
            callback(input.filename().string(), 0.5f, true,
                "Auto-partitioned high-poly mesh into " + std::to_string(model->meshes.size()) +
                " 16-bit safe sub-meshes (≤" + std::to_string(options.max_vertices_16bit) + " vertices/mesh)");
        } else if (!options.auto_split_16bit && callback) {
            for (const auto& m : model->meshes) {
                if (m.vertices.size() > 65535) {
                    callback(input.filename().string(), 0.5f, false,
                        "WARNING: Mesh '" + m.name + "' has " + std::to_string(m.vertices.size()) +
                        " vertices (>65,535). Mesh optimizer is OFF: this file will crash Granny 1.2b / Sacred Gold!");
                }
            }
        }

        // Optimize animations (prune static tracks, collapse constant keyframes, decimate redundant linear frames)
        if (options.optimize_animations && !model->animations.empty()) {
            AnimOptimizationOptions anim_opt;
            anim_opt.prune_static_tracks = options.prune_static_tracks;
            anim_opt.collapse_constant_keyframes = options.collapse_constant_keyframes;
            anim_opt.decimate_keyframes = options.decimate_keyframes;
            anim_opt.loop_safe = options.loop_safe_animations;
            anim_opt.pos_tolerance = options.anim_pos_tolerance;
            anim_opt.rot_tolerance = options.anim_rot_tolerance;
            anim_opt.scale_tolerance = options.anim_scale_tolerance;
            anim_opt.target_fps = options.anim_target_fps;
            anim_opt.min_rotation_deg = options.anim_min_rotation_deg;

            for (auto& a : model->animations) {
                auto stats = optimize_animation(a, model->bones, anim_opt);
                if (stats.optimized_tracks < stats.original_tracks || stats.optimized_keyframes < stats.original_keyframes) {
                    if (callback) {
                        callback(input.filename().string(), 0.55f, true,
                            "Optimized animation '" + a.name + "': tracks " +
                            std::to_string(stats.original_tracks) + " -> " + std::to_string(stats.optimized_tracks) +
                            ", keyframes " + std::to_string(stats.original_keyframes) + " -> " +
                            std::to_string(stats.optimized_keyframes));
                    }
                }
            }
        }

        bool has_real_anims = false;
        for (const auto& a : model->animations) {
            if (!a.tracks.empty()) {
                has_real_anims = true;
                break;
            }
        }
        bool split_anims = options.split_animations && has_real_anims;

        if (split_anims) {
            // Write base model without animations
            GrnModel base_model = *model;
            base_model.animations.clear();

            if (callback) callback(input.filename().string(), 0.6f, true, "Serializing base model GRN: " + out_path.string());
            bool ok = write_grn_file(out_path, base_model);
            if (!ok) {
                if (callback) callback(out_path.filename().string(), 0.0f, false, "Failed writing base GRN model");
                return false;
            }

            // Export each animation into a separate .grn file with skeleton
            auto out_dir = out_path.parent_path();
            std::string stem = out_path.stem().string();

            // Case-insensitive prefix check to avoid double-prefixing (e.g. Character_Character_Walk.grn)
            auto starts_with_ci = [](const std::string& str, const std::string& prefix) -> bool {
                if (str.size() < prefix.size()) return false;
                for (size_t i = 0; i < prefix.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(str[i])) !=
                        std::tolower(static_cast<unsigned char>(prefix[i]))) {
                        return false;
                    }
                }
                return true;
            };

            std::unordered_set<std::string> used_anim_filenames;
            for (size_t ai = 0; ai < model->animations.size(); ++ai) {
                const auto& anim = model->animations[ai];
                std::string safe_anim_name = anim.name.empty() ? ("Anim_" + std::to_string(ai)) : anim.name;
                for (char& c : safe_anim_name) {
                    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
                        c = '_';
                    }
                }

                std::string anim_base_name;
                if (starts_with_ci(safe_anim_name, stem + "_") || starts_with_ci(safe_anim_name, stem + "-")) {
                    anim_base_name = safe_anim_name;
                } else if (safe_anim_name == stem) {
                    anim_base_name = stem + "_anim";
                } else {
                    anim_base_name = stem + "_" + safe_anim_name;
                }

                std::string unique_anim_filename = anim_base_name;
                int counter = 1;
                while (used_anim_filenames.count(unique_anim_filename) || unique_anim_filename == stem) {
                    unique_anim_filename = anim_base_name + "_" + std::to_string(counter++);
                }
                used_anim_filenames.insert(unique_anim_filename);

                std::filesystem::path anim_out = out_dir / (unique_anim_filename + ".grn");

                GrnModel anim_model;
                anim_model.bones = model->bones;
                anim_model.animations.push_back(anim);

                float prog = 0.6f + 0.4f * (static_cast<float>(ai + 1) / static_cast<float>(model->animations.size()));
                if (callback) callback(anim.name, prog, true,
                                       "Writing animation GRN: " + anim_out.filename().string() + " (" + std::to_string(anim.tracks.size()) + " tracks)");
                write_grn_file(anim_out, anim_model);
            }

            if (callback) callback(out_path.filename().string(), 1.0f, true,
                "Successfully converted GLB -> GRN (Model + " + std::to_string(model->animations.size()) + " animation files)");
            return true;
        } else {
            if (callback) callback(input.filename().string(), 0.6f, true, "Serializing GRN container: " + out_path.string());

            bool ok = write_grn_file(out_path, *model);
            if (callback) {
                if (ok) callback(out_path.filename().string(), 1.0f, true, "Successfully converted GLB -> GRN (" + std::to_string(std::filesystem::file_size(out_path)) + " bytes)");
                else callback(out_path.filename().string(), 0.0f, false, "Failed writing GRN file");
            }
            return ok;
        }
    }
}

ConversionResult convert_directory(const std::filesystem::path& input_dir,
                                   const std::filesystem::path& output_dir,
                                   const ConversionOptions& options,
                                   ProgressCallback callback) {
    ConversionResult res;
    if (!std::filesystem::is_directory(input_dir)) {
        res.log_messages.push_back("Error: input path is not a directory: " + input_dir.string());
        return res;
    }

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(input_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".grn" || ext == ".glb" || ext == ".gltf") {
            files.push_back(entry.path());
        }
    }

    res.files_attempted = files.size();
    if (files.empty()) {
        if (callback) callback("", 1.0f, true, "No compatible model files found in " + input_dir.string());
        return res;
    }

    std::filesystem::path out_base = output_dir.empty() ? input_dir : output_dir;

    std::atomic<size_t> next_index{0};
    std::atomic<size_t> succeeded{0};
    std::atomic<size_t> failed{0};
    std::mutex cb_mutex;

    size_t num_threads = std::max<size_t>(1, std::thread::hardware_concurrency());
    auto worker = [&]() {
        while (true) {
            size_t i = next_index.fetch_add(1);
            if (i >= files.size()) break;

            const auto& in_file = files[i];
            auto rel = std::filesystem::relative(in_file, input_dir);
            auto out_file = out_base / rel;

            std::string ext = in_file.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            out_file.replace_extension(ext == ".grn" ? ".glb" : ".grn");
            std::error_code ec;
            std::filesystem::create_directories(out_file.parent_path(), ec);

            ProgressCallback thread_cb = nullptr;
            if (callback && files.size() <= 20) {
                thread_cb = [&](const std::string& item, float p, bool s, const std::string& msg) {
                    std::lock_guard<std::mutex> lock(cb_mutex);
                    callback(item, p, s, msg);
                };
            }

            bool ok = convert_file(in_file, out_file, options, thread_cb);
            if (ok) {
                succeeded.fetch_add(1);
            } else {
                failed.fetch_add(1);
            }

            if (callback && files.size() > 20) {
                size_t done = succeeded.load() + failed.load();
                if (done % 200 == 0 || done == files.size()) {
                    float p = static_cast<float>(done) / static_cast<float>(files.size());
                    std::lock_guard<std::mutex> lock(cb_mutex);
                    callback(in_file.filename().string(), p, true,
                             "Processed " + std::to_string(done) + " / " + std::to_string(files.size()) + " files");
                }
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

    res.files_succeeded = succeeded.load();
    res.files_failed = failed.load();

    if (callback) {
        callback("", 1.0f, true, "Batch completed: " + std::to_string(res.files_succeeded) + "/" + std::to_string(res.files_attempted) + " succeeded.");
    }
    return res;
}

static inline Vec4 quat_mul(const Vec4& a, const Vec4& b) {
    return Vec4{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

static inline Vec4 quat_inv(const Vec4& q) {
    float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len_sq > 1e-12f) {
        float inv = 1.0f / len_sq;
        return Vec4{-q.x * inv, -q.y * inv, -q.z * inv, q.w * inv};
    }
    return Vec4{0.0f, 0.0f, 0.0f, 1.0f};
}

static inline Vec3 quat_rot_vec(const Vec4& q, const Vec3& v) {
    Vec4 qv{v.x, v.y, v.z, 0.0f};
    Vec4 res = quat_mul(quat_mul(q, qv), quat_inv(q));
    return Vec3{res.x, res.y, res.z};
}

bool detect_is_z_up(const GrnModel& model) {
    // 1. Vertex Bounding Box Analysis (Ground truth when meshes are present)
    float min_y = 1e30f, max_y = -1e30f;
    float min_z = 1e30f, max_z = -1e30f;
    size_t total_verts = 0;
    for (const auto& m : model.meshes) {
        total_verts += m.vertices.size();
        for (const auto& v : m.vertices) {
            min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
            min_z = std::min(min_z, v.z); max_z = std::max(max_z, v.z);
        }
    }

    if (total_verts > 0) {
        float span_y = max_y - min_y;
        float span_z = max_z - min_z;

        // Groundedness: base touches ground plane (~0) and rises into +axis
        bool z_grounded = (span_z > 0.01f) && (min_z >= -0.15f * span_z) && (min_z <= 0.15f * span_z) && (max_z > 0.5f * span_z);
        bool y_grounded = (span_y > 0.01f) && (min_y >= -0.15f * span_y) && (min_y <= 0.15f * span_y) && (max_y > 0.5f * span_y);

        // Groundedness is invariant to body proportions (bipeds vs quadrupeds):
        // Standing models in Z-up are grounded on Z=0 and extend into negative coordinates on Y.
        // Standing models in Y-up are grounded on Y=0 and extend into negative coordinates on Z.
        if (z_grounded && !y_grounded) {
            return true; // Z-up: bipeds, quadrupeds, animals, ground props
        }
        if (y_grounded && !z_grounded) {
            // A standing Y-up model (biped or quadruped) must have realistic 3D depth along Z.
            // Slender directional meshes along Y with negligible Z thickness (swords, staves, bows,
            // needles) are native Z-up weapons/props centered at the origin, not Y-up standing models.
            if (span_z < 0.06f * span_y) {
                return true; // Z-up weapon / directional prop
            }
            return false; // Y-up: glTF bipeds, glTF quadrupeds, ground props
        }

        // When both or neither are grounded:
        if (span_z > 1.25f * span_y && span_z > 0.01f) {
            return true;                  // Height along Z -> Z-up biped
        }
        if (span_y > 1.25f * span_z && span_y > 0.01f) {
            if (y_grounded) return false; // Height along Y -> Y-up biped
            // For ungrounded models (flying creatures like bats, armor pieces, helmets, shoes)
            // or quadrupeds with unusual bounds, do not declare Y-up without positive grounding evidence.
            return true;
        }

        return true; // Mesh present and not Y-up -> default to Z-up for Granny
    }

    // 2. Skeletal Hierarchy World Transformation Analysis (for pure animation / skeleton-only models)
    if (!model.bones.empty()) {
        struct Mat4 {
            float m[16];
        };
        std::vector<Mat4> world(model.bones.size());
        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            const auto& b = model.bones[bi];
            float qx = b.rotation.x, qy = b.rotation.y, qz = b.rotation.z, qw = b.rotation.w;
            float lenSq = qx * qx + qy * qy + qz * qz + qw * qw;
            if (lenSq > 1e-8f) {
                float inv = 1.0f / std::sqrt(lenSq);
                qx *= inv; qy *= inv; qz *= inv; qw *= inv;
            } else {
                qx = qy = qz = 0.0f; qw = 1.0f;
            }
            float xx = qx * qx, yy = qy * qy, zz = qz * qz;
            float xy = qx * qy, xz = qx * qz, yz = qy * qz;
            float wx = qw * qx, wy = qw * qy, wz = qw * qz;

            Mat4 local{};
            local.m[0] = 1.0f - 2.0f * (yy + zz);
            local.m[1] = 2.0f * (xy + wz);
            local.m[2] = 2.0f * (xz - wy);
            local.m[3] = 0.0f;

            local.m[4] = 2.0f * (xy - wz);
            local.m[5] = 1.0f - 2.0f * (xx + zz);
            local.m[6] = 2.0f * (yz + wx);
            local.m[7] = 0.0f;

            local.m[8] = 2.0f * (xz + wy);
            local.m[9] = 2.0f * (yz - wx);
            local.m[10] = 1.0f - 2.0f * (xx + yy);
            local.m[11] = 0.0f;

            local.m[12] = b.position.x;
            local.m[13] = b.position.y;
            local.m[14] = b.position.z;
            local.m[15] = 1.0f;

            int32_t p = b.parent_index;
            if (p >= 0 && static_cast<size_t>(p) < bi) {
                const auto& pw = world[p];
                Mat4 wm{};
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        float s = 0.0f;
                        for (int k = 0; k < 4; ++k) s += pw.m[k * 4 + r] * local.m[c * 4 + k];
                        wm.m[c * 4 + r] = s;
                    }
                }
                world[bi] = wm;
            } else {
                world[bi] = local;
            }
        }

        int headIdx = -1, pelvisIdx = -1, spineIdx = -1, rootIdx = -1;
        for (size_t i = 0; i < model.bones.size(); ++i) {
            std::string lower = model.bones[i].name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (headIdx < 0 && (lower.find("head") != std::string::npos || lower.find("neck") != std::string::npos)) {
                headIdx = static_cast<int>(i);
            }
            if (spineIdx < 0 && lower.find("spine") != std::string::npos) {
                spineIdx = static_cast<int>(i);
            }
            if (pelvisIdx < 0 && (lower.find("pelvis") != std::string::npos || lower.find("hips") != std::string::npos)) {
                pelvisIdx = static_cast<int>(i);
            }
            if (rootIdx < 0 && lower.find("root") != std::string::npos) {
                rootIdx = static_cast<int>(i);
            }
        }
        if (pelvisIdx < 0) pelvisIdx = rootIdx;
        if (pelvisIdx < 0) {
            for (size_t i = 0; i < model.bones.size(); ++i) {
                if (model.bones[i].parent_index < 0) {
                    pelvisIdx = static_cast<int>(i);
                    break;
                }
            }
        }
        int topIdx = (headIdx >= 0) ? headIdx : spineIdx;
        if (topIdx >= 0 && pelvisIdx >= 0 && topIdx != pelvisIdx) {
            float dy = std::abs(world[topIdx].m[13] - world[pelvisIdx].m[13]);
            float dz = std::abs(world[topIdx].m[14] - world[pelvisIdx].m[14]);
            if (dz > 1.25f * dy && dz > 1.0f) return true; // Humanoid/animal spine along Z -> Z-up
            if (dy > 1.25f * dz && dy > 1.0f) return false; // Positive spine/head indicators point along Y -> Y-up
        }

        float min_wy = 1e30f, max_wy = -1e30f;
        float min_wz = 1e30f, max_wz = -1e30f;
        size_t valid_bones = 0;
        for (size_t i = 0; i < model.bones.size(); ++i) {
            std::string lower = model.bones[i].name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            // Ignore camera and light helper nodes that artificially inflate bounds
            if (lower.find("cam") != std::string::npos || lower.find("spot") != std::string::npos ||
                lower.find("light") != std::string::npos || lower.find(".target") != std::string::npos) {
                continue;
            }
            valid_bones++;
            float wy = world[i].m[13];
            float wz = world[i].m[14];
            min_wy = std::min(min_wy, wy); max_wy = std::max(max_wy, wy);
            min_wz = std::min(min_wz, wz); max_wz = std::max(max_wz, wz);
        }

        if (valid_bones > 0) {
            float w_span_y = max_wy - min_wy;
            float w_span_z = max_wz - min_wz;
            bool wz_grounded = (w_span_z > 0.01f) && (min_wz >= -0.2f * w_span_z) && (max_wz > 0.5f * w_span_z);
            bool wy_grounded = (w_span_y > 0.01f) && (min_wy >= -0.2f * w_span_y) && (max_wy > 0.5f * w_span_y);

            if (wz_grounded && !wy_grounded) return true;
            if (w_span_z > 1.25f * w_span_y && w_span_z > 1.0f) return true;
        }
    }

    return true; // Default to Z-up for Granny 1.2b
}

} // namespace grn
