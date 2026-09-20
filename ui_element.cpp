#include "stdafx.h"
#include "dlna_server.h"
#include "config.h"
#include <libPPUI/win32_op.h>
#include <helpers/BumpableElem.h>

namespace {
static const GUID guid_sacd_dlna_element = { 0x8c0fe7e9, 0x5f8f, 0x4d33, { 0x9d, 0x9c, 0x2b, 0x64, 0xa8, 0x3d, 0x20, 0x11 } };

class CSacdDlnaWindow : public ui_element_instance, public CWindowImpl<CSacdDlnaWindow> {
public:
    DECLARE_WND_CLASS_EX(TEXT("{5A6C02E7-8D2E-4D5D-8FB7-5F1BD2E11F01}"), CS_VREDRAW | CS_HREDRAW, -1);

    CSacdDlnaWindow(ui_element_config::ptr config, ui_element_instance_callback_ptr cb) : m_config(config), m_callback(cb) {}
    ~CSacdDlnaWindow() { KillTimer(1); }
    void initialize_window(HWND parent) { WIN32_OP(Create(parent) != NULL); SetTimer(1, 500); }
    HWND get_wnd() { return *this; }
    void set_configuration(ui_element_config::ptr config) { m_config = config; }
    ui_element_config::ptr get_configuration() { return m_config; }
    static GUID g_get_guid() { return guid_sacd_dlna_element; }
    static GUID g_get_subclass() { return ui_element_subclass_utility; }
    static void g_get_name(pfc::string_base& out) { out = "SACD DLNA Status"; }
    static ui_element_config::ptr g_get_default_configuration() { return ui_element_config::g_create_empty(g_get_guid()); }
    static const char* g_get_description() { return "Real-time native DSD DLNA, server read-ahead and network diagnostics."; }
    static bool g_get_popup_specs(ui_size& size, pfc::string_base& title) { size.cx = 980; size.cy = 610; title = "SACD DLNA Status / Diagnostics"; return true; }

    BEGIN_MSG_MAP_EX(CSacdDlnaWindow)
        MSG_WM_PAINT(OnPaint)
        MSG_WM_ERASEBKGND(OnEraseBkgnd)
        MSG_WM_LBUTTONUP(OnClick)
        MSG_WM_LBUTTONDBLCLK(OnDoubleClick)
        MSG_WM_RBUTTONUP(OnRightClick)
        MSG_WM_TIMER(OnTimer)
    END_MSG_MAP()

    void notify(const GUID& what, t_size, const void*, t_size) {
        if (what == ui_element_notify_colors_changed || what == ui_element_notify_font_changed) Invalidate();
    }

private:
    ui_element_config::ptr m_config;
protected:
    const ui_element_instance_callback_ptr m_callback;

    BOOL OnEraseBkgnd(CDCHandle dc) {
        CRect rc; WIN32_OP_D(GetClientRect(&rc));
        CBrush brush; WIN32_OP_D(brush.CreateSolidBrush(m_callback->query_std_color(ui_color_background)) != NULL);
        dc.FillRect(&rc, brush); return TRUE;
    }

    void OnTimer(UINT_PTR) { Invalidate(FALSE); }

