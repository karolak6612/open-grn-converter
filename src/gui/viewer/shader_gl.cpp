#include "shader_gl.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace grn {

namespace {

const char* kMeshVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
uniform mat4 view_proj;
uniform mat4 view;
out vec3 v_normal;
out vec3 v_view_normal;
out vec2 v_uv;
void main() {
    gl_Position = view_proj * vec4(in_position, 1.0);
    v_normal = in_normal;
    v_view_normal = mat3(view) * in_normal;
    v_uv = in_uv;
}
)";

const char* kMeshFragmentShader = R"(
#version 330 core
in vec3 v_normal;
in vec3 v_view_normal;
in vec2 v_uv;
out vec4 f_color;
uniform sampler2D tex;
uniform sampler2D matcap_tex;
uniform int u_mode;
uniform int u_has_texture;
uniform vec3 light_dir;

void main() {
    vec3 n = normalize(v_normal);
    vec3 nv = normalize(v_view_normal);
    vec3 col;
    if (u_mode == 3) {
        col = n * 0.5 + 0.5;
    } else if (u_mode == 2) {
        col = texture(matcap_tex, nv.xy * 0.5 + 0.5).rgb;
    } else {
        vec4 s = (u_has_texture == 1) ? texture(tex, v_uv) : vec4(0.72, 0.74, 0.78, 1.0);
        if (u_has_texture == 1 && s.a < 0.1) discard;
        if (u_mode == 1) {
            col = s.rgb;
        } else {
            float ndotl = abs(dot(n, normalize(light_dir)));
            float brightness = clamp(0.35 + 0.65 * ndotl, 0.15, 1.0);
            float fill = clamp(0.25 + 0.25 * abs(dot(n, normalize(vec3(-light_dir.x, -light_dir.y, light_dir.z)))), 0.0, 1.0);
            col = s.rgb * (brightness * 0.85 + fill * 0.15);
        }
    }
    f_color = vec4(col, 1.0);
}
)";

const char* kWireVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 in_position;
uniform mat4 view_proj;
void main() {
    vec4 clip = view_proj * vec4(in_position, 1.0);
    clip.z -= 8e-5 * clip.w;
    gl_Position = clip;
}
)";

const char* kWireFragmentShader = R"(
#version 330 core
uniform vec4 u_color;
out vec4 f_color;
void main() {
    f_color = u_color;
}
)";

const char* kGridVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 in_position;
uniform mat4 view_proj;
out vec3 v_world;
void main() {
    v_world = in_position;
    gl_Position = view_proj * vec4(in_position, 1.0);
}
)";

const char* kGridFragmentShader = R"(
#version 330 core
in vec3 v_world;
out vec4 f_color;
uniform vec3 cam_pos;
uniform float cell;

float grid_line(vec2 p, float scale) {
    vec2 q = p / scale;
    vec2 g = abs(fract(q - 0.5) - 0.5) / fwidth(q);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    vec2 p = v_world.xz;
    float minor = grid_line(p, cell) * 0.30;
    float major = grid_line(p, cell * 5.0) * 0.55;
    float lines = max(minor, major);

    float wx = 1.0 - min(abs(v_world.z) / (fwidth(v_world.z) * 1.5 + 1e-6), 1.0);
    float wz = 1.0 - min(abs(v_world.x) / (fwidth(v_world.x) * 1.5 + 1e-6), 1.0);

    vec3 rgb = vec3(0.40, 0.44, 0.50);
    rgb = mix(rgb, vec3(0.85, 0.35, 0.32), wx);
    rgb = mix(rgb, vec3(0.40, 0.80, 0.45), wz);

    float dist = distance(v_world, cam_pos);
    float fade = exp(-dist / max(cell * 150.0, 1.0));
    fade *= fade;

    float alpha = max(lines, max(wx, wz) * 0.8) * fade;
    if (alpha < 0.004) discard;
    f_color = vec4(rgb, alpha);
}
)";

} // namespace

GLuint compileShader(QOpenGLFunctions_3_3_Core* gl, GLenum type, const char* src) {
    GLuint shader = gl->glCreateShader(type);
    gl->glShaderSource(shader, 1, &src, nullptr);
    gl->glCompileShader(shader);
    GLint ok = 0;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        gl->glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return shader;
}

GLuint linkProgram(QOpenGLFunctions_3_3_Core* gl, const char* vs_src, const char* fs_src) {
    GLuint vs = compileShader(gl, GL_VERTEX_SHADER, vs_src);
    GLuint fs = compileShader(gl, GL_FRAGMENT_SHADER, fs_src);
    GLuint program = gl->glCreateProgram();
    gl->glAttachShader(program, vs);
    gl->glAttachShader(program, fs);
    gl->glLinkProgram(program);
    GLint ok = 0;
    gl->glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl->glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        gl->glDeleteShader(vs);
        gl->glDeleteShader(fs);
        gl->glDeleteProgram(program);
        throw std::runtime_error(std::string("Program link error: ") + log);
    }
    gl->glDeleteShader(vs);
    gl->glDeleteShader(fs);
    return program;
}

GLuint createMeshProgram(QOpenGLFunctions_3_3_Core* gl) {
    return linkProgram(gl, kMeshVertexShader, kMeshFragmentShader);
}

GLuint createWireProgram(QOpenGLFunctions_3_3_Core* gl) {
    return linkProgram(gl, kWireVertexShader, kWireFragmentShader);
}

GLuint createGridProgram(QOpenGLFunctions_3_3_Core* gl) {
    return linkProgram(gl, kGridVertexShader, kGridFragmentShader);
}

GLuint createMatcapTexture(QOpenGLFunctions_3_3_Core* gl) {
    constexpr int S = 256;
    std::vector<unsigned char> pixels(S * S * 3);
    for (int y = 0; y < S; ++y) {
        float ny = (y / static_cast<float>(S - 1)) * 2.0f - 1.0f;
        for (int x = 0; x < S; ++x) {
            float nx = (x / static_cast<float>(S - 1)) * 2.0f - 1.0f;
            float r2 = nx * nx + ny * ny;
            size_t idx = (static_cast<size_t>(y) * S + x) * 3;
            if (r2 > 1.0f) {
                pixels[idx] = 40; pixels[idx + 1] = 42; pixels[idx + 2] = 48;
                continue;
            }
            float nz = std::sqrt(std::max(0.0f, 1.0f - r2));
            float k1 = std::max(0.0f, nx * 0.45f + ny * 0.70f + nz * 0.55f);
            float k2 = std::max(0.0f, -nx * 0.60f - ny * 0.30f + nz * 0.30f);
            float spec = std::pow(std::max(0.0f, nx * 0.40f + ny * 0.65f + nz * 0.65f), 18.0f);
            float r = (0.35f * k1 + 0.12f * k2 + 0.35f * spec + 0.25f) * 255.0f;
            float g = (0.32f * k1 + 0.11f * k2 + 0.35f * spec + 0.22f) * 255.0f;
            float b = (0.28f * k1 + 0.10f * k2 + 0.35f * spec + 0.19f) * 255.0f;
            pixels[idx] = static_cast<unsigned char>(std::clamp(r, 0.0f, 255.0f));
            pixels[idx + 1] = static_cast<unsigned char>(std::clamp(g, 0.0f, 255.0f));
            pixels[idx + 2] = static_cast<unsigned char>(std::clamp(b, 0.0f, 255.0f));
        }
    }

    GLuint texId = 0;
    gl->glGenTextures(1, &texId);
    gl->glBindTexture(GL_TEXTURE_2D, texId);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, S, S, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    return texId;
}

} // namespace grn
