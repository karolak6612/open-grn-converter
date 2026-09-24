#include "anim_optimizer.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace grn {

namespace {

inline float vec3Dist(const Vec3& a, const Vec3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float quatDiff(const Vec4& a, const Vec4& b) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    return 1.0f - std::abs(dot);
}

inline float scaleDiff(const std::array<float, 9>& a, const std::array<float, 9>& b) {
    float maxDiff = 0.0f;
    for (int i = 0; i < 9; ++i) {
        maxDiff = std::max(maxDiff, std::abs(a[i] - b[i]));
    }
    return maxDiff;
}

inline Vec3 lerpVec3(const Vec3& a, const Vec3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

inline Vec4 nlerpQuat(const Vec4& a, const Vec4& b, float t) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    float sign = (dot >= 0.0f) ? 1.0f : -1.0f;
    float rx = a.x * (1.0f - t) + b.x * t * sign;
    float ry = a.y * (1.0f - t) + b.y * t * sign;
    float rz = a.z * (1.0f - t) + b.z * t * sign;
    float rw = a.w * (1.0f - t) + b.w * t * sign;
    float len = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
    if (len > 1e-6f) {
        float inv = 1.0f / len;
        return { rx * inv, ry * inv, rz * inv, rw * inv };
    }
    return a;
}

inline std::array<float, 9> lerpScale(const std::array<float, 9>& a, const std::array<float, 9>& b, float t) {
    std::array<float, 9> res{};
    for (int i = 0; i < 9; ++i) {
        res[i] = a[i] + (b[i] - a[i]) * t;
    }
    return res;
}

// Decimate intermediate positions between start and end (with loop boundary protection)
void decimateTranslations(std::vector<float>& times, std::vector<Vec3>& values, float tol, bool loop_safe) {
    if (values.size() <= 2) return;

    // Detect if this channel forms a cyclic loop (start pose matches end pose within tolerance)
    bool isLoop = loop_safe && (vec3Dist(values.front(), values.back()) <= tol * 5.0f);
    if (isLoop) {
        // Snap boundary endpoints for exact bit-equality
        values.back() = values.front();
    }

    if (isLoop && values.size() > 4) {
        std::vector<float> newTimes;
        std::vector<Vec3> newValues;

        // Lock outgoing tangent: times[0], times[1]
        newTimes.push_back(times[0]);
        newValues.push_back(values[0]);
        newTimes.push_back(times[1]);
        newValues.push_back(values[1]);

        size_t anchor = 1;
        const size_t endLimit = values.size() - 2; // preserve up to N-2
        while (anchor < endLimit) {
            size_t furthest = anchor + 1;
            for (size_t candidate = anchor + 2; candidate <= endLimit; ++candidate) {
                float t0 = times[anchor];
                float t1 = times[candidate];
                float dt = t1 - t0;
                if (dt <= 1e-6f) break;

                bool canBridge = true;
                for (size_t mid = anchor + 1; mid < candidate; ++mid) {
                    float u = (times[mid] - t0) / dt;
                    Vec3 interp = lerpVec3(values[anchor], values[candidate], u);
                    if (vec3Dist(values[mid], interp) > tol) {
                        canBridge = false;
                        break;
                    }
                }

                if (canBridge) {
                    furthest = candidate;
                } else {
                    break;
                }
            }

            if (furthest < endLimit) {
                newTimes.push_back(times[furthest]);
                newValues.push_back(values[furthest]);
                anchor = furthest;
            } else {
                break;
            }
        }

        // Lock incoming tangent: times[N-2], times[N-1]
        if (newTimes.back() != times[endLimit]) {
            newTimes.push_back(times[endLimit]);
            newValues.push_back(values[endLimit]);
        }
        newTimes.push_back(times.back());
        newValues.push_back(values.back());

        times = std::move(newTimes);
        values = std::move(newValues);
        return;
    }

    // Standard linear decimation
    std::vector<float> newTimes;
    std::vector<Vec3> newValues;
    newTimes.push_back(times.front());
    newValues.push_back(values.front());

    size_t anchor = 0;
    while (anchor < values.size() - 1) {
        size_t furthest = anchor + 1;
        for (size_t candidate = anchor + 2; candidate < values.size(); ++candidate) {
            float t0 = times[anchor];
            float t1 = times[candidate];
            float dt = t1 - t0;
            if (dt <= 1e-6f) break;

            bool canBridge = true;
            for (size_t mid = anchor + 1; mid < candidate; ++mid) {
                float u = (times[mid] - t0) / dt;
                Vec3 interp = lerpVec3(values[anchor], values[candidate], u);
                if (vec3Dist(values[mid], interp) > tol) {
                    canBridge = false;
                    break;
                }
            }

            if (canBridge) {
                furthest = candidate;
            } else {
                break;
            }
        }

        newTimes.push_back(times[furthest]);
        newValues.push_back(values[furthest]);
        anchor = furthest;
    }

    times = std::move(newTimes);
    values = std::move(newValues);
}

// Decimate intermediate rotations between start and end (with loop boundary protection)
void decimateRotations(std::vector<float>& times, std::vector<Vec4>& values, float tol, bool loop_safe) {
    if (values.size() <= 2) return;

    bool isLoop = loop_safe && (quatDiff(values.front(), values.back()) <= tol * 5.0f);
    if (isLoop) {
        float dot = values.front().x * values.back().x +
                    values.front().y * values.back().y +
                    values.front().z * values.back().z +
                    values.front().w * values.back().w;
        float sign = (dot >= 0.0f) ? 1.0f : -1.0f;
        values.back().x = values.front().x * sign;
        values.back().y = values.front().y * sign;
        values.back().z = values.front().z * sign;
        values.back().w = values.front().w * sign;
    }

    if (isLoop && values.size() > 4) {
        std::vector<float> newTimes;
        std::vector<Vec4> newValues;

        newTimes.push_back(times[0]);
        newValues.push_back(values[0]);
        newTimes.push_back(times[1]);
        newValues.push_back(values[1]);

        size_t anchor = 1;
        const size_t endLimit = values.size() - 2;
        while (anchor < endLimit) {
            size_t furthest = anchor + 1;
            for (size_t candidate = anchor + 2; candidate <= endLimit; ++candidate) {
                float t0 = times[anchor];
                float t1 = times[candidate];
                float dt = t1 - t0;
                if (dt <= 1e-6f) break;

                bool canBridge = true;
                for (size_t mid = anchor + 1; mid < candidate; ++mid) {
                    float u = (times[mid] - t0) / dt;
                    Vec4 interp = nlerpQuat(values[anchor], values[candidate], u);
                    if (quatDiff(values[mid], interp) > tol) {
                        canBridge = false;
                        break;
                    }
                }

                if (canBridge) {
                    furthest = candidate;
                } else {
                    break;
                }
            }

            if (furthest < endLimit) {
                newTimes.push_back(times[furthest]);
                newValues.push_back(values[furthest]);
                anchor = furthest;
            } else {
                break;
            }
        }

        if (newTimes.back() != times[endLimit]) {
            newTimes.push_back(times[endLimit]);
            newValues.push_back(values[endLimit]);
        }
        newTimes.push_back(times.back());
        newValues.push_back(values.back());

        times = std::move(newTimes);
        values = std::move(newValues);
        return;
    }

    std::vector<float> newTimes;
    std::vector<Vec4> newValues;
    newTimes.push_back(times.front());
    newValues.push_back(values.front());

    size_t anchor = 0;
    while (anchor < values.size() - 1) {
        size_t furthest = anchor + 1;
        for (size_t candidate = anchor + 2; candidate < values.size(); ++candidate) {
            float t0 = times[anchor];
            float t1 = times[candidate];
            float dt = t1 - t0;
            if (dt <= 1e-6f) break;

            bool canBridge = true;
            for (size_t mid = anchor + 1; mid < candidate; ++mid) {
                float u = (times[mid] - t0) / dt;
                Vec4 interp = nlerpQuat(values[anchor], values[candidate], u);
                if (quatDiff(values[mid], interp) > tol) {
                    canBridge = false;
                    break;
                }
            }

            if (canBridge) {
                furthest = candidate;
            } else {
                break;
            }
        }

        newTimes.push_back(times[furthest]);
        newValues.push_back(values[furthest]);
        anchor = furthest;
    }

    times = std::move(newTimes);
    values = std::move(newValues);
}

// Decimate intermediate scale/shears between start and end (with loop boundary protection)
void decimateScaleShears(std::vector<float>& times, std::vector<std::array<float, 9>>& values, float tol, bool loop_safe) {
    if (values.size() <= 2) return;

    bool isLoop = loop_safe && (scaleDiff(values.front(), values.back()) <= tol * 5.0f);
    if (isLoop) {
        values.back() = values.front();
    }

    if (isLoop && values.size() > 4) {
        std::vector<float> newTimes;
        std::vector<std::array<float, 9>> newValues;

        newTimes.push_back(times[0]);
        newValues.push_back(values[0]);
        newTimes.push_back(times[1]);
        newValues.push_back(values[1]);

        size_t anchor = 1;
        const size_t endLimit = values.size() - 2;
        while (anchor < endLimit) {
            size_t furthest = anchor + 1;
            for (size_t candidate = anchor + 2; candidate <= endLimit; ++candidate) {
                float t0 = times[anchor];
                float t1 = times[candidate];
                float dt = t1 - t0;
                if (dt <= 1e-6f) break;

                bool canBridge = true;
                for (size_t mid = anchor + 1; mid < candidate; ++mid) {
                    float u = (times[mid] - t0) / dt;
                    auto interp = lerpScale(values[anchor], values[candidate], u);
                    if (scaleDiff(values[mid], interp) > tol) {
                        canBridge = false;
                        break;
                    }
                }

                if (canBridge) {
                    furthest = candidate;
                } else {
                    break;
                }
            }

            if (furthest < endLimit) {
                newTimes.push_back(times[furthest]);
                newValues.push_back(values[furthest]);
                anchor = furthest;
            } else {
                break;
            }
        }

        if (newTimes.back() != times[endLimit]) {
            newTimes.push_back(times[endLimit]);
            newValues.push_back(values[endLimit]);
        }
        newTimes.push_back(times.back());
        newValues.push_back(values.back());

        times = std::move(newTimes);
        values = std::move(newValues);
        return;
    }

    std::vector<float> newTimes;
    std::vector<std::array<float, 9>> newValues;
    newTimes.push_back(times.front());
    newValues.push_back(values.front());

    size_t anchor = 0;
    while (anchor < values.size() - 1) {
        size_t furthest = anchor + 1;
        for (size_t candidate = anchor + 2; candidate < values.size(); ++candidate) {
            float t0 = times[anchor];
            float t1 = times[candidate];
            float dt = t1 - t0;
            if (dt <= 1e-6f) break;

            bool canBridge = true;
            for (size_t mid = anchor + 1; mid < candidate; ++mid) {
                float u = (times[mid] - t0) / dt;
                auto interp = lerpScale(values[anchor], values[candidate], u);
                if (scaleDiff(values[mid], interp) > tol) {
                    canBridge = false;
                    break;
                }
            }

            if (canBridge) {
                furthest = candidate;
            } else {
                break;
            }
        }

        newTimes.push_back(times[furthest]);
        newValues.push_back(values[furthest]);
        anchor = furthest;
    }

    times = std::move(newTimes);
    values = std::move(newValues);
}

template<typename T, typename LerpFn>
T sampleChannel(const std::vector<float>& times, const std::vector<T>& values, float t, LerpFn lerp) {
    if (values.empty()) return {};
    if (values.size() == 1 || t <= times.front()) return values.front();
    if (t >= times.back()) return values.back();

    auto it = std::upper_bound(times.begin(), times.end(), t);
    size_t idx1 = std::distance(times.begin(), it);
    size_t idx0 = idx1 - 1;

    float t0 = times[idx0];
    float t1 = times[idx1];
    float dt = t1 - t0;
    if (dt <= 1e-6f) return values[idx0];

    float u = (t - t0) / dt;
    return lerp(values[idx0], values[idx1], u);
}

void resampleTrack(AnimTrack& tr, float duration, float target_fps, bool loop_safe) {
    if (target_fps <= 0.0f || duration <= 0.0f) return;
    float dt = 1.0f / target_fps;
    int numFrames = static_cast<int>(std::round(duration * target_fps)) + 1;
    if (numFrames < 2) numFrames = 2;

    std::vector<float> sampleTimes(numFrames);
    for (int i = 0; i < numFrames; ++i) {
        sampleTimes[i] = std::min(duration, static_cast<float>(i) * dt);
    }
    sampleTimes.front() = 0.0f;
    sampleTimes.back() = duration;

    // Resample rotations
    if (tr.rotations.size() > 1) {
        std::vector<Vec4> newRots(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            newRots[i] = sampleChannel(tr.rotation_times, tr.rotations, sampleTimes[i], nlerpQuat);
        }
        if (loop_safe && quatDiff(newRots.front(), newRots.back()) <= 0.02f) {
            newRots.back() = newRots.front();
        }
        tr.rotation_times = sampleTimes;
        tr.rotations = std::move(newRots);
    }

    // Resample translations
    if (tr.translations.size() > 1) {
        std::vector<Vec3> newTrans(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            newTrans[i] = sampleChannel(tr.translation_times, tr.translations, sampleTimes[i], lerpVec3);
        }
        if (loop_safe && vec3Dist(newTrans.front(), newTrans.back()) <= 0.05f) {
            newTrans.back() = newTrans.front();
        }
        tr.translation_times = sampleTimes;
        tr.translations = std::move(newTrans);
    }

    // Resample scale
    if (tr.scale_shears.size() > 1) {
        std::vector<std::array<float, 9>> newScales(numFrames);
        for (int i = 0; i < numFrames; ++i) {
            newScales[i] = sampleChannel(tr.scale_shear_times, tr.scale_shears, sampleTimes[i], lerpScale);
        }
        if (loop_safe && scaleDiff(newScales.front(), newScales.back()) <= 0.05f) {
            newScales.back() = newScales.front();
        }
        tr.scale_shear_times = sampleTimes;
        tr.scale_shears = std::move(newScales);
    }
}

} // namespace

