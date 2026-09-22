#include "camera.h"

namespace grn {

void OrbitCamera::frameBounds(const QVector3D& minXYZ, const QVector3D& maxXYZ) {
    target = (minXYZ + maxXYZ) * 0.5f;
    float extent = (maxXYZ - minXYZ).length();
    distance = std::max(extent * 2.0f, 1.0f);
}

QVector3D OrbitCamera::eye() const {
    float cp = std::cos(pitch), sp = std::sin(pitch);
    float cy = std::cos(yaw), sy = std::sin(yaw);
    QVector3D offset(cp * sy, cp * cy, sp);
    return target + offset * distance;
}

QMatrix4x4 OrbitCamera::view() const {
    QMatrix4x4 v;
    v.lookAt(eye(), target, QVector3D(0.0f, 0.0f, 1.0f));
    return v;
}

QMatrix4x4 OrbitCamera::viewProj(float aspect) const {
    float nearPlane = std::max(distance * 0.01f, 0.1f);
    float farPlane = distance * 20.0f + 1000.0f;
    QMatrix4x4 proj;
    proj.perspective(50.0f, aspect, nearPlane, farPlane);
    return proj * view();
}

void OrbitCamera::orbit(float deltaX, float deltaY) {
    yaw -= deltaX * 0.006f;
    pitch = std::clamp(pitch - deltaY * 0.006f, -1.45f, 1.45f);
}

void OrbitCamera::pan(float deltaX, float deltaY) {
    QVector3D fwd = (target - eye()).normalized();
    QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0.0f, 0.0f, 1.0f)).normalized();
    QVector3D up = QVector3D::crossProduct(right, fwd).normalized();
    float panScale = distance * 0.0015f;
    target += -right * (deltaX * panScale) + up * (deltaY * panScale);
}

void OrbitCamera::zoom(float factor) {
    distance = std::max(distance * factor, 0.5f);
}

void OrbitCamera::reset() {
    yaw = 0.6f;
    pitch = 0.35f;
}

} // namespace grn
