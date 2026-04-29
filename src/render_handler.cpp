#include "render_handler.h"

#include <cstring>

RenderHandler::RenderHandler(int width, int height)
    : m_width(width), m_height(height) {}

void RenderHandler::Resize(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_width = width;
    m_height = height;
}

void RenderHandler::GetViewRect(CefRefPtr<CefBrowser> /*browser*/,
                                CefRect& rect) {
    std::lock_guard<std::mutex> lock(m_mutex);
    rect = CefRect(0, 0, m_width, m_height);
}

void RenderHandler::OnPaint(CefRefPtr<CefBrowser> /*browser*/,
                            PaintElementType type,
                            const RectList& /*dirtyRects*/,
                            const void* buffer,
                            int width,
                            int height) {
    if (type != PET_VIEW) {
        return;  // Ignore popup paints for this demo.
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    m_pixels.resize(bytes);
    std::memcpy(m_pixels.data(), buffer, bytes);
    m_pendingW = width;
    m_pendingH = height;
    m_dirty = true;
}

bool RenderHandler::UploadIfDirty(GLuint texture) {
    std::vector<uint8_t> local;
    int w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_dirty) return false;
        local.swap(m_pixels);
        w = m_pendingW;
        h = m_pendingH;
        m_dirty = false;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // CEF delivers BGRA8; we upload as RGBA and swizzle in the fragment
    // shader. This keeps us portable on plain GLES2 implementations that
    // don't expose GL_BGRA_EXT.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, local.data());
    return true;
}
