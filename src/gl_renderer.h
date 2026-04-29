#pragma once

#include <GLES2/gl2.h>
#include <cstdint>

// Renders the demo: textured upper-half (CEF browser) and lower-half (video).
// Both halves are drawn through the same generic DrawQuad path.
class GLRenderer {
public:
    GLRenderer() = default;
    ~GLRenderer();

    GLRenderer(const GLRenderer&) = delete;
    GLRenderer& operator=(const GLRenderer&) = delete;

    bool Init();

    GLuint browser_texture() const { return m_browserTexture; }
    GLuint video_texture()   const { return m_videoTexture; }

    // Replace the contents of the video texture with `width`x`height` BGRA8
    // pixels. Allocates / reallocates as needed.
    void UploadVideoBGRA(const uint8_t* pixels, int width, int height);

    // Per-frame state: viewport + clear.
    void BeginFrame(int viewportW, int viewportH);

    // Draw a textured quad. Position rect is in clip space ([-1,1]); UV
    // rect is in normalized texture space (top-left origin = (0,0), so v=1
    // is the bottom of the texture). The fragment shader swizzles BGRA to
    // RGBA, so both the CEF and FFmpeg paths upload BGRA bytes to a GL_RGBA
    // texture and the result lands on screen with correct colours.
    void DrawQuad(GLuint texture,
                  float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1);

private:
    GLuint CompileShader(GLenum type, const char* src);
    GLuint LinkProgram(GLuint vs, GLuint fs);

    GLuint m_program = 0;
    GLint  m_aPos = -1;
    GLint  m_aUV  = -1;
    GLint  m_uTex = -1;

    GLuint m_browserTexture = 0;
    GLuint m_videoTexture   = 0;
    int    m_videoTexW = 0;
    int    m_videoTexH = 0;
};
