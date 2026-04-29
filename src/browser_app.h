#pragma once

#include "include/cef_app.h"

class BrowserApp : public CefApp,
                   public CefBrowserProcessHandler {
public:
    BrowserApp() = default;

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override;

private:
    IMPLEMENT_REFCOUNTING(BrowserApp);
};
