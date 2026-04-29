#include "browser_client.h"

#include <cstdio>

BrowserClient::BrowserClient(CefRefPtr<RenderHandler> render_handler)
    : m_renderHandler(std::move(render_handler)) {}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    m_browser = browser;
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
    return false;  // Allow default close handling.
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    m_browser = nullptr;
    m_closed = true;
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
                                CefRefPtr<CefFrame> /*frame*/,
                                ErrorCode errorCode,
                                const CefString& errorText,
                                const CefString& failedUrl) {
    if (errorCode == ERR_ABORTED) return;
    std::fprintf(stderr, "[CEF] Load error %d for %s: %s\n",
                 errorCode,
                 failedUrl.ToString().c_str(),
                 errorText.ToString().c_str());
}
