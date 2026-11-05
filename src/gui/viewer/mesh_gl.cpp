#include "mesh_gl.h"
#include <algorithm>
#include <cmath>

namespace grn {

MeshGL::MeshGL(QOpenGLFunctions_3_3_Core* gl, const GrnMesh& mesh)
    : gl_(gl) {
    const bool has_uv = !mesh.uvs.empty();

    if (!mesh.tri_groups.empty()) {
        for (const auto& group : mesh.tri_groups) {
            Batch batch;
            batch.material_index = group.material_index;
            batch.first_corner = static_cast<int>(corner_position_indices_.size());
            for (size_t fi = 0; fi < group.faces.size(); ++fi) {
                const auto& f = group.faces[fi];
                const auto& uv_idx = (fi < group.face_uvs.size()) ? group.face_uvs[fi] : f;
                for (int k = 0; k < 3; ++k) {
                    corner_position_indices_.push_back(f[k]);
                    uint32_t ui = uv_idx[k];
                    if (has_uv && ui < mesh.uvs.size()) {
                        corner_uvs_.push_back(mesh.uvs[ui]);
                    } else {
                        corner_uvs_.push_back(Vec2{ 0.0f, 0.0f });
                    }
                }
            }
            batch.corner_count = static_cast<int>(corner_position_indices_.size()) - batch.first_corner;
            if (batch.corner_count > 0) {
                batches_.push_back(batch);
            }
        }
    }

    // Fallback: if materials didn't cover faces or tri_groups empty
    if (corner_position_indices_.empty() && !mesh.faces.empty()) {
        Batch batch;
        batch.material_index = mesh.material_index;
        batch.first_corner = 0;
        for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
            const auto& f = mesh.faces[fi];
            const auto& uv_idx = (fi < mesh.face_uvs.size()) ? mesh.face_uvs[fi] : f;
            for (int k = 0; k < 3; ++k) {
                corner_position_indices_.push_back(f[k]);
                uint32_t ui = uv_idx[k];
                if (has_uv && ui < mesh.uvs.size()) {
                    corner_uvs_.push_back(mesh.uvs[ui]);
                } else {
                    corner_uvs_.push_back(Vec2{ 0.0f, 0.0f });
                }
            }
        }
        batch.corner_count = static_cast<int>(corner_position_indices_.size());
        if (batch.corner_count > 0) {
            batches_.push_back(batch);
        }
    }

    corner_count_ = corner_position_indices_.size();

    gl_->glGenBuffers(1, &vbo_);
    gl_->glGenBuffers(1, &line_ibo_);
    gl_->glGenVertexArrays(1, &vao_);

    gl_->glBindVertexArray(vao_);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl_->glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(std::max<size_t>(corner_count_, 1) * 8 * sizeof(float)),
                     nullptr, GL_DYNAMIC_DRAW);

    GLsizei stride = 8 * sizeof(float);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    gl_->glEnableVertexAttribArray(1);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    gl_->glEnableVertexAttribArray(2);
    gl_->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));

    // Wireframe index buffer
    std::vector<uint32_t> line_indices;
    line_indices.reserve((corner_count_ / 3) * 6);
    for (size_t tri = 0; tri < corner_count_ / 3; ++tri) {
        uint32_t c0 = static_cast<uint32_t>(tri * 3 + 0);
        uint32_t c1 = static_cast<uint32_t>(tri * 3 + 1);
        uint32_t c2 = static_cast<uint32_t>(tri * 3 + 2);
        line_indices.push_back(c0); line_indices.push_back(c1);
        line_indices.push_back(c1); line_indices.push_back(c2);
        line_indices.push_back(c2); line_indices.push_back(c0);
    }
    gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ibo_);
    gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(std::max<size_t>(line_indices.size(), 1) * sizeof(uint32_t)),
                     line_indices.empty() ? nullptr : line_indices.data(), GL_STATIC_DRAW);
    line_index_count_ = line_indices.size();

    gl_->glBindVertexArray(0);
}

MeshGL::~MeshGL() {
    if (gl_) {
        if (vbo_) gl_->glDeleteBuffers(1, &vbo_);
        if (line_ibo_) gl_->glDeleteBuffers(1, &line_ibo_);
        if (vao_) gl_->glDeleteVertexArrays(1, &vao_);
    }
}

