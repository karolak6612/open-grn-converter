#include "viewport_widget.h"
#include "shader_gl.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

namespace grn {

ViewportWidget::ViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    frame_timer_ = new QTimer(this);
    QObject::connect(frame_timer_, &QTimer::timeout, this, [this]() {
        update();
    });
    frame_timer_->start(16); // ~60 FPS
    elapsed_timer_.start();
}

ViewportWidget::~ViewportWidget() {
    if (gl_initialized_ && isValid()) {
        makeCurrent();
        cleanupGL();
        doneCurrent();
    }
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();
    gl_initialized_ = true;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    try {
        mesh_program_ = createMeshProgram(this);
        wire_program_ = createWireProgram(this);
        grid_program_ = createGridProgram(this);
        matcap_texture_ = createMatcapTexture(this);
        setupGridGeometry();
    } catch (...) {
        // Shaders fallback
    }

    if (pending_model_) {
        const GrnModel* m = pending_model_;
        const GrnAnimation* a = pending_anim_;
        pending_model_ = nullptr;
        pending_anim_ = nullptr;
        loadModel(m);
        if (a) {
            playAnimation(a);
        }
    }
}

void ViewportWidget::setupGridGeometry() {
    constexpr float kExtent = 5000.0f;
    const float quadVertices[] = {
        -kExtent, -kExtent, 0.0f,
         kExtent, -kExtent, 0.0f,
         kExtent,  kExtent, 0.0f,

        -kExtent, -kExtent, 0.0f,
         kExtent,  kExtent, 0.0f,
        -kExtent,  kExtent, 0.0f
    };

    glGenBuffers(1, &grid_vbo_);
    glGenVertexArrays(1, &grid_vao_);

    glBindVertexArray(grid_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, grid_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);
}

void ViewportWidget::cleanupGL() {
    if (!gl_initialized_) return;
    if (grid_vbo_) { glDeleteBuffers(1, &grid_vbo_); grid_vbo_ = 0; }
    if (grid_vao_) { glDeleteVertexArrays(1, &grid_vao_); grid_vao_ = 0; }
    if (matcap_texture_) { glDeleteTextures(1, &matcap_texture_); matcap_texture_ = 0; }
    if (mesh_program_) { glDeleteProgram(mesh_program_); mesh_program_ = 0; }
    if (wire_program_) { glDeleteProgram(wire_program_); wire_program_ = 0; }
    if (grid_program_) { glDeleteProgram(grid_program_); grid_program_ = 0; }

    for (GLuint tex : gl_textures_) {
        if (tex) glDeleteTextures(1, &tex);
    }
    gl_textures_.clear();
    meshes_.clear();
    gl_initialized_ = false;
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ViewportWidget::loadModel(const GrnModel* model) {
    if (!gl_initialized_) {
        pending_model_ = model;
        skinning_.setModel(model);
        current_time_ = 0.0f;
        is_playing_ = false;
        emit playbackStateChanged(false);
        emit playbackTimeChanged(0.0f, skinning_.duration());
        if (model) {
            size_t totalVerts = 0;
            for (const auto& m : model->meshes) totalVerts += m.vertices.size();
            emit modelLoaded(model->meshes.size(), totalVerts, model->bones.size());
        } else {
            emit modelLoaded(0, 0, 0);
        }
        return;
    }

    makeCurrent();

    for (GLuint tex : gl_textures_) {
        if (tex) glDeleteTextures(1, &tex);
    }
    gl_textures_.clear();
    meshes_.clear();

    skinning_.setModel(model);

    if (model) {
        // 1. Upload textures
        for (const auto& tex : model->textures) {
            if (!tex.decoded_rgba.empty() && tex.width > 0 && tex.height > 0) {
                GLuint texId = 0;
                glGenTextures(1, &texId);
                glBindTexture(GL_TEXTURE_2D, texId);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.decoded_rgba.data());
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                gl_textures_.push_back(texId);
            } else {
                gl_textures_.push_back(0);
            }
        }

        // 2. Build GPU meshes
        size_t totalVerts = 0;
        for (size_t mi = 0; mi < model->meshes.size(); ++mi) {
            const auto& meshData = model->meshes[mi];
            auto meshGl = std::make_unique<MeshGL>(this, meshData);

            // Bind textures to material passes
            for (const auto& grp : meshData.tri_groups) {
                if (grp.material_index >= 0 && static_cast<size_t>(grp.material_index) < model->materials.size()) {
                    int texIdx = model->materials[grp.material_index].diffuse_texture_index;
                    if (texIdx >= 0 && static_cast<size_t>(texIdx) < gl_textures_.size() && gl_textures_[texIdx] != 0) {
                        meshGl->setTexture(grp.material_index, gl_textures_[texIdx]);
                    }
                }
            }
            if (meshData.material_index >= 0 && static_cast<size_t>(meshData.material_index) < model->materials.size()) {
                int texIdx = model->materials[meshData.material_index].diffuse_texture_index;
                if (texIdx >= 0 && static_cast<size_t>(texIdx) < gl_textures_.size() && gl_textures_[texIdx] != 0) {
                    meshGl->setTexture(meshData.material_index, gl_textures_[texIdx]);
                }
            }

            totalVerts += meshGl->vertexCount();
            meshes_.push_back(std::move(meshGl));
        }

        frameBounds();
        emit modelLoaded(model->meshes.size(), totalVerts, model->bones.size());
    } else {
        emit modelLoaded(0, 0, 0);
    }

    current_time_ = 0.0f;
    is_playing_ = false;
    emit playbackStateChanged(false);
    emit playbackTimeChanged(0.0f, skinning_.duration());

    doneCurrent();
    update();
}

