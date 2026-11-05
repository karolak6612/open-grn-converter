#include "grn_anim_sampler.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace grn {

namespace {

constexpr uint32_t kPrefixPad = 3;
constexpr uint32_t kSuffixPad = 2;

inline void WeightsLinear(float t0, float t1, float t, float &w0, float &w1) {
    float f = (t1 != t0) ? (t - t0) / (t1 - t0) : 0.0f;
    w1 = f;
    w0 = 1.0f - f;
}

inline void WeightsQuadratic(float t0, float t1, float t2, float t3, float t,
                             float &w0, float &w1, float &w2) {
    float f1 = (t2 != t1) ? (t - t1) / (t2 - t1) : 0.0f;
    float f2 = (t2 != t0) ? (t - t0) / (t2 - t0) : 0.0f;
    float g = (f2 + f1) - f2 * f1;
    float h = (t3 != t1) ? ((t - t1) / (t3 - t1)) * f1 : 0.0f;
    w2 = h;
    w1 = g - h;
    w0 = 1.0f - g;
}

inline void WeightsCubic(float t0, float t1, float t2, float t3, float t4, float t5,
                         float t, float &w0, float &w1, float &w2, float &w3) {
    float c2 = (t3 != t2) ? (t - t2) / (t3 - t2) : 0.0f;
    float c1 = (t3 != t1) ? (t - t1) / (t3 - t1) : 0.0f;
    float c3 = (t4 != t2) ? (t - t2) / (t4 - t2) : 0.0f;
    float c0 = (t3 != t0) ? (t - t0) / (t3 - t0) : 0.0f;
    float c1b = (t4 != t1) ? (t - t1) / (t4 - t1) : 0.0f;
    float c4 = (t5 != t2) ? (t - t2) / (t5 - t2) : 0.0f;
    float g = (1.0f - c1) * (1.0f - c2);
    float h = c3 * c2;
    w0 = (1.0f - c0) * g;
    float m = (1.0f - c3) * c2 + (1.0f - c2) * c1;
    w1 = g * c0 + (1.0f - c1b) * m;
    w2 = m * c1b + (1.0f - c4) * h;
    w3 = h * c4;
}

static uint32_t FindBracketIndex(const float *times, uint32_t count, double t) {
    uint32_t idx = kPrefixPad;
    if (count <= kPrefixPad) {
        return idx;
    }
    uint32_t bound = count - kPrefixPad;
    if (kPrefixPad < bound) {
        const float tf = static_cast<float>(t);
        uint32_t lo = kPrefixPad;
        uint32_t hi = bound;
        while (lo < hi) {
            const uint32_t mid = lo + ((hi - lo) >> 1);
            if (times[mid] < tf) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        idx = lo;
    }
    return idx;
}

static std::vector<Vec4> alignSourceRotationHemispheres(const std::vector<Vec4>& src) {
    std::vector<Vec4> out = src;
    for (size_t k = 1; k < out.size(); ++k) {
        const float d = out[k - 1].x * out[k].x + out[k - 1].y * out[k].y +
                        out[k - 1].z * out[k].z + out[k - 1].w * out[k].w;
        if (d < 0.0f) {
            out[k].x = -out[k].x;
            out[k].y = -out[k].y;
            out[k].z = -out[k].z;
            out[k].w = -out[k].w;
        }
    }
    return out;
}

static void filterSourceRotationSpikes(std::vector<Vec4>& q, const std::vector<float>& times) {
    const size_t n = std::min(q.size(), times.size());
    if (n < 3) return;

    auto dot4 = [](const Vec4& a, const Vec4& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    };
    constexpr float kRad2Deg = 180.0f / 3.14159265f;

    bool changed = true;
    int passes = 3;
    while (changed && passes-- > 0) {
        changed = false;
        for (size_t k = 1; k + 1 < n; ++k) {
            const float d01 = std::clamp(std::abs(dot4(q[k - 1], q[k])), 0.0f, 1.0f);
            const float d12 = std::clamp(std::abs(dot4(q[k], q[k + 1])), 0.0f, 1.0f);
            const float d02 = std::clamp(std::abs(dot4(q[k - 1], q[k + 1])), 0.0f, 1.0f);
            const float a01 = 2.0f * std::acos(d01) * kRad2Deg;
            const float a12 = 2.0f * std::acos(d12) * kRad2Deg;
            const float a02 = 2.0f * std::acos(d02) * kRad2Deg;

            if (a01 > 5.0f && a12 > 5.0f && a02 < 8.0f && a02 < 0.25f * std::min(a01, a12)) {
                const float t0 = times[k - 1], t1 = times[k], t2 = times[k + 1];
                const float alpha = (t2 > t0) ? (t1 - t0) / (t2 - t0) : 0.5f;

                // Slerp between q[k-1] and q[k+1]
                float dot = dot4(q[k - 1], q[k + 1]);
                float sign = 1.0f;
                if (dot < 0.0f) {
                    dot = -dot;
                    sign = -1.0f;
                }
                dot = std::clamp(dot, -1.0f, 1.0f);
                float theta = std::acos(dot) * alpha;
                Vec4 qperp{
                    q[k + 1].x * sign - q[k - 1].x * dot,
                    q[k + 1].y * sign - q[k - 1].y * dot,
                    q[k + 1].z * sign - q[k - 1].z * dot,
                    q[k + 1].w * sign - q[k - 1].w * dot
                };
                float perpLen = std::sqrt(dot4(qperp, qperp));
                if (perpLen > 1e-6f) {
                    qperp.x /= perpLen; qperp.y /= perpLen; qperp.z /= perpLen; qperp.w /= perpLen;
                    q[k].x = q[k - 1].x * std::cos(theta) + qperp.x * std::sin(theta);
                    q[k].y = q[k - 1].y * std::cos(theta) + qperp.y * std::sin(theta);
                    q[k].z = q[k - 1].z * std::cos(theta) + qperp.z * std::sin(theta);
                    q[k].w = q[k - 1].w * std::cos(theta) + qperp.w * std::sin(theta);
                } else {
                    q[k] = q[k - 1];
                }
                changed = true;
            }
        }
    }
}

static std::vector<float> buildPaddedTimes(const std::vector<float>& srcTimes) {
    if (srcTimes.empty()) return {};
    size_t n = srcTimes.size();
    std::vector<float> padded(n + kPrefixPad + kSuffixPad);

    for (size_t i = 0; i < n; ++i) {
        padded[kPrefixPad + i] = srcTimes[i];
    }

    float t0 = srcTimes[0];
    padded[0] = t0 - 3.0f;
    padded[1] = t0 - 2.0f;
    padded[2] = t0 - 1.0f;

    float tEnd = srcTimes.back();
    padded[kPrefixPad + n] = tEnd;
    padded[kPrefixPad + n + 1] = tEnd;

    return padded;
}

static std::vector<float> buildPaddedValues(const std::vector<float>& srcValues, uint32_t stride) {
    if (srcValues.empty() || stride == 0) return {};
    size_t n = srcValues.size() / stride;
    std::vector<float> padded((n + kPrefixPad + kSuffixPad) * stride);

    for (size_t i = 0; i < n * stride; ++i) {
        padded[kPrefixPad * stride + i] = srcValues[i];
    }

    // Pre-align consecutive quaternions BEFORE duplicating prefix/suffix
    if (stride == 4 && n > 1) {
        for (size_t i = 1; i < n; ++i) {
            float* prev = padded.data() + (kPrefixPad + i - 1) * 4;
            float* cur = padded.data() + (kPrefixPad + i) * 4;
            float dot = prev[0]*cur[0] + prev[1]*cur[1] + prev[2]*cur[2] + prev[3]*cur[3];
            if (dot < 0.0f) {
                cur[0] = -cur[0];
                cur[1] = -cur[1];
                cur[2] = -cur[2];
                cur[3] = -cur[3];
            }
        }
    }

    // Prefix: duplicate first real value
    for (uint32_t k = 0; k < kPrefixPad; ++k) {
        for (uint32_t s = 0; s < stride; ++s) {
            padded[k * stride + s] = padded[kPrefixPad * stride + s];
        }
    }

    // Suffix: duplicate last real value
    const float* lastVal = &padded[(kPrefixPad + n - 1) * stride];
    for (uint32_t k = 0; k < kSuffixPad; ++k) {
        for (uint32_t s = 0; s < stride; ++s) {
            padded[(kPrefixPad + n + k) * stride + s] = lastVal[s];
        }
    }

    return padded;
}

static void sampleSplitComponent(uint32_t mode, const float *times, uint32_t timeCount,
                                 const float *values, double t, uint32_t stride, float *out) {
    uint32_t idx = FindBracketIndex(times, timeCount, t);
    const float *tp = times + idx;
    const float *vp = values + static_cast<size_t>(idx) * stride;

    const int s = static_cast<int>(stride);
    const float ft = static_cast<float>(t);

    if (stride == 4) {
        switch (mode) {
        case 0: {
            for (int i = 0; i < 4; ++i) out[i] = vp[i];
            break;
        }
        case 1: {
            float w0, w1;
            WeightsLinear(tp[-1], tp[0], ft, w0, w1);
            const float *p0 = vp - 4;
            const float *p1 = vp;
            float dot = p0[0]*p1[0] + p0[1]*p1[1] + p0[2]*p1[2] + p0[3]*p1[3];
            float sign = (dot < 0.0f) ? -1.0f : 1.0f;
            for (int i = 0; i < 4; ++i) {
                out[i] = w1 * p1[i] + (w0 * sign) * p0[i];
            }
            break;
        }
        case 2: {
            float w0, w1, w2;
            WeightsQuadratic(tp[-2], tp[-1], tp[0], tp[1], ft, w0, w1, w2);
            const float *p0 = vp - 8;
            const float *p1 = vp - 4;
            const float *p2 = vp;
            float d1 = p1[0]*p2[0] + p1[1]*p2[1] + p1[2]*p2[2] + p1[3]*p2[3];
            float s1 = (d1 < 0.0f) ? -1.0f : 1.0f;
            float d0 = p0[0]*p2[0] + p0[1]*p2[1] + p0[2]*p2[2] + p0[3]*p2[3];
            float s0 = (d0 < 0.0f) ? -1.0f : 1.0f;
            for (int i = 0; i < 4; ++i) {
                out[i] = w2 * p2[i] + (w1 * s1) * p1[i] + (w0 * s0) * p0[i];
            }
            break;
        }
        case 3: {
            float w0, w1, w2, w3;
            WeightsCubic(tp[-3], tp[-2], tp[-1], tp[0], tp[1], tp[2], ft, w0, w1, w2, w3);
            const float *p0 = vp - 12;
            const float *p1 = vp - 8;
            const float *p2 = vp - 4;
            const float *p3 = vp;
            float d2 = p2[0]*p3[0] + p2[1]*p3[1] + p2[2]*p3[2] + p2[3]*p3[3];
            float s2 = (d2 < 0.0f) ? -1.0f : 1.0f;
            float d1 = p1[0]*p3[0] + p1[1]*p3[1] + p1[2]*p3[2] + p1[3]*p3[3];
            float s1 = (d1 < 0.0f) ? -1.0f : 1.0f;
            float d0 = p0[0]*p3[0] + p0[1]*p3[1] + p0[2]*p3[2] + p0[3]*p3[3];
            float s0 = (d0 < 0.0f) ? -1.0f : 1.0f;
            for (int i = 0; i < 4; ++i) {
                out[i] = w3 * p3[i] + (w2 * s2) * p2[i] + (w1 * s1) * p1[i] + (w0 * s0) * p0[i];
            }
            break;
        }
        default:
            for (int i = 0; i < 4; ++i) out[i] = 0.0f;
            out[3] = 1.0f;
            break;
        }

        float lenSq = out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3];
        if (lenSq > 1e-8f) {
            float invLen = 1.0f / std::sqrt(lenSq);
            for (int i = 0; i < 4; ++i) out[i] *= invLen;
        } else {
            out[0] = out[1] = out[2] = 0.0f;
            out[3] = 1.0f;
        }
        return;
    }

    switch (mode) {
    case 1: {
        float w0, w1;
        WeightsLinear(tp[-1], tp[0], ft, w0, w1);
        for (int i = 0; i < s; ++i) {
            out[i] = w0 * vp[-s + i] + w1 * vp[i];
        }
        break;
    }
    case 2: {
        float w0, w1, w2;
        WeightsQuadratic(tp[-2], tp[-1], tp[0], tp[1], ft, w0, w1, w2);
        for (int i = 0; i < s; ++i) {
            out[i] = w0 * vp[-2 * s + i] + w1 * vp[-s + i] + w2 * vp[i];
        }
        break;
    }
    case 3: {
        float w0, w1, w2, w3;
        WeightsCubic(tp[-3], tp[-2], tp[-1], tp[0], tp[1], tp[2], ft, w0, w1, w2, w3);
        for (int i = 0; i < s; ++i) {
            out[i] = w0 * vp[-3 * s + i] + w1 * vp[-2 * s + i] + w2 * vp[-s + i] + w3 * vp[i];
        }
        break;
    }
    case 0:
    default:
        for (int i = 0; i < s; ++i) out[i] = vp[i];
        break;
    }
}

} // namespace

