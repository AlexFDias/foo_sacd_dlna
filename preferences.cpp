#include "stdafx.h"
#include "config.h"
#include "dlna_server.h"
#include "resource.h"
#include "dsp_bridge.h"
#include "dvd_audio_flac.h"
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

namespace {

// Root SACD DLNA page: intentionally small. The real controls live in the
// three child preference pages below it.
const GUID guid_sacd_dlna_root = { 0x5fbb3c34, 0x8f75, 0x4e2d, { 0xb6, 0x7f, 0x1d, 0x37, 0x78, 0x8c, 0x0e, 0x29 } };
const GUID guid_sacd_dlna_test = { 0x6b2e6f31, 0x7d9d, 0x4d5e, { 0x9c, 0x21, 0x42, 0x58, 0x7e, 0x0a, 0x31, 0x11 } };
const GUID guid_sacd_dlna_settings = { 0x8f1a0a52, 0x2f4c, 0x46e2, { 0xa7, 0x34, 0x5d, 0x91, 0x12, 0x6b, 0x48, 0x20 } };
const GUID guid_sacd_dlna_other = { 0x3c7e2a93, 0x5a18, 0x4a61, { 0xb4, 0x0f, 0x83, 0x2d, 0x66, 0x9c, 0x17, 0x54 } };

void update_status(HWND hwnd) {
    if (!::IsWindow(hwnd)) return;
    const auto st = SacdDlnaServer::instance().get_status();

    std::string line = std::string("DLNA: ") + (st.broadcasting ? "BROADCASTING / ACTIVE" : "STOPPED");
    if (st.broadcasting) line += " | HTTP " + std::string(st.httpReady ? "READY" : "NOT READY") + " | SSDP " + std::string(st.ssdpReady ? "READY" : "NOT READY");
    ::SetDlgItemTextA(hwnd, IDC_STATUS_DLNA, line.c_str());

    line = std::string("foo_input_sacd: ") + (st.sacdInstalled ? "INSTALLED" : "NOT INSTALLED");
    if (st.sacdInstalled && !st.sacdVersion.is_empty()) { line += " ("; line += st.sacdVersion; line += ")"; }
    line += " | DVD-Audio: " + std::string(st.dvdaInstalled ? "INSTALLED" : "NOT INSTALLED");
    ::SetDlgItemTextA(hwnd, IDC_STATUS_SACD, line.c_str());

    line = "Music Library: ";
    line += st.sharingLibrary ? "SHARING" : "NOT SHARING";
    line += " (" + std::to_string(st.sharedCount) + " tracks)";
    ::SetDlgItemTextA(hwnd, IDC_STATUS_LIBRARY, line.c_str());

    const double mbps = static_cast<double>(st.bytesPerSecond) * 8.0 / 1000000.0;
    line = "Audio stream: ";
    line += st.streamingActive ? "ACTIVE / TRANSMITTING" : "IDLE";
    if (st.streamingActive) line += "   TX " + std::to_string(mbps) + " Mbit/s";
    ::SetDlgItemTextA(hwnd, IDC_STATUS_STREAM, line.c_str());

    line = "T+A SDX: ";
    line += st.sdxDetected ? (st.sdxStreaming ? "DETECTED / STREAMING" : "DETECTED / IDLE") : "NOT DETECTED";
    if (!st.sdxIp.is_empty()) line += "  " + std::string(st.sdxIp.c_str());
    if (!st.sdxModelNumber.is_empty()) line += "  model " + std::string(st.sdxModelNumber.c_str());
    if (st.sdxProtocolNegotiated) line += "  protocol negotiated";
    ::SetDlgItemTextA(hwnd, IDC_STATUS_SDX, line.c_str());

    line = "DSD read-ahead: ";
    if (st.streamingActive && st.prebufferTargetBytes) {
        line += st.bufferState.c_str();
        line += " | " + std::to_string(st.prebufferBytes * 100ULL / st.prebufferTargetBytes) + "%";
        line += " | " + std::to_string(st.prebufferBytes / 1048576.0) + "/" + std::to_string(st.prebufferTargetBytes / 1048576.0) + " MB";
    } else line += st.stabilityMode ? "READY" : "OFF";
    if (st.streamingActive && st.requiredBytesPerSecond) line += " | required " + std::to_string(static_cast<double>(st.requiredBytesPerSecond) * 8.0 / 1000000.0) + " Mbit/s";
    ::SetDlgItemTextA(hwnd, IDC_STATUS_BUFFER, line.c_str());

    line = "DSD Processor: ";
    if (!st.dsdProcessorInstalled) line += "NOT INSTALLED";
    else line += st.dsdProcessorEnabled ? "ENABLED" : "INSTALLED / BYPASS";
    if (st.dsdProcessorInstalled && !st.dsdProcessorVersion.is_empty()) { line += " ("; line += st.dsdProcessorVersion; line += ")"; }
    ::SetDlgItemTextA(hwnd, IDC_STATUS_DSP, line.c_str());

    line = "Network: ";
    line += st.networkPresence ? "PRESENCE CONFIRMED" : "NO EXTERNAL TRAFFIC SEEN";
    line += " | HTTP self-test " + std::string(st.httpSelfTestOk ? "OK" : "NOT RUN/FAILED");
    line += " | SSDP self-probe " + std::string(st.ssdpProbeOk ? "OK" : "NOT RUN/FAILED");
    line += " | NOTIFY loopback " + std::string(st.ssdpNotifyLoopbackOk ? "OK" : "not observed");
    ::SetDlgItemTextA(hwnd, IDC_STATUS_NETWORK, line.c_str());

    line = "Clients: " + std::to_string(st.clientsTotal) + " total | " + std::to_string(st.clientsActive) + " active | " + std::to_string(st.clientsIdle) + " idle | streams " + std::to_string(st.streamSlotsUsed) + " of " + std::to_string(st.streamLimit) + " allowed";
    if (st.streamsRejected) line += " | " + std::to_string(st.streamsRejected) + " rejected";
    ::SetDlgItemTextA(hwnd, IDC_STATUS_CLIENTS, line.c_str());

    line = "Probe: ";
    line += st.networkDiagnostic.is_empty() ? "not run" : st.networkDiagnostic.c_str();
    ::SetDlgItemTextA(hwnd, IDC_STATUS_PROBE, line.c_str());
}

void load_settings(HWND hwnd) {
    CheckDlgButton(hwnd, IDC_ENABLE, sacd_dlna_cfg::enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_SHARE_LIBRARY, sacd_dlna_cfg::share_library ? BST_CHECKED : BST_UNCHECKED);
    ::SetDlgItemTextA(hwnd, IDC_SERVER_NAME, sacd_dlna_cfg::server_name.get().c_str());
    ::SetDlgItemInt(hwnd, IDC_PORT, static_cast<UINT>(sacd_dlna_cfg::port.get()), FALSE);
    ::SetDlgItemInt(hwnd, IDC_MAX_STREAMS, static_cast<UINT>(sacd_dlna_max_streams()), FALSE);
    ::SetDlgItemTextA(hwnd, IDC_SHARED_FORMATS, sacd_dlna_cfg::shared_formats.get().c_str());
    CheckDlgButton(hwnd, IDC_STABILITY_MODE, sacd_dlna_cfg::stability_mode ? BST_CHECKED : BST_UNCHECKED);
    ::SetDlgItemInt(hwnd, IDC_PREBUFFER_SECONDS, static_cast<UINT>(sacd_dlna_cfg::prebuffer_seconds.get()), FALSE);
    CheckDlgButton(hwnd, IDC_DSP_PROCESSOR, sacd_dlna_cfg::dsd_processor_enabled ? BST_CHECKED : BST_UNCHECKED);
}

bool save_settings(HWND hwnd) {
    sacd_dlna_cfg::enabled = IsDlgButtonChecked(hwnd, IDC_ENABLE) == BST_CHECKED;
    sacd_dlna_cfg::share_library = IsDlgButtonChecked(hwnd, IDC_SHARE_LIBRARY) == BST_CHECKED;

    char name[256]{};
    ::GetDlgItemTextA(hwnd, IDC_SERVER_NAME, name, static_cast<int>(sizeof(name)));
    sacd_dlna_cfg::server_name = name;

    BOOL ok = FALSE;
    const UINT port = ::GetDlgItemInt(hwnd, IDC_PORT, &ok, FALSE);
    if (ok) sacd_dlna_cfg::port = std::clamp<UINT>(port, 1024, 65535);

    BOOL mok = FALSE;
    const UINT streams = ::GetDlgItemInt(hwnd, IDC_MAX_STREAMS, &mok, FALSE);
    if (mok) sacd_dlna_cfg::max_streams = clientreg::clampStreams(streams);
    ::SetDlgItemInt(hwnd, IDC_MAX_STREAMS, static_cast<UINT>(sacd_dlna_max_streams()), FALSE);

    char formats[1024]{};
    ::GetDlgItemTextA(hwnd, IDC_SHARED_FORMATS, formats, static_cast<int>(sizeof(formats)));
    sacd_dlna_cfg::shared_formats = formats;

    sacd_dlna_cfg::stability_mode = IsDlgButtonChecked(hwnd, IDC_STABILITY_MODE) == BST_CHECKED;
    BOOL bok = FALSE;
    const UINT buffer = ::GetDlgItemInt(hwnd, IDC_PREBUFFER_SECONDS, &bok, FALSE);
    if (bok) sacd_dlna_cfg::prebuffer_seconds = std::clamp<UINT>(buffer, 5, 60);

    sacd_dlna_cfg::dsd_processor_enabled = IsDlgButtonChecked(hwnd, IDC_DSP_PROCESSOR) == BST_CHECKED;
    if (sacd_dlna_cfg::dsd_processor_enabled && !DsdProcessorBridge::installed()) {
        sacd_dlna_cfg::dsd_processor_enabled = false;
        CheckDlgButton(hwnd, IDC_DSP_PROCESSOR, BST_UNCHECKED);
        popup_message::g_show("Install foo_dsd_processor before enabling DLNA DSP processing.", "SACD DLNA");
    }

    if (sacd_dlna_cfg::enabled) {
        if (!sacd_plugin_installed() && !dvda_plugin_installed()) {
            sacd_dlna_cfg::enabled = false;
            CheckDlgButton(hwnd, IDC_ENABLE, BST_UNCHECKED);
            popup_message::g_show("Install foo_input_sacd (SACD) or foo_input_dvda (DVD-Audio) before enabling SACD DLNA.", "SACD DLNA");
            return false;
        }
        SacdDlnaServer::instance().set_enabled(true);
        if (sacd_dlna_cfg::share_library) SacdDlnaServer::instance().share_music_library();
    } else {
        SacdDlnaServer::instance().set_enabled(false);
    }
    return true;
}

class sacd_dlna_root_page : public CDialogImpl<sacd_dlna_root_page>, public preferences_page_instance {
public:
    enum { IDD = IDD_SACD_DLNA_PREFERENCES };
    sacd_dlna_root_page(preferences_page_callback::ptr callback) : m_callback(callback) {}
    ~sacd_dlna_root_page() { if (::IsWindow(m_hWnd)) ::KillTimer(m_hWnd, 1); }
    BEGIN_MSG_MAP_EX(sacd_dlna_root_page)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_TIMER(OnTimer)
    END_MSG_MAP()
    t_uint32 get_state() override { return preferences_state::resettable | preferences_state::dark_mode_supported; }
    void reset() override { update_summary(); }
    void apply() override {}
private:
    preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
    BOOL OnInitDialog(CWindow, LPARAM) { m_dark.AddDialogWithControls(*this); SetTimer(1, 1000); update_summary(); return TRUE; }
    void OnTimer(UINT_PTR) { update_summary(); }
    void update_summary() {
        const auto st = SacdDlnaServer::instance().get_status();
        std::string s = "DLNA: ";
        s += st.broadcasting ? "BROADCASTING" : "STOPPED";
        s += " | clients " + std::to_string(st.clientsTotal);
        s += " | streams " + std::to_string(st.streamSlotsUsed) + "/" + std::to_string(st.streamLimit);
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_SUMMARY, s.c_str());
    }
};