void ViewportWidget::playAnimation(const GrnAnimation* anim) {
    pending_anim_ = anim;
    skinning_.setAnimation(anim);
    current_time_ = 0.0f;
    is_playing_ = (anim != nullptr && skinning_.duration() > 0.0f);
    emit playbackStateChanged(is_playing_);
    emit playbackTimeChanged(0.0f, skinning_.duration());
    if (gl_initialized_) {
        update();
    }
}

void ViewportWidget::stopAnimation() {
    pending_anim_ = nullptr;
    skinning_.clearAnimation();
    current_time_ = 0.0f;
    is_playing_ = false;
    emit playbackStateChanged(false);
    emit playbackTimeChanged(0.0f, 0.0f);
    if (gl_initialized_) {
        update();
    }
}

void ViewportWidget::setPlaying(bool playing) {
    is_playing_ = playing;
    emit playbackStateChanged(is_playing_);
    update();
}

void ViewportWidget::setTime(float t) {
    current_time_ = std::clamp(t, 0.0f, skinning_.duration());
    update();
}

void ViewportWidget::setLooping(bool looping) {
    is_looping_ = looping;
}

void ViewportWidget::setPlaybackSpeed(float spd) {
    playback_speed_ = spd;
}

void ViewportWidget::setShadingMode(ShadingMode mode) {
    shading_mode_ = mode;
    update();
}

void ViewportWidget::setWireframe(bool enabled) {
    wireframe_ = enabled;
    update();
}

void ViewportWidget::setShowGrid(bool enabled) {
    show_grid_ = enabled;
    update();
}

void ViewportWidget::setModelScale(float s) {
    skinning_.setScale(s);
    update();
}

void ViewportWidget::syncCamera(const OrbitCamera& cam) {
    camera_.target = cam.target;
    camera_.yaw = cam.yaw;
    camera_.pitch = cam.pitch;
    camera_.distance = cam.distance;
    update();
}

void ViewportWidget::frameBounds() {
    QVector3D minXYZ, maxXYZ;
    skinning_.computeBounds(minXYZ, maxXYZ);
    camera_.frameBounds(minXYZ, maxXYZ);
    emit cameraChanged(camera_);
    update();
}

void ViewportWidget::resetCamera() {
    camera_.reset();
    frameBounds();
    emit cameraChanged(camera_);
    update();
}