    void drawText(HDC hdc, int x, int y, int w, int h, const char* text, bool bold = false) {
        CRect rc(x, y, x + w, y + h);
        ::SetTextColor(hdc, m_callback->query_std_color(ui_color_text));
        ::SetBkMode(hdc, TRANSPARENT);
        const auto font = m_callback->query_font_ex(ui_font_default);
        SelectObjectScope fs(hdc, (HGDIOBJ)font);
        ::DrawTextA(hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        (void)bold;
    }

    void drawBar(HDC hdc, int x, int y, int w, int h, uint64_t value, uint64_t total) {
        CRect outer(x, y, x + w, y + h);
        HBRUSH bg = CreateSolidBrush(m_callback->query_std_color(ui_color_background));
        FillRect(hdc, &outer, bg); DeleteObject(bg);
        FrameRect(hdc, &outer, GetSysColorBrush(COLOR_WINDOWFRAME));
        if (!total) return;
        const uint64_t pct = value >= total ? 100 : (value * 100ULL / total);
        CRect fill = outer;
        fill.left += 2; fill.top += 2; fill.right = fill.left + static_cast<int>((w - 4) * pct / 100); fill.bottom -= 2;
        HBRUSH bar = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        FillRect(hdc, &fill, bar); DeleteObject(bar);
    }

    static std::string fmtMB(uint64_t bytes) {
        char b[64]{};
        if (bytes >= 1024ull * 1024ull * 1024ull) snprintf(b, sizeof(b), "%.2f GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        else snprintf(b, sizeof(b), "%.2f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return b;
    }

    static std::string fmtRate(uint32_t rate) {
        if (rate == 2822400) return "DSD64 (2.8224 MHz)";
        if (rate == 5644800) return "DSD128 (5.6448 MHz)";
        if (rate == 11289600) return "DSD256 (11.2896 MHz)";
        if (!rate) return "-";
        char b[64]{}; snprintf(b, sizeof(b), "%.6f MHz", static_cast<double>(rate) / 1000000.0); return b;
    }

    static std::string fmtResolution(uint32_t rate, uint32_t channels, uint32_t bits) {
        std::string out = fmtRate(rate);
        if (channels) out += " / " + std::to_string(channels) + " ch";
        if (bits) out += " / " + std::to_string(bits) + " bit";
        return out;
    }

    void OnPaint(CDCHandle) {
        CPaintDC dc(*this);
        const auto st = SacdDlnaServer::instance().get_status();
        CRect client; GetClientRect(&client);
        const int W = client.Width();

        const std::string title = "SACD DLNA  |  " + std::string(st.broadcasting ? "BROADCASTING / ACTIVE" : "STOPPED");
        drawText(dc.m_hDC, 12, 8, W - 24, 24, title.c_str());
        drawText(dc.m_hDC, 12, 34, W / 2 - 18, 20, ("HTTP: " + std::string(st.httpReady ? "READY" : "NOT READY") + "  |  TCP " + std::to_string(st.port)).c_str());
        drawText(dc.m_hDC, W / 2, 34, W / 2 - 12, 20, ("SSDP: " + std::string(st.ssdpReady ? "READY" : "NOT READY") + "  |  239.255.255.250:1900").c_str());

        drawText(dc.m_hDC, 12, 62, W - 24, 20, ("NETWORK VISIBILITY: " + std::string(st.networkVisibility.is_empty() ? (st.networkPresence ? "CONFIRMED" : "NOT CONFIRMED") : st.networkVisibility.c_str()) +
            "   | self HTTP " + std::string(st.httpSelfTestOk ? "OK" : "-") + "   | self SSDP " + std::string(st.ssdpProbeOk ? "OK" : "-") + "   | NOTIFY loopback " + std::string(st.ssdpNotifyLoopbackOk ? "OK" : "-")).c_str());
        drawText(dc.m_hDC, 12, 84, W - 24, 20, ("SSDP NOTIFY sent: " + std::to_string(st.ssdpAliveSent) +
            "   M-SEARCH rx: " + std::to_string(st.ssdpMSearchReceived) + " (remote " + std::to_string(st.remoteSsdpSearches) + ")" +
            "   responses: " + std::to_string(st.ssdpResponsesSent) +
            "   renderer responses: " + std::to_string(st.ssdpDiscoverResponses)).c_str());
        if (!st.localIp.is_empty()) drawText(dc.m_hDC, W / 2, 104, W / 2 - 12, 20, ("LAN IP: " + std::string(st.localIp.c_str())).c_str());
        drawText(dc.m_hDC, 12, 104, W / 2 - 18, 20, ("HTTP requests: " + std::to_string(st.httpRequests) + " (remote " + std::to_string(st.remoteHttpRequests) + ")" +
            "   last peer: " + std::string(st.lastRemotePeer.is_empty() ? "-" : st.lastRemotePeer.c_str())).c_str());

        drawText(dc.m_hDC, 12, 132, W - 24, 20, "SERVER READ-AHEAD / BUFFER RESERVE");
        if (st.prebufferTargetBytes) {
            drawBar(dc.m_hDC, 12, 156, W - 24, 22, st.prebufferBytes, st.prebufferTargetBytes);
            const uint64_t pct = st.prebufferBytes * 100ULL / st.prebufferTargetBytes;
            drawText(dc.m_hDC, 12, 180, W / 2 - 18, 20, (std::to_string(pct) + "%  " + std::string(st.bufferState.c_str())).c_str());
            drawText(dc.m_hDC, W / 2, 180, W / 2 - 12, 20, (std::to_string(st.prebufferBytes / 1048576.0) + " / " + std::to_string(st.prebufferTargetBytes / 1048576.0) + " MB").c_str());
        } else {
            drawText(dc.m_hDC, 12, 156, W - 24, 20, st.streamingActive ? "Streaming without server read-ahead" : "Idle");
        }

        const double txMbps = static_cast<double>(st.bytesPerSecond) * 8.0 / 1000000.0;
        const double reqMbps = static_cast<double>(st.requiredBytesPerSecond) * 8.0 / 1000000.0;
        std::string stream = "AUDIO: " + std::string(st.streamingActive ? "TRANSMITTING" : "IDLE");
        if (st.streamingActive) stream += "  |  TX " + std::to_string(txMbps) + " Mbit/s  |  required " + std::to_string(reqMbps) + " Mbit/s";
        if (st.realtimeMultiplier > 0.0) stream += "  |  speed " + std::to_string(st.realtimeMultiplier) + "x realtime";
        drawText(dc.m_hDC, 12, 214, W - 24, 20, stream.c_str());

        std::string music = "MUSIC: " + std::string(st.streamTitle.is_empty() ? "-" : st.streamTitle.c_str());
        if (!st.streamArtist.is_empty()) music += "  |  " + std::string(st.streamArtist.c_str());
        if (!st.streamAlbum.is_empty()) music += "  |  " + std::string(st.streamAlbum.c_str());
        drawText(dc.m_hDC, 12, 238, W - 24, 20, music.c_str());

        std::string source = "SOURCE: " + std::string(st.sourceFormat.is_empty() ? "-" : st.sourceFormat.c_str()) +
            "  |  " + fmtMB(st.sourceFileSize) +
            "  |  " + fmtResolution(st.sourceSampleRate, st.sourceChannels, st.sourceBitsPerSample);
        drawText(dc.m_hDC, 12, 262, W - 24, 20, source.c_str());

        std::string output = "OUTPUT: " + std::string(st.outputFormat.is_empty() ? "-" : st.outputFormat.c_str()) +
            "  |  " + fmtMB(st.outputFileSize) +
            "  |  " + fmtResolution(st.dsdRate, st.streamChannels, st.streamBitsPerSample);
        drawText(dc.m_hDC, 12, 286, W - 24, 20, output.c_str());

        std::string pipeline = "PIPELINE: " + std::string(st.pipelineState.is_empty() ? "-" : st.pipelineState.c_str());
        drawText(dc.m_hDC, 12, 310, W - 24, 20, pipeline.c_str());

        std::string conversion = "CONVERSION: " + std::string(st.conversionState.is_empty() ? "-" : st.conversionState.c_str());
        if (st.conversionActive) conversion += "  |  progress " + std::to_string(st.conversionPercent) + "%";
        drawText(dc.m_hDC, 12, 334, W - 24, 20, conversion.c_str());

        std::string sdx = "T+A SDX: " + std::string(st.sdxDetected ? (st.sdxStreaming ? "DETECTED / STREAMING" : "DETECTED / IDLE") : "NOT DETECTED");
        if (!st.sdxIp.is_empty()) sdx += "  |  " + std::string(st.sdxIp.c_str());
        if (!st.sdxModelNumber.is_empty()) sdx += "  |  model " + std::string(st.sdxModelNumber.c_str());
        drawText(dc.m_hDC, 12, 358, W - 24, 20, sdx.c_str());

        std::string renderer = "Client: " + std::string(st.clientIp.is_empty() ? "-" : st.clientIp.c_str());
        if (!st.clientName.is_empty()) renderer += "  |  " + std::string(st.clientName.c_str());
        if (st.streamDuration > 0.0) renderer += "  |  duration " + std::to_string(st.streamDuration) + " s";
        drawText(dc.m_hDC, 12, 382, W - 24, 20, renderer.c_str());

        const std::string prefetch = "NEXT TRACK PREP: " + std::string(st.prefetchTitle.is_empty() ? "-" : st.prefetchTitle.c_str()) + "  |  " + std::string(st.prefetchState.is_empty() ? "IDLE" : st.prefetchState.c_str());
        drawText(dc.m_hDC, 12, 406, W - 24, 20, prefetch.c_str());

        std::string debug = "Diagnostics: " + std::string(sacd_dlna_cfg::debug_diagnostics ? "ON" : "OFF") +
            "  |  " + std::string(st.networkDiagnostic.is_empty() ? "No probe run" : st.networkDiagnostic.c_str());
        drawText(dc.m_hDC, 12, 430, W - 24, 36, debug.c_str());
        drawText(dc.m_hDC, 12, 476, W - 24, 20, "Left click: enable/disable DLNA   |   Right click: run network probe   |   Double click: Preferences");
    }

    void OnClick(UINT, CPoint) {
        const bool now = !SacdDlnaServer::instance().is_running();
        if (now && !sacd_plugin_installed()) {
            popup_message::g_show("foo_input_sacd.dll (Super Audio CD Decoder) is required.", "SACD DLNA");
            return;
        }
        sacd_dlna_cfg::enabled = now;
        SacdDlnaServer::instance().set_enabled(now);
        if (now && sacd_dlna_cfg::share_library) SacdDlnaServer::instance().share_music_library();
        Invalidate();
    }

    void OnRightClick(UINT, CPoint) {
        const bool ok = SacdDlnaServer::instance().run_network_diagnostics();
        const auto st = SacdDlnaServer::instance().get_status();
        popup_message::g_show(st.networkDiagnostic.c_str(), ok ? (st.networkPresence ? "SACD DLNA Network Probe: PASS / REMOTE VISIBLE" : "SACD DLNA Network Probe: LOCAL PASS / REMOTE PENDING") : "SACD DLNA Network Probe: CHECK REQUIRED");
        Invalidate();
    }

    void OnDoubleClick(UINT, CPoint) { standard_commands::main_preferences(); }
};

class ui_element_sacd_impl : public ui_element_impl_withpopup<CSacdDlnaWindow> {
public:
    bool get_popup_specs(ui_size& size, pfc::string_base& title) override { return CSacdDlnaWindow::g_get_popup_specs(size, title); }
};
static service_factory_single_t<ui_element_sacd_impl> g_ui_element_factory;
}
