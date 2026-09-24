#include "viewport_widget.h"
#include "shader_gl.h"
#include "font_atlas.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>

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
        ui_program_ = createUIProgram(this);
        matcap_texture_ = createMatcapTexture(this);
        setupGridGeometry();
        setupSkeletonGeometry();
        setupUIGeometry();

        font_atlas_ = std::make_unique<FontAtlas>();
        font_atlas_->init(this, 11);
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
        -kExtent, 0.0f, -kExtent,
         kExtent, 0.0f, -kExtent,
         kExtent, 0.0f,  kExtent,

        -kExtent, 0.0f, -kExtent,
         kExtent, 0.0f,  kExtent,
        -kExtent, 0.0f,  kExtent
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

void ViewportWidget::setupSkeletonGeometry() {
    glGenBuffers(1, &skeleton_vbo_);
    glGenVertexArrays(1, &skeleton_vao_);

    glBindVertexArray(skeleton_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, skeleton_vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);
}

void ViewportWidget::setupUIGeometry() {
    glGenBuffers(1, &ui_vbo_);
    glGenVertexArrays(1, &ui_vao_);

    glBindVertexArray(ui_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), reinterpret_cast<void*>(2 * sizeof(float)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), reinterpret_cast<void*>(4 * sizeof(float)));

    glBindVertexArray(0);
}

void ViewportWidget::addQuad(float x0, float y0, float x1, float y1,
                             float u0, float v0, float u1, float v1,
                             const QVector4D& color) {
    float r = color.x(), g = color.y(), b = color.z(), a = color.w();
    ui_vertices_.push_back({ x0, y0, u0, v0, r, g, b, a });
    ui_vertices_.push_back({ x1, y0, u1, v0, r, g, b, a });
    ui_vertices_.push_back({ x1, y1, u1, v1, r, g, b, a });

    ui_vertices_.push_back({ x0, y0, u0, v0, r, g, b, a });
    ui_vertices_.push_back({ x1, y1, u1, v1, r, g, b, a });
    ui_vertices_.push_back({ x0, y1, u0, v1, r, g, b, a });
}

void ViewportWidget::addSolidRect(float x, float y, float w, float h, const QVector4D& color) {
    if (!font_atlas_) return;
    auto uv = font_atlas_->whiteUV();
    addQuad(x, y, x + w, y + h, uv[0], uv[1], uv[2], uv[3], color);
}

void ViewportWidget::addBadge(float x, float y, float w, float h, const QVector4D& bgColor, const QVector4D& borderColor) {
    addSolidRect(x, y, w, 1.0f, borderColor);
    addSolidRect(x, y + h - 1.0f, w, 1.0f, borderColor);
    addSolidRect(x, y + 1.0f, 1.0f, h - 2.0f, borderColor);
    addSolidRect(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f, borderColor);
    addSolidRect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, bgColor);
}

void ViewportWidget::addText(float x, float y, std::string_view text, const QVector4D& color) {
    if (!font_atlas_) return;
    float cur_x = x;
    for (char c : text) {
        const auto& m = font_atlas_->glyph(c);
        if (m.width > 0.0f && m.height > 0.0f) {
            addQuad(cur_x, y, cur_x + m.width, y + m.height, m.u0, m.v0, m.u1, m.v1, color);
        }
        cur_x += m.advance;
    }
}

