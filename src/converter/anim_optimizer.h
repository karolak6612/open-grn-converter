#pragma once

#include "../core/grn_types.h"
#include <vector>
#include <cstddef>

namespace grn {

struct AnimOptimizationOptions {
    bool prune_static_tracks = true;         // Omit tracks where keyframes match bone rest pose
    bool collapse_constant_keyframes = true; // Collapse identical keyframe sequences down to 1 keyframe
    bool decimate_keyframes = false;         // Disabled by default: non-uniform chord decimation causes FK chain accumulation & bending distortion!
    bool loop_safe = true;                   // Lock boundary tangents (t0, t1, tN-1, tN) and snap loop endpoints
    float pos_tolerance = 0.002f;            // Position distance tolerance (0.002 game units)
    float rot_tolerance = 0.001f;            // Rotation quaternion dot tolerance (1 - abs(q1.dot(q2)))
    float scale_tolerance = 0.001f;          // Scale/shear matrix element tolerance
    float target_fps = 0.0f;                 // Target uniform frame rate (0.0 = keep original, e.g. 30, 20, 15)
    float min_rotation_deg = 0.0f;           // Minimum total rotation range in degrees to retain track (0.0 = keep all)
};

struct AnimOptimizationStats {
    size_t original_tracks = 0;
    size_t optimized_tracks = 0;
    size_t original_keyframes = 0;
    size_t optimized_keyframes = 0;
};

/**
 * @brief Optimizes a Granny animation by collapsing constant keyframes,
 *        decimating redundant linear keyframes, and pruning static rest-pose tracks.
 */
AnimOptimizationStats optimize_animation(GrnAnimation& anim,
                                        const std::vector<GrnBone>& bones,
                                        const AnimOptimizationOptions& opts);

/**
 * @brief Optimizes all animations in a model.
 */
void optimize_all_animations(std::vector<GrnAnimation>& anims,
                             const std::vector<GrnBone>& bones,
                             const AnimOptimizationOptions& opts);

} // namespace grn
