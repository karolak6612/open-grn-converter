#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <array>
#include <string_view>
#include <cstdint>

namespace grn {

/**
 * @brief Metrics for an individual rasterized glyph in the font texture atlas.
 */
struct GlyphMetrics {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float advance = 0.0f;
};

/**
 * @brief High-performance OpenGL texture atlas containing ASCII glyphs
 *        and a solid white block for badge backgrounds and borders.
 *        Zero QPainter usage at runtime.
 */
class FontAtlas {
public:
    FontAtlas() = default;
    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;

    /**
     * @brief Bakes the font atlas into an OpenGL texture.
     * @param gl Pointer to OpenGL 3.3 core functions.
     * @param font_pixel_size Point size / pixel height of rasterized text.
     */
    bool init(QOpenGLFunctions_3_3_Core* gl, int font_pixel_size = 11);

    /**
     * @brief Releases the OpenGL texture resource.
     */
    void cleanup();

    GLuint textureId() const { return texture_id_; }
    const GlyphMetrics& glyph(char c) const;
    float measureText(std::string_view text) const;
    float lineHeight() const { return line_height_; }

    /**
     * @brief Normalized UV coordinates (u0, v0, u1, v1) for the solid white rectangle.
     */
    std::array<float, 4> whiteUV() const { return { white_u0_, white_v0_, white_u1_, white_v1_ }; }

private:
    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLuint texture_id_ = 0;
    float line_height_ = 14.0f;
    float white_u0_ = 0.0f;
    float white_v0_ = 0.0f;
    float white_u1_ = 0.0f;
    float white_v1_ = 0.0f;

    std::array<GlyphMetrics, 128> glyphs_{};
};

} // namespace grn