class sacd_dlna_test_page : public CDialogImpl<sacd_dlna_test_page>, public preferences_page_instance {
public:
    enum { IDD = IDD_SACD_DLNA_TEST };
    sacd_dlna_test_page(preferences_page_callback::ptr callback) : m_callback(callback) {}
    ~sacd_dlna_test_page() { if (::IsWindow(m_hWnd)) ::KillTimer(m_hWnd, 1); }
    BEGIN_MSG_MAP_EX(sacd_dlna_test_page)
        MSG_WM_INITDIALOG(OnInitDialog)
        MSG_WM_TIMER(OnTimer)
    END_MSG_MAP()
    t_uint32 get_state() override { return preferences_state::resettable | preferences_state::dark_mode_supported; }
    void reset() override { update_status(m_hWnd); }
    void apply() override {}
private:
    preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
    BOOL OnInitDialog(CWindow, LPARAM) { m_dark.AddDialogWithControls(*this); SetTimer(1, 1000); update_status(m_hWnd); return TRUE; }
    void OnTimer(UINT_PTR) { update_status(m_hWnd); }
};

class sacd_dlna_settings_page : public CDialogImpl<sacd_dlna_settings_page>, public preferences_page_instance {
public:
    enum { IDD = IDD_SACD_DLNA_SETTINGS };
    sacd_dlna_settings_page(preferences_page_callback::ptr callback) : m_callback(callback) {}
    BEGIN_MSG_MAP_EX(sacd_dlna_settings_page)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_ENABLE, BN_CLICKED, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_SHARE_LIBRARY, BN_CLICKED, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_SERVER_NAME, EN_CHANGE, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_PORT, EN_CHANGE, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_MAX_STREAMS, EN_CHANGE, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_SHARED_FORMATS, EN_CHANGE, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_STABILITY_MODE, BN_CLICKED, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_PREBUFFER_SECONDS, EN_CHANGE, OnAnyChanged)
        COMMAND_HANDLER_EX(IDC_DSP_PROCESSOR, BN_CLICKED, OnDspToggle)
    END_MSG_MAP()
    // Without preferences_state::changed here, foobar2000 never learns that anything on this
    // page was edited: the Preferences dialog's Apply button stays disabled and apply() is
    // never invoked, no matter what the user ticks or types (this was the actual cause behind
    // "Share Music Library doesn't stick" - it affected every field on this page, not just that
    // checkbox).
    t_uint32 get_state() override {
        return (m_dirty ? preferences_state::changed : 0) | preferences_state::resettable | preferences_state::dark_mode_supported;
    }
    void reset() override { load_settings(m_hWnd); markClean(); }
    void apply() override { save_settings(m_hWnd); markClean(); }
