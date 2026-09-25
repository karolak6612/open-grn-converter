#include "skinning_engine.h"
#include "../../converter/converter.h"
#include <algorithm>
#include <cmath>

namespace grn {

QMatrix4x4 SkinningEngine::composeTransform(const Vec3& pos, const Vec4& rot, const std::array<float, 9>& scale_3x3) {
    float qx = rot.x, qy = rot.y, qz = rot.z, qw = rot.w;
    float lenSq = qx * qx + qy * qy + qz * qz + qw * qw;
    if (lenSq > 1e-8f) {
        float invLen = 1.0f / std::sqrt(lenSq);
        qx *= invLen; qy *= invLen; qz *= invLen; qw *= invLen;
    } else {
        qx = qy = qz = 0.0f; qw = 1.0f;
    }

    float xx = qx * qx, yy = qy * qy, zz = qz * qz;
    float xy = qx * qy, xz = qx * qz, yz = qy * qz;
    float wx = qw * qx, wy = qw * qy, wz = qw * qz;

    float R[3][3];
    R[0][0] = 1.0f - 2.0f * (yy + zz);
    R[0][1] = 2.0f * (xy - wz);
    R[0][2] = 2.0f * (xz + wy);

    R[1][0] = 2.0f * (xy + wz);
    R[1][1] = 1.0f - 2.0f * (xx + zz);
    R[1][2] = 2.0f * (yz - wx);

    R[2][0] = 2.0f * (xz - wy);
    R[2][1] = 2.0f * (yz + wx);
    R[2][2] = 1.0f - 2.0f * (xx + yy);

    float S[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            S[r][c] = scale_3x3[r * 3 + c];
        }
    }

    // RS = R * S
    float RS[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            RS[r][c] = R[r][0] * S[0][c] + R[r][1] * S[1][c] + R[r][2] * S[2][c];
        }
    }

    QMatrix4x4 mat;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            mat(r, c) = RS[r][c];
        }
    }
    mat(0, 3) = pos.x;
    mat(1, 3) = pos.y;
    mat(2, 3) = pos.z;

    mat(3, 0) = 0.0f;
    mat(3, 1) = 0.0f;
    mat(3, 2) = 0.0f;
    mat(3, 3) = 1.0f;

    return mat;
}

void SkinningEngine::computeInverseBindMatrices() {
    if (!model_ || model_->bones.empty()) {
        inverse_bind_matrices_.clear();
        world_matrices_.clear();
        return;
    }

    size_t numBones = model_->bones.size();
    world_matrices_.resize(numBones);
    inverse_bind_matrices_.resize(numBones);

    std::vector<QMatrix4x4> localRest(numBones);
    for (size_t bi = 0; bi < numBones; ++bi) {
        const auto& b = model_->bones[bi];
        localRest[bi] = composeTransform(b.position, b.rotation, b.scale_3x3);
    }

    for (size_t bi = 0; bi < numBones; ++bi) {
        int32_t p = model_->bones[bi].parent_index;
        if (p >= 0 && static_cast<size_t>(p) < bi) {
            world_matrices_[bi] = world_matrices_[p] * localRest[bi];
        } else {
            world_matrices_[bi] = localRest[bi];
        }

        bool invertible = false;
        QMatrix4x4 inv = world_matrices_[bi].inverted(&invertible);
        if (invertible) {
            inverse_bind_matrices_[bi] = inv;
        } else {
            inverse_bind_matrices_[bi] = QMatrix4x4();
        }
    }
}

void SkinningEngine::setModel(const GrnModel* model) {
    model_ = model;
    sampler_.clear();
    if (model_) {
        computeInverseBindMatrices();
        skinned_positions_.resize(model_->meshes.size());
        for (size_t mi = 0; mi < model_->meshes.size(); ++mi) {
            const auto& mesh = model_->meshes[mi];
            skinned_positions_[mi].resize(mesh.vertices.size());
            for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
                skinned_positions_[mi][vi] = QVector3D(mesh.vertices[vi].x, mesh.vertices[vi].y, mesh.vertices[vi].z);
            }
        }
    } else {
        inverse_bind_matrices_.clear();
        world_matrices_.clear();
        skin_matrices_.clear();
        skinned_positions_.clear();
    }
}

void SkinningEngine::setAnimation(const GrnAnimation* anim, const std::vector<GrnBone>* /*anim_bones*/) {
    if (anim && model_) {
        sampler_.setAnimation(*anim, model_->bones);
    } else {
        sampler_.clear();
    }
}

void SkinningEngine::clearAnimation() {
    sampler_.clear();
}

