#include "gl_renderer.h"

#include <cstdio>
#include <vector>

namespace {

const char* kVertexSrc = R"(
attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

// Both inputs (CEF and FFmpeg) hand us BGRA bytes which we upload as RGBA;
// swizzle here so the on-screen colours are correct.
const char* kFragmentSrc = R"(
precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
void main() {
    vec4 c = texture2D(u_tex, v_uv);
    gl_FragColor = vec4(c.bgr, c.a);
}
)";

GLuint MakeTexture() {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const uint8_t kPlaceholder[4] = { 0, 0, 0, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, kPlaceholder);
    return t;
}

} // namespace

GLRenderer::~GLRenderer() {
    if (m_program)        glDeleteProgram(m_program);
    if (m_browserTexture) glDeleteTextures(1, &m_browserTexture);
    if (m_videoTexture)   glDeleteTextures(1, &m_videoTexture);
}

GLuint GLRenderer::CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(s, log.size(), nullptr, log.data());
        std::fprintf(stderr, "Shader compile failed: %s\n", log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint GLRenderer::LinkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetProgramInfoLog(p, log.size(), nullptr, log.data());
        std::fprintf(stderr, "Program link failed: %s\n", log.data());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool GLRenderer::Init() {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
    if (!vs || !fs) return false;
    m_program = LinkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!m_program) return false;

    m_aPos = glGetAttribLocation(m_program, "a_pos");
    m_aUV  = glGetAttribLocation(m_program, "a_uv");
    m_uTex = glGetUniformLocation(m_program, "u_tex");

    m_browserTexture = MakeTexture();
    m_videoTexture   = MakeTexture();
    return m_browserTexture != 0 && m_videoTexture != 0;
}

void GLRenderer::UploadVideoBGRA(const uint8_t* pixels, int width, int height) {
    glBindTexture(GL_TEXTURE_2D, m_videoTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (width != m_videoTexW || height != m_videoTexH) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        m_videoTexW = width;
        m_videoTexH = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
}

void GLRenderer::BeginFrame(int viewportW, int viewportH) {
    glViewport(0, 0, viewportW, viewportH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::DrawQuad(GLuint texture,
                          float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1) {
    // UV origin is top-left; flip v so a video frame's row 0 (top of source
    // image) maps to clip-space y1 (top of the destination quad).
    const float fv0 = 1.0f - v0;
    const float fv1 = 1.0f - v1;
    const GLfloat verts[] = {
        x0, y0,    u0, fv0,
        x1, y0,    u1, fv0,
        x0, y1,    u0, fv1,
        x1, y1,    u1, fv1,
    };

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(m_uTex, 0);

    glEnableVertexAttribArray(m_aPos);
    glEnableVertexAttribArray(m_aUV);
    const GLsizei stride = 4 * sizeof(GLfloat);
    glVertexAttribPointer(m_aPos, 2, GL_FLOAT, GL_FALSE, stride, &verts[0]);
    glVertexAttribPointer(m_aUV,  2, GL_FLOAT, GL_FALSE, stride, &verts[2]);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(m_aPos);
    glDisableVertexAttribArray(m_aUV);
}
