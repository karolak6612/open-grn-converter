#pragma once

#include <QVector3D>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>

namespace grn {

/**
 * @brief Orbit camera for Granny Z-up coordinate space.
 */
class OrbitCamera {
public:
    float yaw{ 0.6f };
    float pitch{ 0.35f };
    float distance{ 100.0f };
    QVector3D target{ 0.0f, 0.0f, 0.0f };

    void frameBounds(const QVector3D& minXYZ, const QVector3D& maxXYZ);
    QVector3D eye() const;
    QMatrix4x4 view() const;
    QMatrix4x4 viewProj(float aspect) const;

    void orbit(float deltaX, float deltaY);
    void pan(float deltaX, float deltaY);
    void zoom(float factor);
    void reset();
};

} // namespace grn
