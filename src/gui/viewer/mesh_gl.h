#pragma once

#include "../../core/grn_types.h"
#include <QOpenGLFunctions_3_3_Core>
#include <QVector3D>
#include <vector>
#include <cstdint>

namespace grn {

/**
 * @brief Manages GPU buffers (VAO, VBO, IBO) and material batches for a single GrnMesh.
 */
class MeshGL {
public:
    MeshGL(QOpenGLFunctions_3_3_Core* gl, const GrnMesh& mesh);
    ~MeshGL();

    MeshGL(const MeshGL&) = delete;
    MeshGL& operator=(const MeshGL&) = delete;

    void updatePositions(QOpenGLFunctions_3_3_Core* gl, const std::vector<QVector3D>& positions, bool smooth_normals);
    void setTexture(int32_t material_index, GLuint gl_texture_id);
    void renderSolid(QOpenGLFunctions_3_3_Core* gl, GLint u_has_texture) const;
    void renderWireframe(QOpenGLFunctions_3_3_Core* gl) const;

    size_t vertexCount() const { return corner_count_; }
    size_t triangleCount() const { return corner_count_ / 3; }

private:
    struct Batch {
        int32_t material_index{ -1 };
        int first_corner{ 0 };
        int corner_count{ 0 };
        GLuint texture{ 0 };
    };

    GLuint vbo_{ 0 };
    GLuint vao_{ 0 };
    GLuint line_ibo_{ 0 };
    size_t line_index_count_{ 0 };

    std::vector<Batch> batches_;
    std::vector<uint32_t> corner_position_indices_;
    std::vector<Vec2> corner_uvs_;
    size_t corner_count_{ 0 };
    QOpenGLFunctions_3_3_Core* gl_{ nullptr };
};

} // namespace grn
