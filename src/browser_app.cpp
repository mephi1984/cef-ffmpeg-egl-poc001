#include "browser_app.h"

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
    // Disable GPU/sandbox features that aren't available inside a minimal
    // headless container running on Xvfb + llvmpipe. CEF uses its own
    // bundled libEGL/libGLESv2 (ANGLE/SwiftShader) for the GPU process,
    // which works fine off-screen.
    if (process_type.empty()) {
        command_line->AppendSwitch("disable-gpu-compositing");
        command_line->AppendSwitch("disable-software-rasterizer");
        command_line->AppendSwitch("no-sandbox");
    }
}
