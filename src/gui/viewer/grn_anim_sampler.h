#pragma once

#include "../../core/grn_types.h"
#include <vector>
#include <array>
#include <string>

namespace grn {

/**
 * @brief Preconditioned curve data for fast, DLL-accurate Granny 1.2b spline evaluation.
 */
struct ConditionedTrack {
    int32_t channel_id{ 0 };
    std::string bone_name;

    int32_t pos_mode{ 1 };
    std::vector<float> pos_times;
    std::vector<float> pos_values; // stride 3

    int32_t rot_mode{ 1 };
    std::vector<float> rot_times;
    std::vector<float> rot_values; // stride 4

    int32_t scale_mode{ 1 };
    std::vector<float> scale_times;
    std::vector<float> scale_values; // stride 9
};

/**
 * @brief Evaluator for a loaded GrnAnimation. Pre-conditions curves once on load.
 */
class GrnAnimSampler {
public:
    GrnAnimSampler() = default;
    GrnAnimSampler(const GrnAnimation& anim, const std::vector<GrnBone>& bones);

    void setAnimation(const GrnAnimation& anim, const std::vector<GrnBone>& bones);
    void clear();

    bool hasAnimation() const { return !tracks_.empty(); }
    float duration() const { return duration_; }
    float fps() const { return fps_; }
    const std::string& name() const { return name_; }
    bool hasTrackForBone(size_t bone_idx) const { return bone_idx < bone_to_track_.size() && bone_to_track_[bone_idx] >= 0; }

    void sampleBone(size_t bone_idx,
                    const Vec3& rest_pos,
                    const Vec4& rest_rot,
                    const std::array<float, 9>& rest_scale,
                    float t,
                    Vec3& out_pos,
                    Vec4& out_rot,
                    std::array<float, 9>& out_scale_3x3) const;

private:
    std::string name_;
    float duration_{ 0.0f };
    float fps_{ 30.0f };
    std::vector<ConditionedTrack> tracks_;
    std::vector<int32_t> bone_to_track_;
};

} // namespace grn