void MeshGL::updatePositions(QOpenGLFunctions_3_3_Core* gl, const std::vector<QVector3D>& positions, bool smooth_normals) {
    if (corner_count_ == 0 || positions.empty()) return;
    size_t tri_count = corner_count_ / 3;

    std::vector<QVector3D> smooth_acc;
    if (smooth_normals) {
        smooth_acc.assign(positions.size(), QVector3D(0.0f, 0.0f, 0.0f));
        for (size_t tri = 0; tri < tri_count; ++tri) {
            size_t c0 = corner_position_indices_[tri * 3 + 0];
            size_t c1 = corner_position_indices_[tri * 3 + 1];
            size_t c2 = corner_position_indices_[tri * 3 + 2];
            if (c0 < positions.size() && c1 < positions.size() && c2 < positions.size()) {
                QVector3D fn = QVector3D::crossProduct(positions[c1] - positions[c0], positions[c2] - positions[c0]);
                smooth_acc[c0] += fn;
                smooth_acc[c1] += fn;
                smooth_acc[c2] += fn;
            }
        }
    }

    std::vector<float> interleaved;
    interleaved.reserve(corner_count_ * 8);

    for (size_t tri = 0; tri < tri_count; ++tri) {
        size_t c0 = corner_position_indices_[tri * 3 + 0];
        size_t c1 = corner_position_indices_[tri * 3 + 1];
        size_t c2 = corner_position_indices_[tri * 3 + 2];
        if (c0 >= positions.size() || c1 >= positions.size() || c2 >= positions.size()) continue;

        QVector3D p0 = positions[c0];
        QVector3D p1 = positions[c1];
        QVector3D p2 = positions[c2];
        QVector3D face_normal = QVector3D::crossProduct(p1 - p0, p2 - p0);
        float len = face_normal.length();
        face_normal = len > 1e-8f ? face_normal / len : QVector3D(0.0f, 0.0f, 1.0f);

        const QVector3D tri_pos[3] = { p0, p1, p2 };
        for (int k = 0; k < 3; ++k) {
            size_t corner = tri * 3 + k;
            size_t vidx = corner_position_indices_[corner];
            QVector3D out_n = (smooth_normals && vidx < smooth_acc.size()) ? smooth_acc[vidx] : face_normal;
            float nl = out_n.length();
            out_n = nl > 1e-8f ? out_n / nl : QVector3D(0.0f, 0.0f, 1.0f);

            interleaved.push_back(tri_pos[k].x());
            interleaved.push_back(tri_pos[k].y());
            interleaved.push_back(tri_pos[k].z());
            interleaved.push_back(out_n.x());
            interleaved.push_back(out_n.y());
            interleaved.push_back(out_n.z());
            interleaved.push_back(corner_uvs_[corner].x);
            interleaved.push_back(corner_uvs_[corner].y);
        }
    }

    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl->glBufferSubData(GL_ARRAY_BUFFER, 0,
                       static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
                       interleaved.empty() ? nullptr : interleaved.data());
}

void MeshGL::setTexture(int32_t material_index, GLuint gl_texture_id) {
    for (auto& batch : batches_) {
        if (material_index < 0 || batch.material_index == material_index) {
            batch.texture = gl_texture_id;
        }
    }
}

void MeshGL::renderSolid(QOpenGLFunctions_3_3_Core* gl, GLint u_has_texture) const {
    if (corner_count_ == 0) return;
    gl->glBindVertexArray(vao_);
    for (const auto& batch : batches_) {
        if (batch.texture != 0) {
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, batch.texture);
            if (u_has_texture >= 0) gl->glUniform1i(u_has_texture, 1);
        } else {
            if (u_has_texture >= 0) gl->glUniform1i(u_has_texture, 0);
        }
        gl->glDrawArrays(GL_TRIANGLES, batch.first_corner, batch.corner_count);
    }
    gl->glBindVertexArray(0);
}

void MeshGL::renderWireframe(QOpenGLFunctions_3_3_Core* gl) const {
    if (line_index_count_ == 0) return;
    gl->glBindVertexArray(vao_);
    gl->glDrawElements(GL_LINES, static_cast<GLsizei>(line_index_count_), GL_UNSIGNED_INT, nullptr);
    gl->glBindVertexArray(0);
}

} // namespace grn
