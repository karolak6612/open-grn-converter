#include "font_atlas.h"
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <algorithm>

namespace grn {

FontAtlas::~FontAtlas() {
    cleanup();
}

void FontAtlas::cleanup() {
    if (texture_id_ != 0 && gl_ != nullptr) {
        gl_->glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
    gl_ = nullptr;
}

bool FontAtlas::init(QOpenGLFunctions_3_3_Core* gl, int font_pixel_size) {
    if (!gl) return false;
    cleanup();
    gl_ = gl;

    constexpr int kAtlasWidth = 512;
    constexpr int kAtlasHeight = 512;

    QImage atlas_img(kAtlasWidth, kAtlasHeight, QImage::Format_RGBA8888);
    atlas_img.fill(Qt::transparent);

    // 1. Bake a 4x4 solid white region at (0, 0)
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            atlas_img.setPixelColor(x, y, QColor(255, 255, 255, 255));
        }
    }
    // Point UV into center of solid white region to avoid bilinear filtering border bleed
    white_u0_ = 1.0f / static_cast<float>(kAtlasWidth);
    white_v0_ = 1.0f / static_cast<float>(kAtlasHeight);
    white_u1_ = 2.0f / static_cast<float>(kAtlasWidth);
    white_v1_ = 2.0f / static_cast<float>(kAtlasHeight);

    // 2. Setup Font & Painter
    QFont font("Segoe UI");
    font.setPixelSize(font_pixel_size);
    font.setWeight(QFont::Bold);

    QFontMetrics fm(font);
    line_height_ = static_cast<float>(fm.height());

    {
        QPainter painter(&atlas_img);
        painter.setFont(font);
        painter.setPen(QColor(255, 255, 255, 255));
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        int cur_x = 8; // Start past 4x4 solid block
        int cur_y = 2;
        int row_height = fm.height() + 4;

        for (int c = 32; c <= 126; ++c) {
            char ch = static_cast<char>(c);
            QString str(ch);
            QRect br = fm.boundingRect(str);
            int adv = fm.horizontalAdvance(str);

            int glyph_w = std::max(adv, br.width()) + 2;
            int glyph_h = row_height;

            if (cur_x + glyph_w >= kAtlasWidth - 2) {
                cur_x = 2;
                cur_y += row_height;
            }

            if (cur_y + row_height >= kAtlasHeight) {
                break; // Atlas full (512x512 is plenty for ASCII)
            }

            // Draw character
            int text_x = cur_x;
            int text_y = cur_y + fm.ascent();
            painter.drawText(text_x, text_y, str);

            // Record glyph UV and dimensions
            GlyphMetrics& m = glyphs_[static_cast<size_t>(c)];
            m.u0 = static_cast<float>(cur_x) / static_cast<float>(kAtlasWidth);
            m.v0 = static_cast<float>(cur_y) / static_cast<float>(kAtlasHeight);
            m.u1 = static_cast<float>(cur_x + glyph_w) / static_cast<float>(kAtlasWidth);
            m.v1 = static_cast<float>(cur_y + glyph_h) / static_cast<float>(kAtlasHeight);
            m.width = static_cast<float>(glyph_w);
            m.height = static_cast<float>(glyph_h);
            m.advance = static_cast<float>(adv);

            cur_x += glyph_w + 2; // 2px margin to prevent sampling bleed
        }
    }

    // 3. Upload to OpenGL Texture
    gl_->glGenTextures(1, &texture_id_);
    gl_->glBindTexture(GL_TEXTURE_2D, texture_id_);
    gl_->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kAtlasWidth, kAtlasHeight, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, atlas_img.constBits());
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl_->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl_->glBindTexture(GL_TEXTURE_2D, 0);

    return texture_id_ != 0;
}

const GlyphMetrics& FontAtlas::glyph(char c) const {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 32 || uc > 126) {
        return glyphs_[static_cast<size_t>(' ')];
    }
    return glyphs_[uc];
}

float FontAtlas::measureText(std::string_view text) const {
    float width = 0.0f;
    for (char c : text) {
        width += glyph(c).advance;
    }
    return width;
}

} // namespace grn