void ViewportWidget::cleanupGL() {
    if (!gl_initialized_) return;
    if (font_atlas_) { font_atlas_->cleanup(); font_atlas_.reset(); }
    if (ui_vbo_) { glDeleteBuffers(1, &ui_vbo_); ui_vbo_ = 0; }
    if (ui_vao_) { glDeleteVertexArrays(1, &ui_vao_); ui_vao_ = 0; }
    if (ui_program_) { glDeleteProgram(ui_program_); ui_program_ = 0; }
    if (grid_vbo_) { glDeleteBuffers(1, &grid_vbo_); grid_vbo_ = 0; }
    if (grid_vao_) { glDeleteVertexArrays(1, &grid_vao_); grid_vao_ = 0; }
    if (skeleton_vbo_) { glDeleteBuffers(1, &skeleton_vbo_); skeleton_vbo_ = 0; }
    if (skeleton_vao_) { glDeleteVertexArrays(1, &skeleton_vao_); skeleton_vao_ = 0; }
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

void ViewportWidget::setShowSkeleton(bool enabled) {
    show_skeleton_ = enabled;
    update();
}

void ViewportWidget::setShowBoneLabels(bool enabled) {
    show_bone_labels_ = enabled;
    update();
}

void ViewportWidget::setBoneLabelsLOD(bool enabled) {
    if (bone_labels_lod_ != enabled) {
        bone_labels_lod_ = enabled;
        update();
    }
}

void ViewportWidget::setSelectedBone(int boneIndex) {
    selected_bone_index_ = boneIndex;
    update();
}

void ViewportWidget::setModelScale(float s) {
    skinning_.setScale(s);
    update();
}

void ViewportWidget::setZUpMode(bool enabled) {
    if (z_up_mode_ != enabled) {
        z_up_mode_ = enabled;
        frameBounds();
        update();
    }
}

QMatrix4x4 ViewportWidget::modelMatrix() const {
    QMatrix4x4 m;
    if (z_up_mode_) {
        // Rotate -90 degrees around X axis to convert Granny Z-up coordinates to OpenGL Y-up
        m.rotate(-90.0f, 1.0f, 0.0f, 0.0f);
    }
    return m;
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
    if (z_up_mode_) {
        QMatrix4x4 mm = modelMatrix();
        QVector3D pts[8] = {
            mm.map(QVector3D(minXYZ.x(), minXYZ.y(), minXYZ.z())),
            mm.map(QVector3D(maxXYZ.x(), minXYZ.y(), minXYZ.z())),
            mm.map(QVector3D(minXYZ.x(), maxXYZ.y(), minXYZ.z())),
            mm.map(QVector3D(maxXYZ.x(), maxXYZ.y(), minXYZ.z())),
            mm.map(QVector3D(minXYZ.x(), minXYZ.y(), maxXYZ.z())),
            mm.map(QVector3D(maxXYZ.x(), minXYZ.y(), maxXYZ.z())),
            mm.map(QVector3D(minXYZ.x(), maxXYZ.y(), maxXYZ.z())),
            mm.map(QVector3D(maxXYZ.x(), maxXYZ.y(), maxXYZ.z()))
        };
        minXYZ = maxXYZ = pts[0];
        for (int i = 1; i < 8; ++i) {
            minXYZ.setX(std::min(minXYZ.x(), pts[i].x()));
            minXYZ.setY(std::min(minXYZ.y(), pts[i].y()));
            minXYZ.setZ(std::min(minXYZ.z(), pts[i].z()));
            maxXYZ.setX(std::max(maxXYZ.x(), pts[i].x()));
            maxXYZ.setY(std::max(maxXYZ.y(), pts[i].y()));
            maxXYZ.setZ(std::max(maxXYZ.z(), pts[i].z()));
        }
    }
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

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); // Transparent grid does not occlude meshes in depth buffer
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

    glDepthMask(GL_TRUE); // Re-enable depth writes for opaque scene objects
}

void ViewportWidget::paintGL() {
    // Explicitly restore clean 3D pipeline states on every frame
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

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

    QMatrix4x4 modelMat = modelMatrix();

    // 3. Render Solid Meshes
    if (mesh_program_ && !meshes_.empty()) {
        glUseProgram(mesh_program_);
        GLint locVP = glGetUniformLocation(mesh_program_, "view_proj");
        GLint locV = glGetUniformLocation(mesh_program_, "view");
        GLint locM = glGetUniformLocation(mesh_program_, "model");
        GLint locMode = glGetUniformLocation(mesh_program_, "u_mode");
        GLint locHasTex = glGetUniformLocation(mesh_program_, "u_has_texture");
        GLint locLight = glGetUniformLocation(mesh_program_, "light_dir");
        GLint locTex = glGetUniformLocation(mesh_program_, "tex");
        GLint locMatcap = glGetUniformLocation(mesh_program_, "matcap_tex");

        glUniformMatrix4fv(locVP, 1, GL_FALSE, viewProj.constData());
        glUniformMatrix4fv(locV, 1, GL_FALSE, view.constData());
        glUniformMatrix4fv(locM, 1, GL_FALSE, modelMat.constData());
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
        GLint locWireM = glGetUniformLocation(wire_program_, "model");
        GLint locWireCol = glGetUniformLocation(wire_program_, "u_color");
        glUniformMatrix4fv(locWireVP, 1, GL_FALSE, viewProj.constData());
        glUniformMatrix4fv(locWireM, 1, GL_FALSE, modelMat.constData());
        glUniform4f(locWireCol, 0.95f, 0.55f, 0.15f, 0.75f);

        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0f, -1.0f);
        for (const auto& mesh : meshes_) {
            mesh->renderWireframe(this);
        }
        glDisable(GL_POLYGON_OFFSET_LINE);
    }

    // 5. Render Skeleton Overlay
    if (show_skeleton_) {
        renderSkeleton(viewProj);
    }

    // 6. Render Bone Labels Overlay (DoD GPU batch)
    if (show_bone_labels_ && skinning_.hasModel()) {
        renderBoneLabelsGPU(viewProj);
    }
}