void SkinningEngine::evaluate(float time_seconds) {
    if (!model_) return;

    size_t numBones = model_->bones.size();
    if (numBones == 0) {
        // No bones: vertices remain in rest positions
        for (size_t mi = 0; mi < model_->meshes.size(); ++mi) {
            const auto& mesh = model_->meshes[mi];
            if (skinned_positions_[mi].size() != mesh.vertices.size()) {
                skinned_positions_[mi].resize(mesh.vertices.size());
            }
            for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
                skinned_positions_[mi][vi] = QVector3D(mesh.vertices[vi].x, mesh.vertices[vi].y, mesh.vertices[vi].z) * scale_;
            }
        }
        return;
    }

    world_matrices_.resize(numBones);
    skin_matrices_.resize(numBones);

    std::vector<QMatrix4x4> localMatrices(numBones);

    if (sampler_.hasAnimation()) {
        for (size_t bi = 0; bi < numBones; ++bi) {
            const auto& b = model_->bones[bi];
            Vec3 pos;
            Vec4 rot;
            std::array<float, 9> scale;
            sampler_.sampleBone(bi, b.position, b.rotation, b.scale_3x3, time_seconds, pos, rot, scale);
            localMatrices[bi] = composeTransform(pos, rot, scale);
        }
    } else {
        for (size_t bi = 0; bi < numBones; ++bi) {
            const auto& b = model_->bones[bi];
            localMatrices[bi] = composeTransform(b.position, b.rotation, b.scale_3x3);
        }
    }

    // World matrices
    for (size_t bi = 0; bi < numBones; ++bi) {
        int32_t p = model_->bones[bi].parent_index;
        if (p >= 0 && static_cast<size_t>(p) < bi) {
            world_matrices_[bi] = world_matrices_[p] * localMatrices[bi];
        } else {
            world_matrices_[bi] = localMatrices[bi];
        }

        if (bi < inverse_bind_matrices_.size()) {
            skin_matrices_[bi] = world_matrices_[bi] * inverse_bind_matrices_[bi];
        } else {
            skin_matrices_[bi] = QMatrix4x4();
        }
    }

    // Linear Blend Skinning
    for (size_t mi = 0; mi < model_->meshes.size(); ++mi) {
        const auto& mesh = model_->meshes[mi];
        auto& skinned = skinned_positions_[mi];
        if (skinned.size() != mesh.vertices.size()) {
            skinned.resize(mesh.vertices.size());
        }

        bool canSkin = !model_->bones.empty() && !mesh.weights.empty();

        for (size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
            QVector3D origPos(mesh.vertices[vi].x, mesh.vertices[vi].y, mesh.vertices[vi].z);
            if (!canSkin || vi >= mesh.weights.size()) {
                skinned[vi] = origPos;
                continue;
            }

            const auto& w = mesh.weights[vi];
            if (w.bone_indices.empty() || w.bone_weights.empty()) {
                skinned[vi] = origPos;
                continue;
            }

            QVector3D sumPos(0.0f, 0.0f, 0.0f);
            float totalWeight = 0.0f;

            for (size_t ki = 0; ki < w.bone_indices.size() && ki < w.bone_weights.size(); ++ki) {
                float weight = w.bone_weights[ki];
                if (weight <= 1e-6f) continue;

                int localBone = w.bone_indices[ki];
                int boneIdx = localBone;
                if (!mesh.bone_index_map.empty() && localBone >= 0 && static_cast<size_t>(localBone) < mesh.bone_index_map.size()) {
                    boneIdx = mesh.bone_index_map[localBone];
                }

                if (boneIdx >= 0 && static_cast<size_t>(boneIdx) < skin_matrices_.size()) {
                    sumPos += skin_matrices_[boneIdx].map(origPos) * weight;
                    totalWeight += weight;
                }
            }

            if (totalWeight > 1e-6f) {
                skinned[vi] = (sumPos / totalWeight) * scale_;
            } else {
                skinned[vi] = origPos * scale_;
            }
        }
    }
}

void SkinningEngine::computeBounds(QVector3D& out_min, QVector3D& out_max) const {
    out_min = QVector3D(1e9f, 1e9f, 1e9f);
    out_max = QVector3D(-1e9f, -1e9f, -1e9f);

    bool hasVerts = false;
    for (const auto& meshPos : skinned_positions_) {
        for (const auto& p : meshPos) {
            hasVerts = true;
            out_min.setX(std::min(out_min.x(), p.x()));
            out_min.setY(std::min(out_min.y(), p.y()));
            out_min.setZ(std::min(out_min.z(), p.z()));

            out_max.setX(std::max(out_max.x(), p.x()));
            out_max.setY(std::max(out_max.y(), p.y()));
            out_max.setZ(std::max(out_max.z(), p.z()));
        }
    }

    if (!hasVerts) {
        out_min = QVector3D(-10.0f, -10.0f, -10.0f);
        out_max = QVector3D(10.0f, 10.0f, 10.0f);
    }
}

} // namespace grn