private:
    preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
    bool m_dirty = false;
    bool m_loading = false;   // true while load_settings() programmatically fills the controls
    BOOL OnInitDialog(CWindow, LPARAM) { m_dark.AddDialogWithControls(*this); m_loading = true; load_settings(m_hWnd); m_loading = false; return TRUE; }
    // SetDlgItemTextA/SetDlgItemInt (used by load_settings()) fire EN_CHANGE on edit controls
    // exactly like a real keystroke would; m_loading tells the two apart so reset()/OnInitDialog
    // never mark the page dirty on their own.
    void markClean() { m_dirty = false; }
    void OnAnyChanged(UINT, int, CWindow) {
        if (m_loading || m_dirty) return;
        m_dirty = true;
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }
    void OnDspToggle(UINT, int, CWindow) {
        if (IsDlgButtonChecked(IDC_DSP_PROCESSOR) == BST_CHECKED && !DsdProcessorBridge::installed()) {
            CheckDlgButton(IDC_DSP_PROCESSOR, BST_UNCHECKED);
            popup_message::g_show("Install foo_dsd_processor before enabling DLNA DSP processing.", "SACD DLNA");
        }
        OnAnyChanged(0, 0, CWindow());
    }
    // Configuring the DSD Processor itself is done from the Maintenance page now;
    // this page previously had its own second "Configure DSP" button on the same
    // IDC_DSP_CONFIGURE id, which was redundant (and drawn too small to read).
};

