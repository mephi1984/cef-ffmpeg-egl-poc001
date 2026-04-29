#include "gl_window.h"

#include <cstdio>
#include <cstring>

#include <X11/Xutil.h>

namespace {
const char* EglErrorString(EGLint err) {
    switch (err) {
        case EGL_SUCCESS:             return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
        default:                      return "?";
    }
}
} // namespace

GLWindow::~GLWindow() {
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_eglContext != EGL_NO_CONTEXT) eglDestroyContext(m_eglDisplay, m_eglContext);
        if (m_eglSurface != EGL_NO_SURFACE) eglDestroySurface(m_eglDisplay, m_eglSurface);
        eglTerminate(m_eglDisplay);
    }
    if (m_display) {
        if (m_window) XDestroyWindow(m_display, m_window);
        XCloseDisplay(m_display);
    }
}

bool GLWindow::Create(int width, int height, const char* title) {
    m_width  = width;
    m_height = height;

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        std::fprintf(stderr, "XOpenDisplay failed (DISPLAY env not set?)\n");
        return false;
    }

    int screen = DefaultScreen(m_display);
    Window root = RootWindow(m_display, screen);

    XSetWindowAttributes attrs{};
    attrs.background_pixel = BlackPixel(m_display, screen);
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask;

    m_window = XCreateWindow(
        m_display, root,
        0, 0, width, height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWBackPixel | CWEventMask, &attrs);

    XStoreName(m_display, m_window, title);
    m_wmDeleteWindow = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(m_display, m_window, &m_wmDeleteWindow, 1);
    XMapWindow(m_display, m_window);
    XFlush(m_display);

    m_eglDisplay = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(m_display));
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        std::fprintf(stderr, "eglGetDisplay failed\n");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(m_eglDisplay, &major, &minor)) {
        std::fprintf(stderr, "eglInitialize failed: %s\n", EglErrorString(eglGetError()));
        return false;
    }
    std::fprintf(stderr, "[EGL] %d.%d, vendor=%s\n",
                 major, minor, eglQueryString(m_eglDisplay, EGL_VENDOR));

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    0,
        EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(m_eglDisplay, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        std::fprintf(stderr, "eglChooseConfig failed: %s\n", EglErrorString(eglGetError()));
        return false;
    }

    m_eglSurface = eglCreateWindowSurface(
        m_eglDisplay, config,
        reinterpret_cast<EGLNativeWindowType>(m_window),
        nullptr);
    if (m_eglSurface == EGL_NO_SURFACE) {
        std::fprintf(stderr, "eglCreateWindowSurface failed: %s\n", EglErrorString(eglGetError()));
        return false;
    }

    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    m_eglContext = eglCreateContext(m_eglDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    if (m_eglContext == EGL_NO_CONTEXT) {
        std::fprintf(stderr, "eglCreateContext failed: %s\n", EglErrorString(eglGetError()));
        return false;
    }

    if (!eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext)) {
        std::fprintf(stderr, "eglMakeCurrent failed: %s\n", EglErrorString(eglGetError()));
        return false;
    }

    // Best-effort: try to vsync, but don't fail if the driver refuses.
    eglSwapInterval(m_eglDisplay, 1);
    return true;
}

void GLWindow::PollEvents() {
    while (XPending(m_display)) {
        XEvent ev{};
        XNextEvent(m_display, &ev);
        switch (ev.type) {
            case ConfigureNotify: {
                int w = ev.xconfigure.width;
                int h = ev.xconfigure.height;
                if (w != m_width || h != m_height) {
                    m_width = w;
                    m_height = h;
                    m_resized = true;
                }
                break;
            }
            case ClientMessage:
                if (static_cast<Atom>(ev.xclient.data.l[0]) == m_wmDeleteWindow) {
                    m_shouldClose = true;
                }
                break;
            case KeyPress:
                // Treat any key as "close" for this demo.
                m_shouldClose = true;
                break;
            default:
                break;
        }
    }
}

void GLWindow::SwapBuffers() {
    eglSwapBuffers(m_eglDisplay, m_eglSurface);
}