GrnAnimSampler::GrnAnimSampler(const GrnAnimation& anim, const std::vector<GrnBone>& bones) {
    setAnimation(anim, bones);
}

void GrnAnimSampler::setAnimation(const GrnAnimation& anim, const std::vector<GrnBone>& bones) {
    clear();
    name_ = anim.name;
    duration_ = anim.duration;
    fps_ = anim.fps > 0.0f ? anim.fps : 30.0f;

    tracks_.reserve(anim.tracks.size());
    for (const auto& t : anim.tracks) {
        ConditionedTrack ct;
        ct.channel_id = t.channel_id;
        ct.bone_name = t.bone_name;

        // 1. Translations
        const auto& pos_times = !t.translation_times.empty() ? t.translation_times : t.times;
        if (!pos_times.empty() && !t.translations.empty()) {
            ct.pos_mode = t.position_interp_mode;
            ct.pos_times = buildPaddedTimes(pos_times);
            std::vector<float> flatPos;
            flatPos.reserve(t.translations.size() * 3);
            for (const auto& v : t.translations) {
                flatPos.push_back(v.x);
                flatPos.push_back(v.y);
                flatPos.push_back(v.z);
            }
            ct.pos_values = buildPaddedValues(flatPos, 3);
        }

        // 2. Rotations
        const auto& rot_times = !t.rotation_times.empty() ? t.rotation_times : t.times;
        if (!rot_times.empty() && !t.rotations.empty()) {
            ct.rot_mode = t.quaternion_interp_mode;
            ct.rot_times = buildPaddedTimes(rot_times);
            auto conditionedQuats = alignSourceRotationHemispheres(t.rotations);
            filterSourceRotationSpikes(conditionedQuats, rot_times);

            std::vector<float> flatRot;
            flatRot.reserve(conditionedQuats.size() * 4);
            for (const auto& q : conditionedQuats) {
                flatRot.push_back(q.x);
                flatRot.push_back(q.y);
                flatRot.push_back(q.z);
                flatRot.push_back(q.w);
            }
            ct.rot_values = buildPaddedValues(flatRot, 4);
        }

        // 3. Scale / Shears
        const auto& scale_times = !t.scale_shear_times.empty() ? t.scale_shear_times : t.times;
        if (!scale_times.empty() && !t.scale_shears.empty()) {
            ct.scale_mode = t.scale_shear_interp_mode;
            ct.scale_times = buildPaddedTimes(scale_times);
            std::vector<float> flatScale;
            flatScale.reserve(t.scale_shears.size() * 9);
            for (const auto& s : t.scale_shears) {
                for (int k = 0; k < 9; ++k) flatScale.push_back(s[k]);
            }
            ct.scale_values = buildPaddedValues(flatScale, 9);
        }

        tracks_.push_back(std::move(ct));
    }

    // Build bone -> track mapping
    bone_to_track_.assign(bones.size(), -1);

    // 1. Build bone name lookup maps (exact and case-insensitive)
    std::unordered_map<std::string, int32_t> nameMap;
    std::unordered_map<std::string, int32_t> lowerNameMap;
    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        if (!tracks_[ti].bone_name.empty()) {
            nameMap[tracks_[ti].bone_name] = static_cast<int32_t>(ti);
            std::string lower = tracks_[ti].bone_name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            lowerNameMap[lower] = static_cast<int32_t>(ti);
        }
    }

    // 2. Map bones to tracks: prioritize bone name over channel_id
    for (size_t bi = 0; bi < bones.size(); ++bi) {
        int32_t track_idx = -1;

        // Try exact name match
        auto it = nameMap.find(bones[bi].name);
        if (it != nameMap.end()) {
            track_idx = it->second;
        }

        // Try case-insensitive name match
        if (track_idx < 0) {
            std::string lower = bones[bi].name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto itLower = lowerNameMap.find(lower);
            if (itLower != lowerNameMap.end()) {
                track_idx = itLower->second;
            }
        }

        // Fallback by channel_id ONLY if track has no specific name or synthetic name ("Bone_<id>")
        if (track_idx < 0) {
            int32_t ch_id = static_cast<int32_t>(bi) + 1; // Granny channel_id convention
            for (size_t ti = 0; ti < tracks_.size(); ++ti) {
                if (tracks_[ti].channel_id == ch_id) {
                    if (tracks_[ti].bone_name.empty() ||
                        tracks_[ti].bone_name == ("Bone_" + std::to_string(ch_id))) {
                        track_idx = static_cast<int32_t>(ti);
                        break;
                    }
                }
            }
        }

        bone_to_track_[bi] = track_idx;
    }
}