class sacd_dlna_other_page : public CDialogImpl<sacd_dlna_other_page>, public preferences_page_instance {
public:
    enum { IDD = IDD_SACD_DLNA_OTHER };
    sacd_dlna_other_page(preferences_page_callback::ptr callback) : m_callback(callback) {}
    BEGIN_MSG_MAP_EX(sacd_dlna_other_page)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_NETWORK_LOGGING, BN_CLICKED, OnChanged)
        COMMAND_HANDLER_EX(IDC_DEBUG_DIAGNOSTICS, BN_CLICKED, OnChanged)
        COMMAND_HANDLER_EX(IDC_NETWORK_PROBE, BN_CLICKED, OnProbe)
        COMMAND_HANDLER_EX(IDC_REFRESH_LIBRARY, BN_CLICKED, OnRefreshLibrary)
        COMMAND_HANDLER_EX(IDC_OPEN_LIBRARY, BN_CLICKED, OnOpenLibrary)
        COMMAND_HANDLER_EX(IDC_CLEAR_LIBRARY, BN_CLICKED, OnClearLibrary)
        COMMAND_HANDLER_EX(IDC_CLEAR_CACHE, BN_CLICKED, OnClearCache)
        COMMAND_HANDLER_EX(IDC_DSP_CONFIGURE, BN_CLICKED, OnConfigureDsp)
        COMMAND_HANDLER_EX(IDC_SACD_HELP, BN_CLICKED, OnHelp)
    END_MSG_MAP()
    // Same fix as the Settings page: report preferences_state::changed once one of the two
    // checkboxes here is actually ticked, otherwise Apply never calls save_other().
    t_uint32 get_state() override {
        return (m_dirty ? preferences_state::changed : 0) | preferences_state::resettable | preferences_state::dark_mode_supported;
    }
    void reset() override { load_other(); markClean(); }
    void apply() override { save_other(); markClean(); }