void ViewportWidget::renderSkeleton(const QMatrix4x4& viewProj) {
    if (!skinning_.hasModel() || !wire_program_ || !skeleton_vao_) return;
    const auto* model = skinning_.model();
    if (!model || model->bones.empty()) return;

    const auto& world = skinning_.worldMatrices();
    if (world.empty()) return;

    std::vector<float> lineVerts;
    std::vector<float> selectedVerts;

    float markerSize = std::max(camera_.distance * 0.015f, 0.05f);

    for (size_t i = 0; i < model->bones.size() && i < world.size(); ++i) {
        const auto& b = model->bones[i];
        QVector3D pBone = world[i].map(QVector3D(0.0f, 0.0f, 0.0f));

        bool isSelected = (static_cast<int>(i) == selected_bone_index_);
        auto& targetList = isSelected ? selectedVerts : lineVerts;

        // Joint axis cross
        targetList.push_back(pBone.x() - markerSize); targetList.push_back(pBone.y()); targetList.push_back(pBone.z());
        targetList.push_back(pBone.x() + markerSize); targetList.push_back(pBone.y()); targetList.push_back(pBone.z());

        targetList.push_back(pBone.x()); targetList.push_back(pBone.y() - markerSize); targetList.push_back(pBone.z());
        targetList.push_back(pBone.x()); targetList.push_back(pBone.y() + markerSize); targetList.push_back(pBone.z());

        targetList.push_back(pBone.x()); targetList.push_back(pBone.y()); targetList.push_back(pBone.z() - markerSize);
        targetList.push_back(pBone.x()); targetList.push_back(pBone.y()); targetList.push_back(pBone.z() + markerSize);

        // Parent-child bone link
        if (b.parent_index >= 0 && static_cast<size_t>(b.parent_index) < world.size() && static_cast<size_t>(b.parent_index) != i) {
            QVector3D pParent = world[b.parent_index].map(QVector3D(0.0f, 0.0f, 0.0f));
            targetList.push_back(pParent.x()); targetList.push_back(pParent.y()); targetList.push_back(pParent.z());
            targetList.push_back(pBone.x()); targetList.push_back(pBone.y()); targetList.push_back(pBone.z());
        }
    }

    glUseProgram(wire_program_);
    GLint locWireVP = glGetUniformLocation(wire_program_, "view_proj");
    GLint locWireM = glGetUniformLocation(wire_program_, "model");
    GLint locWireCol = glGetUniformLocation(wire_program_, "u_color");
    glUniformMatrix4fv(locWireVP, 1, GL_FALSE, viewProj.constData());
    glUniformMatrix4fv(locWireM, 1, GL_FALSE, modelMatrix().constData());

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glBindVertexArray(skeleton_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, skeleton_vbo_);

    if (!lineVerts.empty()) {
        glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(float), lineVerts.data(), GL_DYNAMIC_DRAW);
        glUniform4f(locWireCol, 0.15f, 0.85f, 1.0f, 0.90f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size() / 3));
    }

    if (!selectedVerts.empty()) {
        glBufferData(GL_ARRAY_BUFFER, selectedVerts.size() * sizeof(float), selectedVerts.data(), GL_DYNAMIC_DRAW);
        glUniform4f(locWireCol, 1.0f, 0.80f, 0.10f, 1.0f);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(selectedVerts.size() / 3));
    }

    glBindVertexArray(0);
}

