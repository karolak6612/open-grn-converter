#pragma once

#include "../../core/grn_types.h"
#include "grn_anim_sampler.h"
#include <QMatrix4x4>
#include <QVector3D>
#include <vector>

namespace grn {

/**
 * @brief Performs bone hierarchy evaluation and linear blend skinning for GRN and GLB models.
 */
class SkinningEngine {
public:
    SkinningEngine() = default;

    void setModel(const GrnModel* model);
    void setAnimation(const GrnAnimation* anim);
    void clearAnimation();

    void evaluate(float time_seconds);

    void setScale(float s) { scale_ = (s > 1e-6f ? s : 1.0f); }
    float scale() const { return scale_; }

    const std::vector<std::vector<QVector3D>>& skinnedPositions() const { return skinned_positions_; }
    const std::vector<QMatrix4x4>& worldMatrices() const { return world_matrices_; }
    const std::vector<QMatrix4x4>& skinMatrices() const { return skin_matrices_; }

    void computeBounds(QVector3D& out_min, QVector3D& out_max) const;

    bool hasModel() const { return model_ != nullptr; }
    bool hasAnimation() const { return sampler_.hasAnimation(); }
    float duration() const { return sampler_.duration(); }
    float fps() const { return sampler_.fps(); }
    const std::string& animationName() const { return sampler_.name(); }

private:
    const GrnModel* model_{ nullptr };
    GrnAnimSampler sampler_;

    std::vector<QMatrix4x4> inverse_bind_matrices_;
    std::vector<QMatrix4x4> world_matrices_;
    std::vector<QMatrix4x4> skin_matrices_;
    std::vector<std::vector<QVector3D>> skinned_positions_;
    float scale_{ 1.0f };

    void computeInverseBindMatrices();
    static QMatrix4x4 composeTransform(const Vec3& pos, const Vec4& rot, const std::array<float, 9>& scale_3x3);
};

} // namespace grn