private:
    preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
    bool m_dirty = false;
    BOOL OnInitDialog(CWindow, LPARAM) { m_dark.AddDialogWithControls(*this); load_other(); return TRUE; }
    void load_other() {
        CheckDlgButton(IDC_NETWORK_LOGGING, sacd_dlna_cfg::network_logging ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_DEBUG_DIAGNOSTICS, sacd_dlna_cfg::debug_diagnostics ? BST_CHECKED : BST_UNCHECKED);
    }
    void save_other() {
        sacd_dlna_cfg::network_logging = IsDlgButtonChecked(IDC_NETWORK_LOGGING) == BST_CHECKED;
        sacd_dlna_cfg::debug_diagnostics = IsDlgButtonChecked(IDC_DEBUG_DIAGNOSTICS) == BST_CHECKED;
    }
    void markClean() { m_dirty = false; }
    void OnChanged(UINT, int, CWindow) {
        if (m_dirty) return;
        m_dirty = true;
        if (m_callback.is_valid()) m_callback->on_state_changed();
    }
    void OnProbe(UINT, int, CWindow) {
        const bool ok = SacdDlnaServer::instance().run_network_diagnostics();
        const auto st = SacdDlnaServer::instance().get_status();
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_PROBE, st.networkDiagnostic.is_empty() ? "Probe: not run" : (std::string("Probe: ") + st.networkDiagnostic.c_str()).c_str());
        popup_message::g_show(st.networkDiagnostic.c_str(), ok ? "SACD DLNA Network Probe: PASS" : "SACD DLNA Network Probe: CHECK REQUIRED");
    }
    void OnRefreshLibrary(UINT, int, CWindow) {
        if (!sacd_plugin_installed()) { popup_message::g_show("foo_input_sacd.dll is required before the DSD library can be shared.", "SACD DLNA"); return; }
        if (!SacdDlnaServer::instance().is_running()) { sacd_dlna_cfg::enabled = true; SacdDlnaServer::instance().set_enabled(true); }
        sacd_dlna_cfg::share_library = true;
        SacdDlnaServer::instance().share_music_library();
    }
    void OnOpenLibrary(UINT, int, CWindow) { library_manager::get()->show_preferences(); }
    void OnClearLibrary(UINT, int, CWindow) { SacdDlnaServer::instance().clear_shared_library(); sacd_dlna_cfg::share_library = false; popup_message::g_show("The shared Music Library has been cleared.", "SACD DLNA"); }
    void OnClearCache(UINT, int, CWindow) { SacdDlnaServer::instance().clear_persistent_cache(); popup_message::g_show("The persistent DSF and artwork cache has been cleared. Source files were not modified.", "SACD DLNA"); }
    void OnConfigureDsp(UINT, int, CWindow) {
        if (!DsdProcessorBridge::installed()) { popup_message::g_show("Install foo_dsd_processor before configuring DLNA DSP processing.", "SACD DLNA"); return; }
        DsdProcessorBridge::configure(*this);
    }
    void OnHelp(UINT, int, CWindow) {
        popup_message::g_show(
            "SACD DLNA\n\n"
            "Native DSD over UPnP/DLNA. SACD ISO is prepared as DSD/DSF and DVD-Audio sources are decoded through foo_input_dvda and served as FLAC.\n\n"
            "Use the child Preferences pages Status, Settings and Maintenance.",
            "foo_sacd_dlna Help");
    }
};

