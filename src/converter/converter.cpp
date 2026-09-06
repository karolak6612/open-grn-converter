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
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

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
            if (ok) callback(out_path.filename().string(), 1.0f, true, "Successfully converted GRN -> GLB (" + std::to_string(std::filesystem::file_size(out_path)) + " bytes)");
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

        if (callback) callback(input.filename().string(), 0.6f, true, "Serializing GRN container: " + out_path.string());

        bool ok = write_grn_file(out_path, *model);
        if (callback) {
            if (ok) callback(out_path.filename().string(), 1.0f, true, "Successfully converted GLB -> GRN (" + std::to_string(std::filesystem::file_size(out_path)) + " bytes)");
            else callback(out_path.filename().string(), 0.0f, false, "Failed writing GRN file");
        }
        return ok;
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

} // namespace grn