void GrnAnimSampler::clear() {
    name_.clear();
    duration_ = 0.0f;
    fps_ = 30.0f;
    tracks_.clear();
    bone_to_track_.clear();
}

void GrnAnimSampler::sampleBone(size_t bone_idx,
                               const Vec3& rest_pos,
                               const Vec4& rest_rot,
                               const std::array<float, 9>& rest_scale,
                               float t,
                               Vec3& out_pos,
                               Vec4& out_rot,
                               std::array<float, 9>& out_scale_3x3) const {
    if (bone_idx >= bone_to_track_.size() || bone_to_track_[bone_idx] < 0) {
        out_pos = rest_pos;
        out_rot = rest_rot;
        out_scale_3x3 = rest_scale;
        return;
    }

    const auto& track = tracks_[bone_to_track_[bone_idx]];

    if (!track.pos_times.empty()) {
        sampleSplitComponent(track.pos_mode, track.pos_times.data(), static_cast<uint32_t>(track.pos_times.size()),
                             track.pos_values.data(), t, 3, &out_pos.x);
    } else {
        out_pos = rest_pos;
    }

    if (!track.rot_times.empty()) {
        sampleSplitComponent(track.rot_mode, track.rot_times.data(), static_cast<uint32_t>(track.rot_times.size()),
                             track.rot_values.data(), t, 4, &out_rot.x);
    } else {
        out_rot = rest_rot;
    }

    if (!track.scale_times.empty()) {
        sampleSplitComponent(track.scale_mode, track.scale_times.data(), static_cast<uint32_t>(track.scale_times.size()),
                             track.scale_values.data(), t, 9, out_scale_3x3.data());
    } else {
        out_scale_3x3 = rest_scale;
    }
}

} // namespace grn
