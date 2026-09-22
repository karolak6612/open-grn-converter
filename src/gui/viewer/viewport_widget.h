#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#include <vector>

#include "camera.h"
#include "skinning_engine.h"
#include "mesh_gl.h"

namespace grn {

enum class ShadingMode {
    TexturedLit = 0,
    UnlitTextured = 1,
    ClayMatcap = 2,
    Normals = 3
};

class ViewportWidget : public QOpenGLWidget, public QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    void loadModel(const GrnModel* model);
    void playAnimation(const GrnAnimation* anim);
    void stopAnimation();

    void setPlaying(bool playing);
    void setTime(float t);
    void setLooping(bool looping);
    void setPlaybackSpeed(float spd);
    void setShadingMode(ShadingMode mode);
    void setWireframe(bool enabled);
    void setShowGrid(bool enabled);
    void frameBounds();
    void resetCamera();
    void setModelScale(float s);
    float modelScale() const { return skinning_.scale(); }

    const OrbitCamera& camera() const { return camera_; }
    void syncCamera(const OrbitCamera& cam);

    void setViewportLabel(const QString& label) { viewport_label_ = label; }
    QString viewportLabel() const { return viewport_label_; }

    bool isPlaying() const { return is_playing_; }
    bool isLooping() const { return is_looping_; }
    float currentTime() const { return current_time_; }
    float duration() const { return skinning_.duration(); }
    bool hasAnimation() const { return skinning_.hasAnimation(); }

signals:
    void cameraChanged(const OrbitCamera& cam);
    void playbackTimeChanged(float curTime, float duration);
    void playbackStateChanged(bool playing);
    void modelLoaded(size_t meshCount, size_t vertCount, size_t boneCount);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool gl_initialized_{ false };
    const GrnModel* pending_model_{ nullptr };
    const GrnAnimation* pending_anim_{ nullptr };

    OrbitCamera camera_;
    SkinningEngine skinning_;
    std::vector<std::unique_ptr<MeshGL>> meshes_;
    std::vector<GLuint> gl_textures_;

    GLuint mesh_program_{ 0 };
    GLuint wire_program_{ 0 };
    GLuint grid_program_{ 0 };
    GLuint matcap_texture_{ 0 };

    GLuint grid_vao_{ 0 };
    GLuint grid_vbo_{ 0 };

    ShadingMode shading_mode_{ ShadingMode::TexturedLit };
    bool wireframe_{ false };
    bool show_grid_{ true };

    bool is_playing_{ false };
    bool is_looping_{ true };
    float playback_speed_{ 1.0f };
    float current_time_{ 0.0f };

    QTimer* frame_timer_{ nullptr };
    QElapsedTimer elapsed_timer_;
    QPoint last_mouse_pos_;
    bool is_orbiting_{ false };
    bool is_panning_{ false };
    QString viewport_label_;

    void setupGridGeometry();
    void renderGrid(const QMatrix4x4& viewProj);
    void cleanupGL();
};

} // namespace grn
