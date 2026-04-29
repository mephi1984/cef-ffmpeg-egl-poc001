#pragma once

#include "include/cef_render_handler.h"

#include <GLES2/gl2.h>
#include <mutex>
#include <vector>

// Receives off-screen paint callbacks from CEF and stages the pixels for
// upload into an OpenGL texture. OnPaint is invoked from CEF's UI thread,
// which in this app is the same thread that drives the render loop (we
// pump CEF via CefDoMessageLoopWork). To stay defensive against future
// threading changes we still guard the staging buffer with a mutex.
class RenderHandler : public CefRenderHandler {
public:
    RenderHandler(int width, int height);

    // Update the logical viewport size. Safe to call from the render thread;
    // CEF will be informed via CefBrowserHost::WasResized() by the caller.
    void Resize(int width, int height);

    // Return current viewport size (the size CEF is rendering at).
    int width()  const { return m_width; }
    int height() const { return m_height; }

    // Upload the latest staged frame (if any) into the given GL texture.
    // Must be called with a current GL context. Returns true if a fresh
    // frame was uploaded.
    bool UploadIfDirty(GLuint texture);

    // CefRenderHandler overrides
    void GetViewRect(CefRefPtr<CefBrowser> browser,
                     CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser,
                 PaintElementType type,
                 const RectList& dirtyRects,
                 const void* buffer,
                 int width,
                 int height) override;

private:
    std::mutex m_mutex;
    int m_width;
    int m_height;
    int m_pendingW = 0;
    int m_pendingH = 0;
    bool m_dirty = false;
    std::vector<uint8_t> m_pixels;  // BGRA8

    IMPLEMENT_REFCOUNTING(RenderHandler);
};