AnimOptimizationStats optimize_animation(GrnAnimation& anim,
                                        const std::vector<GrnBone>& bones,
                                        const AnimOptimizationOptions& opts) {
    AnimOptimizationStats stats{};
    stats.original_tracks = anim.tracks.size();

    for (const auto& tr : anim.tracks) {
        stats.original_keyframes += tr.translations.size() + tr.rotations.size() + tr.scale_shears.size();
    }

    // Build bone lookup map by name and channel_id
    std::unordered_map<std::string, const GrnBone*> boneByName;
    for (const auto& b : bones) {
        if (!b.name.empty()) {
            boneByName[b.name] = &b;
        }
    }

    std::vector<AnimTrack> optimizedTracks;
    optimizedTracks.reserve(anim.tracks.size());

    for (auto& tr : anim.tracks) {
        // Find corresponding bone
        const GrnBone* bone = nullptr;
        if (tr.channel_id > 0 && static_cast<size_t>(tr.channel_id - 1) < bones.size()) {
            bone = &bones[tr.channel_id - 1];
        } else if (!tr.bone_name.empty()) {
            auto it = boneByName.find(tr.bone_name);
            if (it != boneByName.end()) {
                bone = it->second;
            }
        }

        // --- 1. Static / Rest-Pose Track Pruning (Motion-based pre-check) ---
        if (opts.prune_static_tracks && bone != nullptr) {
            float maxPosMotion = 0.0f;
            if (!tr.translations.empty()) {
                const auto& p0 = tr.translations.front();
                for (size_t i = 1; i < tr.translations.size(); ++i) {
                    maxPosMotion = std::max(maxPosMotion, vec3Dist(tr.translations[i], p0));
                }
            }

            float maxRotMotion = 0.0f;
            if (!tr.rotations.empty()) {
                const auto& q0 = tr.rotations.front();
                for (size_t i = 1; i < tr.rotations.size(); ++i) {
                    maxRotMotion = std::max(maxRotMotion, quatDiff(tr.rotations[i], q0));
                }
            }

            float maxScaleMotion = 0.0f;
            if (!tr.scale_shears.empty()) {
                const auto& s0 = tr.scale_shears.front();
                for (size_t i = 1; i < tr.scale_shears.size(); ++i) {
                    maxScaleMotion = std::max(maxScaleMotion, scaleDiff(tr.scale_shears[i], s0));
                }
            }

            // Only consider pruning if the bone exhibits ZERO authored motion (accounting for float noise)
            const float motionThreshold = opts.loop_safe ? 1e-4f : opts.pos_tolerance;
            const float rotMotionThreshold = opts.loop_safe ? 1e-4f : opts.rot_tolerance;
            const float scaleMotionThreshold = opts.loop_safe ? 1e-4f : opts.scale_tolerance;

            bool isTrulyStationary = (maxPosMotion <= motionThreshold &&
                                      maxRotMotion <= rotMotionThreshold &&
                                      maxScaleMotion <= scaleMotionThreshold);

            if (isTrulyStationary) {
                bool posMatchesRest = tr.translations.empty() || (vec3Dist(tr.translations.front(), bone->position) <= opts.pos_tolerance);
                bool rotMatchesRest = tr.rotations.empty() || (quatDiff(tr.rotations.front(), bone->rotation) <= opts.rot_tolerance);
                bool scaleMatchesRest = tr.scale_shears.empty() || (scaleDiff(tr.scale_shears.front(), bone->scale_3x3) <= opts.scale_tolerance);

                if (posMatchesRest && rotMatchesRest && scaleMatchesRest) {
                    // Truly stationary bone resting at bind pose: omit track entirely
                    continue;
                }
            }
        }

        // --- 1b. Rotational Amplitude Culling (Micro-Bone Pruning) ---
        if (opts.min_rotation_deg > 0.0f) {
            float maxRotDeg = 0.0f;
            if (!tr.rotations.empty()) {
                const auto& q0 = tr.rotations.front();
                for (size_t i = 1; i < tr.rotations.size(); ++i) {
                    const auto& q = tr.rotations[i];
                    float dot = std::abs(q.x * q0.x + q.y * q0.y + q.z * q0.z + q.w * q0.w);
                    dot = std::clamp(dot, -1.0f, 1.0f);
                    float deg = 2.0f * std::acos(dot) * (180.0f / 3.14159265358979323846f);
                    maxRotDeg = std::max(maxRotDeg, deg);
                }
            }
            float maxPosMotion = 0.0f;
            if (!tr.translations.empty()) {
                const auto& p0 = tr.translations.front();
                for (size_t i = 1; i < tr.translations.size(); ++i) {
                    maxPosMotion = std::max(maxPosMotion, vec3Dist(tr.translations[i], p0));
                }
            }
            if (maxRotDeg < opts.min_rotation_deg && maxPosMotion <= opts.pos_tolerance) {
                // Micro-jitter motion below threshold: omit track to keep 32-bit viewer within memory limits
                continue;
            }
        }

        // --- 1c. Uniform Frame Rate Resampling ---
        if (opts.target_fps > 0.0f && anim.duration > 0.0f) {
            resampleTrack(tr, anim.duration, opts.target_fps, opts.loop_safe);
        }

        // --- 2. Constant Keyframe Collapsing ---
        if (opts.collapse_constant_keyframes) {
            const float collapsePosTol = opts.loop_safe ? 1e-4f : opts.pos_tolerance;
            const float collapseRotTol = opts.loop_safe ? 1e-4f : opts.rot_tolerance;
            const float collapseScaleTol = opts.loop_safe ? 1e-4f : opts.scale_tolerance;

            // Position
            if (tr.translations.size() > 1) {
                bool constant = true;
                const auto& v0 = tr.translations.front();
                for (size_t i = 1; i < tr.translations.size(); ++i) {
                    if (vec3Dist(tr.translations[i], v0) > collapsePosTol) {
                        constant = false;
                        break;
                    }
                }
                if (constant) {
                    tr.translations = { v0 };
                    tr.translation_times = { 0.0f };
                }
            }

            // Rotation
            if (tr.rotations.size() > 1) {
                bool constant = true;
                const auto& q0 = tr.rotations.front();
                for (size_t i = 1; i < tr.rotations.size(); ++i) {
                    if (quatDiff(tr.rotations[i], q0) > collapseRotTol) {
                        constant = false;
                        break;
                    }
                }
                if (constant) {
                    tr.rotations = { q0 };
                    tr.rotation_times = { 0.0f };
                }
            }

            // Scale
            if (tr.scale_shears.size() > 1) {
                bool constant = true;
                const auto& s0 = tr.scale_shears.front();
                for (size_t i = 1; i < tr.scale_shears.size(); ++i) {
                    if (scaleDiff(tr.scale_shears[i], s0) > collapseScaleTol) {
                        constant = false;
                        break;
                    }
                }
                if (constant) {
                    tr.scale_shears = { s0 };
                    tr.scale_shear_times = { 0.0f };
                }
            }
        }

        // --- 3. Keyframe Decimation (Collinear / Linear interpolation) ---
        if (opts.decimate_keyframes) {
            if (tr.translations.size() > 2) {
                decimateTranslations(tr.translation_times, tr.translations, opts.pos_tolerance, opts.loop_safe);
            }
            if (tr.rotations.size() > 2) {
                decimateRotations(tr.rotation_times, tr.rotations, opts.rot_tolerance, opts.loop_safe);
            }
            if (tr.scale_shears.size() > 2) {
                decimateScaleShears(tr.scale_shear_times, tr.scale_shears, opts.scale_tolerance, opts.loop_safe);
            }
        }

        // --- 4. Post-check: Static Rest-Pose Track Pruning after collapsing ---
        if (opts.prune_static_tracks && bone != nullptr) {
            bool posMatchesRest = tr.translations.empty() ||
                (tr.translations.size() == 1 && vec3Dist(tr.translations.front(), bone->position) <= opts.pos_tolerance);

            bool rotMatchesRest = tr.rotations.empty() ||
                (tr.rotations.size() == 1 && quatDiff(tr.rotations.front(), bone->rotation) <= opts.rot_tolerance);

            bool scaleMatchesRest = tr.scale_shears.empty() ||
                (tr.scale_shears.size() == 1 && scaleDiff(tr.scale_shears.front(), bone->scale_3x3) <= opts.scale_tolerance);

            if (posMatchesRest && rotMatchesRest && scaleMatchesRest) {
                continue;
            }
        }

        stats.optimized_keyframes += tr.translations.size() + tr.rotations.size() + tr.scale_shears.size();
        optimizedTracks.push_back(std::move(tr));
    }

    stats.optimized_tracks = optimizedTracks.size();
    anim.tracks = std::move(optimizedTracks);
    if (opts.target_fps > 0.0f) {
        anim.fps = opts.target_fps;
    }
    return stats;
}

void optimize_all_animations(std::vector<GrnAnimation>& anims,
                             const std::vector<GrnBone>& bones,
                             const AnimOptimizationOptions& opts) {
    for (auto& anim : anims) {
        optimize_animation(anim, bones, opts);
    }
}

} // namespace grn