void ViewportWidget::renderBoneLabelsGPU(const QMatrix4x4& viewProj) {
    if (!font_atlas_ || !ui_program_ || !ui_vao_ || !skinning_.hasModel()) return;
    const auto* model = skinning_.model();
    if (!model || model->bones.empty()) return;
    const auto& world = skinning_.worldMatrices();
    if (world.empty()) return;

    size_t numBones = model->bones.size();

    // Count children to identify major articulation hubs
    std::vector<int> childCount(numBones, 0);
    for (size_t i = 0; i < numBones; ++i) {
        int32_t p = model->bones[i].parent_index;
        if (p >= 0 && static_cast<size_t>(p) < numBones && static_cast<size_t>(p) != i) {
            childCount[p]++;
        }
    }

    // Build candidate list of bone indices:
    std::vector<size_t> candidates;
    candidates.reserve(numBones);

    if (bone_labels_lod_) {
        // LOD mode (Declutter): Prioritize selected bone and articulation hubs
        if (selected_bone_index_ >= 0 && static_cast<size_t>(selected_bone_index_) < numBones) {
            candidates.push_back(static_cast<size_t>(selected_bone_index_));
        }
        std::vector<size_t> others;
        others.reserve(numBones);
        for (size_t i = 0; i < numBones; ++i) {
            if (static_cast<int>(i) != selected_bone_index_) {
                others.push_back(i);
            }
        }
        std::stable_sort(others.begin(), others.end(), [&](size_t a, size_t b) {
            return childCount[a] > childCount[b];
        });
        for (size_t idx : others) {
            candidates.push_back(idx);
        }
    } else {
        // Full mode (No LOD): Render all visible bones.
        // Put unselected bones first, selected bone last so its badge renders on top!
        for (size_t i = 0; i < numBones; ++i) {
            if (static_cast<int>(i) != selected_bone_index_) {
                candidates.push_back(i);
            }
        }
        if (selected_bone_index_ >= 0 && static_cast<size_t>(selected_bone_index_) < numBones) {
            candidates.push_back(static_cast<size_t>(selected_bone_index_));
        }
    }

    float w = static_cast<float>(width());
    float h = static_cast<float>(height());

    struct RectF {
        float x, y, w, h;
        bool intersects(const RectF& o) const {
            return (x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y);
        }
        RectF adjusted(float dx0, float dy0, float dx1, float dy1) const {
            return { x + dx0, y + dy0, w - dx0 + dx1, h - dy0 + dy1 };
        }
    };

    std::vector<RectF> placedRects;
    if (bone_labels_lod_) {
        placedRects.reserve(40);
    }
    constexpr size_t kMaxLabels = 35;

    ui_vertices_.clear();

    for (size_t boneIdx : candidates) {
        if (bone_labels_lod_ && placedRects.size() >= kMaxLabels && static_cast<int>(boneIdx) != selected_bone_index_) {
            break;
        }
        if (boneIdx >= world.size()) continue;

        QVector3D pBone = world[boneIdx].map(QVector3D(0.0f, 0.0f, 0.0f));
        QVector4D clip = viewProj * (modelMatrix() * QVector4D(pBone, 1.0f));
        if (clip.w() <= 0.05f) continue;

        float ndcX = clip.x() / clip.w();
        float ndcY = clip.y() / clip.w();
        float ndcZ = clip.z() / clip.w();

        if (ndcX < -1.02f || ndcX > 1.02f || ndcY < -1.02f || ndcY > 1.02f || ndcZ < -1.0f || ndcZ > 1.0f) {
            continue;
        }

        float sx = (ndcX * 0.5f + 0.5f) * w;
        float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * h;

        bool isSelected = (static_cast<int>(boneIdx) == selected_bone_index_);
        const auto& name = model->bones[boneIdx].name;
        int32_t bIndex = (model->bones[boneIdx].index != 0 || boneIdx == 0) ? model->bones[boneIdx].index : static_cast<int32_t>(boneIdx);
        std::string labelText = "[" + std::to_string(bIndex) + "] " + (name.empty() ? "?" : name);

        float textW = font_atlas_->measureText(labelText);
        float textH = font_atlas_->lineHeight();
        bool onRight = (sx >= w * 0.5f);
        float rectX = onRight ? (sx + 4.0f) : (sx - (textW + 10.0f) - 4.0f);
        float badgeH = textH + 4.0f;
        float rectY = sy - badgeH * 0.5f;
        float badgeW = textW + 10.0f;

        RectF bgRect{ rectX, rectY, badgeW, badgeH };

        if (bone_labels_lod_ && !isSelected) {
            RectF padded = bgRect.adjusted(-2.0f, -2.0f, 2.0f, 2.0f);
            bool collides = false;
            for (const auto& r : placedRects) {
                if (r.intersects(padded)) {
                    collides = true;
                    break;
                }
            }
            if (collides) continue;
        }

        if (bone_labels_lod_) {
            placedRects.push_back(bgRect);
        }

        QVector4D bgColor = isSelected ? QVector4D(0.90f, 0.63f, 0.08f, 0.90f) : QVector4D(0.08f, 0.10f, 0.14f, 0.75f);
        QVector4D borderColor = isSelected ? QVector4D(1.0f, 1.0f, 1.0f, 1.0f) : QVector4D(0.39f, 0.70f, 1.0f, 0.70f);
        QVector4D textColor = isSelected ? QVector4D(0.0f, 0.0f, 0.0f, 1.0f) : QVector4D(0.94f, 0.96f, 1.0f, 1.0f);

        addBadge(rectX, rectY, badgeW, badgeH, bgColor, borderColor);
        addText(rectX + 5.0f, rectY + 1.0f, labelText, textColor);
    }

    if (ui_vertices_.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(ui_program_);
    GLint locVp = glGetUniformLocation(ui_program_, "u_viewport_size");
    glUniform2f(locVp, w, h);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_atlas_->textureId());
    GLint locAtlas = glGetUniformLocation(ui_program_, "u_atlas");
    glUniform1i(locAtlas, 0);

    glBindVertexArray(ui_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
    glBufferData(GL_ARRAY_BUFFER, ui_vertices_.size() * sizeof(UIVertex), ui_vertices_.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(ui_vertices_.size()));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
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
