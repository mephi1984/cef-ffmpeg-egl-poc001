#pragma once

#include <X11/Xlib.h>
#include <EGL/egl.h>

// Owns an X11 window plus the EGL display/surface/context that backs it.
// All EGL/GL calls in this app are made on the thread that constructs and
// drives this object.
class GLWindow {
public:
    GLWindow() = default;
    ~GLWindow();

    GLWindow(const GLWindow&) = delete;
    GLWindow& operator=(const GLWindow&) = delete;

    // Create the X11 window and bind a GLES2 context to it. Returns false on
    // failure with a message printed to stderr.
    bool Create(int width, int height, const char* title);

    // Pump pending X11 events. Updates m_width/m_height on configure-notify
    // and m_shouldClose on window-delete.
    void PollEvents();

    void SwapBuffers();

    int  width()       const { return m_width; }
    int  height()      const { return m_height; }
    bool shouldClose() const { return m_shouldClose; }
    bool resized()     { bool r = m_resized; m_resized = false; return r; }

private:
    Display* m_display = nullptr;
    Window   m_window  = 0;
    Atom     m_wmDeleteWindow = 0;

    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    EGLContext m_eglContext = EGL_NO_CONTEXT;

    int  m_width  = 0;
    int  m_height = 0;
    bool m_shouldClose = false;
    bool m_resized = false;
};