class sacd_dlna_root_factory : public preferences_page_impl<sacd_dlna_root_page> {
public:
    const char* get_name() { return "SACD DLNA"; }
    GUID get_guid() { return guid_sacd_dlna_root; }
    GUID get_parent_guid() { return guid_tools; }
};

class sacd_dlna_test_factory : public preferences_page_impl<sacd_dlna_test_page> {
public:
    const char* get_name() { return "Status"; }
    GUID get_guid() { return guid_sacd_dlna_test; }
    GUID get_parent_guid() { return guid_sacd_dlna_root; }
};

class sacd_dlna_settings_factory : public preferences_page_impl<sacd_dlna_settings_page> {
public:
    const char* get_name() { return "Settings"; }
    GUID get_guid() { return guid_sacd_dlna_settings; }
    GUID get_parent_guid() { return guid_sacd_dlna_root; }
};

class sacd_dlna_other_factory : public preferences_page_impl<sacd_dlna_other_page> {
public:
    const char* get_name() { return "Maintenance"; }
    GUID get_guid() { return guid_sacd_dlna_other; }
    GUID get_parent_guid() { return guid_sacd_dlna_root; }
};

static preferences_page_factory_t<sacd_dlna_root_factory> g_root_factory;
static preferences_page_factory_t<sacd_dlna_test_factory> g_test_factory;
static preferences_page_factory_t<sacd_dlna_settings_factory> g_settings_factory;
static preferences_page_factory_t<sacd_dlna_other_factory> g_other_factory;

}
