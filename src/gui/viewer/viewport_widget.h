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
#include "font_atlas.h"

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
    void playAnimation(const GrnAnimation* anim, const std::vector<GrnBone>* anim_bones = nullptr);
    void stopAnimation();

    void setPlaying(bool playing);
    void setTime(float t);
    void setLooping(bool looping);
    void setPlaybackSpeed(float spd);
    void setShadingMode(ShadingMode mode);
    void setWireframe(bool enabled);
    void setShowGrid(bool enabled);
    void setShowSkeleton(bool enabled);
    void setShowBoneLabels(bool enabled);
    void setBoneLabelsLOD(bool enabled);
    void setSelectedBone(int boneIndex);
    bool showSkeleton() const { return show_skeleton_; }
    bool showBoneLabels() const { return show_bone_labels_; }
    bool boneLabelsLOD() const { return bone_labels_lod_; }
    int selectedBone() const { return selected_bone_index_; }
    void frameBounds();
    void resetCamera();
    void setModelScale(float s);
    float modelScale() const { return skinning_.scale(); }
    void setZUpMode(bool enabled);
    bool isZUpMode() const { return z_up_mode_; }
    QMatrix4x4 modelMatrix() const;

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
    struct UIVertex {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    bool gl_initialized_{ false };
    const GrnModel* pending_model_{ nullptr };
    const GrnAnimation* pending_anim_{ nullptr };
    const std::vector<GrnBone>* pending_anim_bones_{ nullptr };

    OrbitCamera camera_;
    SkinningEngine skinning_;
    std::vector<std::unique_ptr<MeshGL>> meshes_;
    std::vector<GLuint> gl_textures_;

    GLuint mesh_program_{ 0 };
    GLuint wire_program_{ 0 };
    GLuint grid_program_{ 0 };
    GLuint ui_program_{ 0 };
    GLuint matcap_texture_{ 0 };

    GLuint grid_vao_{ 0 };
    GLuint grid_vbo_{ 0 };
    GLuint skeleton_vao_{ 0 };
    GLuint skeleton_vbo_{ 0 };
    GLuint ui_vao_{ 0 };
    GLuint ui_vbo_{ 0 };

    std::unique_ptr<FontAtlas> font_atlas_;
    std::vector<UIVertex> ui_vertices_;

    ShadingMode shading_mode_{ ShadingMode::TexturedLit };
    bool wireframe_{ false };
    bool show_grid_{ true };
    bool show_skeleton_{ false };
    bool show_bone_labels_{ false };
    bool bone_labels_lod_{ false };
    int selected_bone_index_{ -1 };

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
    bool z_up_mode_{ false };

    void setupGridGeometry();
    void renderGrid(const QMatrix4x4& viewProj);
    void setupSkeletonGeometry();
    void renderSkeleton(const QMatrix4x4& viewProj);
    void setupUIGeometry();
    void addQuad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const QVector4D& color);
    void addSolidRect(float x, float y, float w, float h, const QVector4D& color);
    void addBadge(float x, float y, float w, float h, const QVector4D& bgColor, const QVector4D& borderColor);
    void addText(float x, float y, std::string_view text, const QVector4D& color);
    void renderBoneLabelsGPU(const QMatrix4x4& viewProj);
    void cleanupGL();
};

} // namespace grn
