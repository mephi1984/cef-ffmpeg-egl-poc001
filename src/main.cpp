#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"

#include "browser_app.h"
#include "browser_client.h"
#include "gl_renderer.h"
#include "gl_window.h"
#include "render_handler.h"
#include "video_decoder.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>
#include <limits.h>

namespace {

// Resolve the directory containing the running executable so we can find
// the bundled HTML page and video regardless of the user's cwd.
std::string ExecutableDir() {
    char buf[PATH_MAX]{};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string path(buf, n);
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "." : path.substr(0, pos);
}

int CalcBrowserHeight(int windowH) {
    // CEF gets the upper half of the window. Round up so 1px of "lower
    // half" never sneaks into the browser texture on odd window heights.
    return (windowH + 1) / 2;
}

// Compute the UV sub-rectangle to sample from a `srcW`x`srcH` source so
// that, when stretched onto a `dstW`x`dstH` destination quad, the source
// covers the whole destination without distortion (excess is cropped
// symmetrically — the "cover" fit familiar from CSS background-size).
struct UVRect { float u0, v0, u1, v1; };
UVRect CoverUVs(int srcW, int srcH, int dstW, int dstH) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return {0.f, 0.f, 1.f, 1.f};
    }
    const double srcAspect = double(srcW) / srcH;
    const double dstAspect = double(dstW) / dstH;
    if (srcAspect > dstAspect) {
        // Source is wider than destination — crop left/right.
        const float scale = float(dstAspect / srcAspect);
        const float pad   = (1.f - scale) * 0.5f;
        return {pad, 0.f, 1.f - pad, 1.f};
    } else {
        // Source is taller than destination — crop top/bottom.
        const float scale = float(srcAspect / dstAspect);
        const float pad   = (1.f - scale) * 0.5f;
        return {0.f, pad, 1.f, 1.f - pad};
    }
}

double NowSeconds() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

} // namespace

int main(int argc, char* argv[]) {
    // Step 1: CEF subprocess dispatch. Must run BEFORE any other CEF call.
    // Returns >= 0 if this invocation is a child process (renderer / GPU /
    // utility); we just return the code in that case.
    CefMainArgs main_args(argc, argv);
    CefRefPtr<BrowserApp> app(new BrowserApp());
    {
        int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
        if (exit_code >= 0) return exit_code;
    }

    // Step 2: bring up the X11/EGL window before CEF. CEF doesn't need our
    // GL context, but we want it ready so rendering can start the moment
    // the browser delivers its first frame.
    constexpr int kInitialW = 1280;
    constexpr int kInitialH = 720;

    GLWindow window;
    if (!window.Create(kInitialW, kInitialH, "CEF + EGL OSR demo")) {
        std::fprintf(stderr, "Failed to create EGL window\n");
        return 1;
    }

    GLRenderer renderer;
    if (!renderer.Init()) {
        std::fprintf(stderr, "Failed to init GL renderer\n");
        return 1;
    }

    // Step 3: open the looping background video.
    const std::string exe_dir = ExecutableDir();
    VideoDecoder video;
    const std::string video_path = exe_dir + "/testvideo001.mp4";
    const bool have_video = video.Open(video_path);
    if (!have_video) {
        std::fprintf(stderr, "Continuing without video (lower half stays empty).\n");
    }

    // Step 4: initialise CEF.
    const std::string url = "file://" + exe_dir + "/html/index.html";

    CefSettings settings;
    settings.no_sandbox = true;
    settings.windowless_rendering_enabled = true;
    settings.multi_threaded_message_loop = false;
    settings.external_message_pump = false;
    settings.log_severity = LOGSEVERITY_WARNING;
    CefString(&settings.cache_path) = "";
    CefString(&settings.root_cache_path) = "";

    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        std::fprintf(stderr, "CefInitialize failed\n");
        return 1;
    }

    // Step 5: create the OSR browser.
    const int browserH = CalcBrowserHeight(window.height());
    CefRefPtr<RenderHandler> render_handler(
        new RenderHandler(window.width(), browserH));
    CefRefPtr<BrowserClient> client(new BrowserClient(render_handler));

    CefWindowInfo window_info;
    window_info.SetAsWindowless(0);  // No parent window for OSR.

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 60;

    CefBrowserHost::CreateBrowser(window_info, client.get(), url,
                                  browser_settings, nullptr, nullptr);

    // Step 6: render loop. We pump CEF's UI thread via DoMessageLoopWork
    // each frame; OnPaint will land on this same thread, so we can upload
    // the texture without cross-thread handoff.
    using clock = std::chrono::steady_clock;
    auto next_frame = clock::now();
    constexpr auto frame_period = std::chrono::milliseconds(16);  // ~60 Hz

    while (!window.shouldClose()) {
        window.PollEvents();

        if (window.resized() && client->browser()) {
            const int newH = CalcBrowserHeight(window.height());
            render_handler->Resize(window.width(), newH);
            client->browser()->GetHost()->WasResized();
        }

        CefDoMessageLoopWork();
        render_handler->UploadIfDirty(renderer.browser_texture());

        if (have_video && video.Advance(NowSeconds())) {
            renderer.UploadVideoBGRA(video.current_bgra(),
                                     video.width(), video.height());
        }

        const int W = window.width();
        const int H = window.height();
        const int halfH = H / 2;

        renderer.BeginFrame(W, H);

        // Lower half: video, cover-cropped to fill.
        if (have_video) {
            const UVRect uv = CoverUVs(video.width(), video.height(),
                                       W, halfH);
            renderer.DrawQuad(renderer.video_texture(),
                              -1.0f, -1.0f, 1.0f, 0.0f,
                              uv.u0, uv.v0, uv.u1, uv.v1);
        }

        // Upper half: CEF browser frame.
        renderer.DrawQuad(renderer.browser_texture(),
                          -1.0f, 0.0f, 1.0f, 1.0f,
                          0.0f, 0.0f, 1.0f, 1.0f);

        window.SwapBuffers();

        next_frame += frame_period;
        std::this_thread::sleep_until(next_frame);
    }

    // Step 7: tear down the browser before shutting CEF.
    if (client->browser()) {
        client->browser()->GetHost()->CloseBrowser(true);
        // Pump CEF until OnBeforeClose has fired. Bail out after a generous
        // timeout so a stuck renderer doesn't hang the process forever.
        const auto deadline = clock::now() + std::chrono::seconds(3);
        while (!client->closed() && clock::now() < deadline) {
            CefDoMessageLoopWork();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    CefShutdown();
    return 0;
}