void ViewportWidget::renderGrid(const QMatrix4x4& viewProj) {
    if (!grid_program_ || !grid_vao_) return;

    glUseProgram(grid_program_);
    GLint locVP = glGetUniformLocation(grid_program_, "view_proj");
    GLint locCam = glGetUniformLocation(grid_program_, "cam_pos");
    GLint locCell = glGetUniformLocation(grid_program_, "cell");

    QVector3D eye = camera_.eye();
    glUniformMatrix4fv(locVP, 1, GL_FALSE, viewProj.constData());
    glUniform3f(locCam, eye.x(), eye.y(), eye.z());
    glUniform1f(locCell, std::max(camera_.distance * 0.05f, 1.0f));

    glBindVertexArray(grid_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void ViewportWidget::paintGL() {
    float dt = elapsed_timer_.restart() / 1000.0f;
    if (dt > 0.1f) dt = 0.1f;

    if (is_playing_ && skinning_.hasAnimation()) {
        float dur = skinning_.duration();
        if (dur > 0.0f) {
            current_time_ += dt * playback_speed_;
            if (current_time_ >= dur) {
                if (is_looping_) {
                    current_time_ = std::fmod(current_time_, dur);
                } else {
                    current_time_ = dur;
                    is_playing_ = false;
                    emit playbackStateChanged(false);
                }
            }
            emit playbackTimeChanged(current_time_, dur);
        }
    }

    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = width() > 0 && height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    QMatrix4x4 view = camera_.view();
    QMatrix4x4 viewProj = camera_.viewProj(aspect);

    // 1. Render Grid
    if (show_grid_) {
        renderGrid(viewProj);
    }

    // 2. Evaluate skinning and update GPU vertex buffers
    skinning_.evaluate(current_time_);
    const auto& positions = skinning_.skinnedPositions();
    for (size_t mi = 0; mi < meshes_.size() && mi < positions.size(); ++mi) {
        meshes_[mi]->updatePositions(this, positions[mi], true);
    }

    // 3. Render Solid Meshes
    if (mesh_program_ && !meshes_.empty()) {
        glUseProgram(mesh_program_);
        GLint locVP = glGetUniformLocation(mesh_program_, "view_proj");
        GLint locV = glGetUniformLocation(mesh_program_, "view");
        GLint locMode = glGetUniformLocation(mesh_program_, "u_mode");
        GLint locHasTex = glGetUniformLocation(mesh_program_, "u_has_texture");
        GLint locLight = glGetUniformLocation(mesh_program_, "light_dir");
        GLint locTex = glGetUniformLocation(mesh_program_, "tex");
        GLint locMatcap = glGetUniformLocation(mesh_program_, "matcap_tex");

        glUniformMatrix4fv(locVP, 1, GL_FALSE, viewProj.constData());
        glUniformMatrix4fv(locV, 1, GL_FALSE, view.constData());
        glUniform1i(locMode, static_cast<int>(shading_mode_));
        glUniform3f(locLight, 0.35f, 0.60f, 0.70f);
        glUniform1i(locTex, 0);
        glUniform1i(locMatcap, 1);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, matcap_texture_);

        for (const auto& mesh : meshes_) {
            mesh->renderSolid(this, locHasTex);
        }
    }

    // 4. Render Wireframe Overlay
    if (wireframe_ && wire_program_ && !meshes_.empty()) {
        glUseProgram(wire_program_);
        GLint locWireVP = glGetUniformLocation(wire_program_, "view_proj");
        GLint locWireCol = glGetUniformLocation(wire_program_, "u_color");
        glUniformMatrix4fv(locWireVP, 1, GL_FALSE, viewProj.constData());
        glUniform4f(locWireCol, 0.95f, 0.55f, 0.15f, 0.75f);

        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        for (const auto& mesh : meshes_) {
            mesh->renderWireframe(this);
        }
        glDisable(GL_POLYGON_OFFSET_LINE);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    last_mouse_pos_ = event->pos();
    if (event->button() == Qt::LeftButton) {
        is_orbiting_ = true;
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        is_panning_ = true;
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    float dx = static_cast<float>(event->pos().x() - last_mouse_pos_.x());
    float dy = static_cast<float>(event->pos().y() - last_mouse_pos_.y());
    last_mouse_pos_ = event->pos();

    if (is_orbiting_) {
        camera_.orbit(dx, dy);
        emit cameraChanged(camera_);
        update();
    } else if (is_panning_) {
        camera_.pan(dx, dy);
        emit cameraChanged(camera_);
        update();
    }
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_orbiting_ = false;
    } else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        is_panning_ = false;
    }
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    float delta = event->angleDelta().y();
    if (delta != 0.0f) {
        float factor = (delta > 0.0f) ? 0.88f : 1.14f;
        camera_.zoom(factor);
        emit cameraChanged(camera_);
        update();
    }
}

} // namespace grn
