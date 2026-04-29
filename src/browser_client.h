#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include "render_handler.h"

class BrowserClient : public CefClient,
                      public CefLifeSpanHandler,
                      public CefLoadHandler {
public:
    explicit BrowserClient(CefRefPtr<RenderHandler> render_handler);

    CefRefPtr<CefRenderHandler>   GetRenderHandler()   override { return m_renderHandler; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler>     GetLoadHandler()     override { return this; }

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    bool DoClose(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefLoadHandler
    void OnLoadError(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode,
                     const CefString& errorText,
                     const CefString& failedUrl) override;

    CefRefPtr<CefBrowser> browser() const { return m_browser; }
    bool closed() const { return m_closed; }

private:
    CefRefPtr<RenderHandler> m_renderHandler;
    CefRefPtr<CefBrowser> m_browser;
    bool m_closed = false;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};
