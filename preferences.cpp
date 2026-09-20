#include "stdafx.h"
#include "config.h"
#include "dlna_server.h"
#include "resource.h"
#include "tooltips.h"
#include "dsp_bridge.h"
#include <helpers/atl-misc.h>
#include <helpers/DarkMode.h>

namespace {
class preferences_impl : public CDialogImpl<preferences_impl>, public preferences_page_instance {
public:
    enum { IDD = IDD_SACD_DLNA_PREFERENCES };
    preferences_impl(preferences_page_callback::ptr callback) : m_callback(callback) {}

    BEGIN_MSG_MAP_EX(preferences_impl)
        MSG_WM_INITDIALOG(OnInitDialog)
        COMMAND_HANDLER_EX(IDC_ENABLE, BN_CLICKED, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_SHARE_LIBRARY, BN_CLICKED, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_SERVER_NAME, EN_CHANGE, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_PORT, EN_CHANGE, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_STABILITY_MODE, BN_CLICKED, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_PREBUFFER_SECONDS, EN_CHANGE, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_NETWORK_LOGGING, BN_CLICKED, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_DEBUG_DIAGNOSTICS, BN_CLICKED, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_NETWORK_PROBE, BN_CLICKED, OnNetworkProbe)
        COMMAND_HANDLER_EX(IDC_DSP_PROCESSOR, BN_CLICKED, OnChangedCommand)
        COMMAND_HANDLER_EX(IDC_DSP_CONFIGURE, BN_CLICKED, OnConfigureDsp)
        COMMAND_HANDLER_EX(IDC_REFRESH_LIBRARY, BN_CLICKED, OnRefreshLibrary)
        COMMAND_HANDLER_EX(IDC_OPEN_LIBRARY, BN_CLICKED, OnOpenLibrary)
        COMMAND_HANDLER_EX(IDC_CLEAR_LIBRARY, BN_CLICKED, OnClearLibrary)
        COMMAND_HANDLER_EX(IDC_CLEAR_CACHE, BN_CLICKED, OnClearCache)
        COMMAND_HANDLER_EX(IDC_SACD_HELP, BN_CLICKED, OnHelp)
        MSG_WM_TIMER(OnTimer)
    END_MSG_MAP()

    t_uint32 get_state() override {
        t_uint32 s = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (HasChanged()) s |= preferences_state::changed;
        return s;
    }

    void reset() override {
        CheckDlgButton(IDC_ENABLE, sacd_dlna_cfg::enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SHARE_LIBRARY, sacd_dlna_cfg::share_library ? BST_CHECKED : BST_UNCHECKED);
        ::SetDlgItemTextA(m_hWnd, IDC_SERVER_NAME, sacd_dlna_cfg::server_name.get().c_str());
        SetDlgItemInt(IDC_PORT, static_cast<UINT>(sacd_dlna_cfg::port.get()), FALSE);
        CheckDlgButton(IDC_STABILITY_MODE, sacd_dlna_cfg::stability_mode ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(IDC_PREBUFFER_SECONDS, static_cast<UINT>(sacd_dlna_cfg::prebuffer_seconds.get()), FALSE);
        CheckDlgButton(IDC_NETWORK_LOGGING, sacd_dlna_cfg::network_logging ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_DEBUG_DIAGNOSTICS, sacd_dlna_cfg::debug_diagnostics ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_DSP_PROCESSOR, sacd_dlna_cfg::dsd_processor_enabled ? BST_CHECKED : BST_UNCHECKED);
        OnChanged();
    }

    void apply() override {
        sacd_dlna_cfg::enabled = IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED;
        sacd_dlna_cfg::share_library = IsDlgButtonChecked(IDC_SHARE_LIBRARY) == BST_CHECKED;
        char name[256]{}; ::GetDlgItemTextA(m_hWnd, IDC_SERVER_NAME, name, static_cast<int>(sizeof(name))); sacd_dlna_cfg::server_name = name;
        BOOL ok = FALSE; const UINT p = ::GetDlgItemInt(m_hWnd, IDC_PORT, &ok, FALSE); if (ok) sacd_dlna_cfg::port = std::clamp<UINT>(p, 1024, 65535);
        sacd_dlna_cfg::stability_mode = IsDlgButtonChecked(IDC_STABILITY_MODE) == BST_CHECKED;
        BOOL bok = FALSE; const UINT b = ::GetDlgItemInt(m_hWnd, IDC_PREBUFFER_SECONDS, &bok, FALSE); if (bok) sacd_dlna_cfg::prebuffer_seconds = std::clamp<UINT>(b, 5, 60);
        sacd_dlna_cfg::network_logging = IsDlgButtonChecked(IDC_NETWORK_LOGGING) == BST_CHECKED;
        sacd_dlna_cfg::debug_diagnostics = IsDlgButtonChecked(IDC_DEBUG_DIAGNOSTICS) == BST_CHECKED;
        sacd_dlna_cfg::dsd_processor_enabled = IsDlgButtonChecked(IDC_DSP_PROCESSOR) == BST_CHECKED;

        if (sacd_dlna_cfg::dsd_processor_enabled && !DsdProcessorBridge::installed()) {
            sacd_dlna_cfg::dsd_processor_enabled = false;
            CheckDlgButton(IDC_DSP_PROCESSOR, BST_UNCHECKED);
            popup_message::g_show("foo_dsd_processor is not installed. Install the DSD Processor component before enabling DLNA DSP processing.", "SACD DLNA");
        }

        if (sacd_dlna_cfg::enabled) {
            if (!sacd_plugin_installed()) {
                sacd_dlna_cfg::enabled = false;
                CheckDlgButton(IDC_ENABLE, BST_UNCHECKED);
                popup_message::g_show("The Super Audio CD Decoder (foo_input_sacd.dll) is not installed. Install it before enabling SACD DLNA.", "SACD DLNA");
            } else {
                SacdDlnaServer::instance().set_enabled(true);
                if (sacd_dlna_cfg::share_library) SacdDlnaServer::instance().share_music_library();
            }
        } else {
            SacdDlnaServer::instance().set_enabled(false);
        }
        OnChanged();
    }

private:
    preferences_page_callback::ptr m_callback;
    fb2k::CDarkModeHooks m_dark;
    sacd_tooltips m_tips;

    BOOL OnInitDialog(CWindow, LPARAM) {
        m_dark.AddDialogWithControls(*this);
        m_tips.create(*this);
        m_tips.add(*this, IDC_ENABLE, "Starts/stops the UPnP/DLNA MediaServer and SSDP discovery. BROADCASTING means discoverable; it does not mean that audio is currently transmitting.");
        m_tips.add(*this, IDC_SHARE_LIBRARY, "Shares DSD-capable items from foobar2000 Media Library as Artist > Album > Track. Files are not moved.");
        m_tips.add(*this, IDC_SERVER_NAME, "Friendly name visible to UPnP/DLNA players. Example: foobar2000 SACD DSD.");
        m_tips.add(*this, IDC_PORT, "TCP port used for UPnP XML and audio HTTP delivery. Default is 8192.");
        m_tips.add(*this, IDC_STABILITY_MODE, "Separates SACD-to-DSD preparation from network delivery. Helps absorb short disk/network fluctuations without converting DSD to PCM.");
        m_tips.add(*this, IDC_PREBUFFER_SECONDS, "Read-ahead target in seconds. 15 s is the default; for DSD256 this is about 42.3 MB of raw stereo DSD.");
        m_tips.add(*this, IDC_NETWORK_LOGGING, "Writes SSDP, HTTP, renderer negotiation, cache and transfer diagnostics to the foobar2000 console. Keep off for normal use.");
        m_tips.add(*this, IDC_DEBUG_DIAGNOSTICS, "Adds live UPnP/DLNA/SSDP diagnostics to the status UI and enables diagnostic logging. It does not alter the audio format.");
        m_tips.add(*this, IDC_NETWORK_PROBE, "Runs a local HTTP validation of device.xml, ContentDirectory.xml and ConnectionManager.xml plus an SSDP MediaServer multicast self-probe.");
        m_tips.add(*this, IDC_DSP_PROCESSOR, "Routes DLNA source audio through the installed DSD Processor DSP. Configure its PCM→DSD and DSD rate mappings with the button next to this option. The network path will only accept a valid DSD/DoP result and always writes native DSF.");
        m_tips.add(*this, IDC_DSP_CONFIGURE, "Open the installed DSD Processor configuration dialog and save a private preset for DLNA. The normal foobar2000 DSP chain is not modified.");
        m_tips.add(*this, IDC_STATUS_DSP, "Shows whether foo_dsd_processor is installed and whether this component is using it for DLNA processing.");
        m_tips.add(*this, IDC_STATUS_DLNA, "BROADCASTING / ACTIVE means the server and SSDP discovery are running.");
        m_tips.add(*this, IDC_STATUS_SACD, "Shows whether the required Super Audio CD Decoder (foo_input_sacd) is installed and its detected version.");
        m_tips.add(*this, IDC_STATUS_LIBRARY, "Shows whether the Media Library is shared and how many DSD-capable items are exposed.");
        m_tips.add(*this, IDC_STATUS_STREAM, "ACTIVE / TRANSMITTING means an actual HTTP media transfer is in progress. TX is measured TCP transmit rate.");
        m_tips.add(*this, IDC_STATUS_SDX, "Shows detected T+A SDX identity/IP and whether that renderer is the active streaming client.");
        m_tips.add(*this, IDC_STATUS_BUFFER, "Shows server-side DSD read-ahead reserve, not the renderer's internal playback buffer. The state changes in real time during an HTTP stream.");
        m_tips.add(*this, IDC_STATUS_NETWORK, "Shows network presence, HTTP self-test, SSDP multicast self-probe and live discovery counters.");
        reset();
        SetTimer(1, 1000);
        UpdateStatus();
        return FALSE;
    }

    bool HasChanged() const {
        char name[256]{}; ::GetDlgItemTextA(m_hWnd, IDC_SERVER_NAME, name, static_cast<int>(sizeof(name)));
        BOOL ok = FALSE; const UINT p = ::GetDlgItemInt(m_hWnd, IDC_PORT, &ok, FALSE);
        BOOL bok = FALSE; const UINT b = ::GetDlgItemInt(m_hWnd, IDC_PREBUFFER_SECONDS, &bok, FALSE);
        return (IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED) != static_cast<bool>(sacd_dlna_cfg::enabled) ||
            (IsDlgButtonChecked(IDC_SHARE_LIBRARY) == BST_CHECKED) != static_cast<bool>(sacd_dlna_cfg::share_library) ||
            strcmp(name, sacd_dlna_cfg::server_name.get()) != 0 ||
            (ok && p != static_cast<UINT>(sacd_dlna_cfg::port.get())) ||
            (IsDlgButtonChecked(IDC_STABILITY_MODE) == BST_CHECKED) != static_cast<bool>(sacd_dlna_cfg::stability_mode) ||
            (bok && b != static_cast<UINT>(sacd_dlna_cfg::prebuffer_seconds.get())) ||
            (IsDlgButtonChecked(IDC_NETWORK_LOGGING) == BST_CHECKED) != static_cast<bool>(sacd_dlna_cfg::network_logging) ||
            (IsDlgButtonChecked(IDC_DEBUG_DIAGNOSTICS) == BST_CHECKED) != static_cast<bool>(sacd_dlna_cfg::debug_diagnostics) ||
            (IsDlgButtonChecked(IDC_DSP_PROCESSOR) == BST_CHECKED) != static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
    }

    void OnTimer(UINT_PTR) { UpdateStatus(); }

    void OnChangedCommand(UINT, int, CWindow) { UpdateStatus(); OnChanged(); }

    void OnChanged() { m_callback->on_state_changed(); }

    void UpdateStatus() {
        const auto st = SacdDlnaServer::instance().get_status();

        std::string line1 = std::string("DLNA: ") + (st.broadcasting ? "BROADCASTING / ACTIVE" : "STOPPED");
        if (st.broadcasting) line1 += " | HTTP " + std::string(st.httpReady ? "READY" : "NOT READY") + " | SSDP " + std::string(st.ssdpReady ? "READY" : "NOT READY");
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_DLNA, line1.c_str());

        std::string line2 = std::string("foo_input_sacd: ") + (st.sacdInstalled ? "INSTALLED" : "NOT INSTALLED");
        if (st.sacdInstalled && !st.sacdVersion.is_empty()) { line2 += " ("; line2 += st.sacdVersion; line2 += ")"; }
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_SACD, line2.c_str());

        std::string line3 = "Music Library: " + std::string(st.sharingLibrary ? "SHARING" : "NOT SHARING") + " (" + std::to_string(st.sharedCount) + " DSD tracks)";
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_LIBRARY, line3.c_str());

        const double mbps = static_cast<double>(st.bytesPerSecond) * 8.0 / 1000000.0;
        std::string line4 = std::string("Audio stream: ") + (st.streamingActive ? "ACTIVE / TRANSMITTING" : "IDLE");
        if (st.streamingActive) line4 += "   TX " + std::to_string(mbps) + " Mbit/s";
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_STREAM, line4.c_str());

        std::string line5 = "T+A SDX: " + std::string(st.sdxDetected ? (st.sdxStreaming ? "DETECTED / STREAMING" : "DETECTED / IDLE") : "NOT DETECTED");
        if (!st.sdxIp.is_empty()) line5 += "  " + std::string(st.sdxIp.c_str());
        if (!st.sdxModelNumber.is_empty()) line5 += "  model " + std::string(st.sdxModelNumber.c_str());
        if (st.sdxProtocolNegotiated) line5 += "  protocol negotiated";
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_SDX, line5.c_str());

        std::string line6 = "DSD read-ahead: ";
        if (st.streamingActive && st.prebufferTargetBytes) {
            const uint64_t pct = st.prebufferBytes * 100ULL / st.prebufferTargetBytes;
            line6 += st.bufferState.c_str();
            line6 += " | " + std::to_string(pct) + "%";
            line6 += " | " + std::to_string(st.prebufferBytes / 1048576.0) + "/" + std::to_string(st.prebufferTargetBytes / 1048576.0) + " MB";
        } else {
            line6 += st.stabilityMode ? "READY" : "OFF";
        }
        if (st.streamingActive && st.requiredBytesPerSecond) line6 += " | required " + std::to_string(static_cast<double>(st.requiredBytesPerSecond) * 8.0 / 1000000.0) + " Mbit/s";
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_BUFFER, line6.c_str());

        std::string line7 = "DSD Processor: ";
        if (!st.dsdProcessorInstalled) line7 += "NOT INSTALLED";
        else line7 += st.dsdProcessorEnabled ? "ENABLED" : "INSTALLED / BYPASS";
        if (st.dsdProcessorInstalled && !st.dsdProcessorVersion.is_empty()) { line7 += " ("; line7 += st.dsdProcessorVersion; line7 += ")"; }
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_DSP, line7.c_str());

        std::string line8 = "Network: ";
        line8 += st.networkPresence ? "PRESENCE CONFIRMED" : "NO EXTERNAL TRAFFIC SEEN";
        line8 += " | HTTP self-test " + std::string(st.httpSelfTestOk ? "OK" : "NOT RUN/FAILED");
        line8 += " | SSDP self-probe " + std::string(st.ssdpProbeOk ? "OK" : "NOT RUN/FAILED");
        line8 += " | NOTIFY loopback " + std::string(st.ssdpNotifyLoopbackOk ? "OK" : "not observed");
        line8 += " | NOTIFY " + std::to_string(st.ssdpAliveSent) + " | M-SEARCH rx " + std::to_string(st.ssdpMSearchReceived);
        ::SetDlgItemTextA(m_hWnd, IDC_STATUS_NETWORK, line8.c_str());
    }

    void OnNetworkProbe(UINT, int, CWindow) {
        const bool ok = SacdDlnaServer::instance().run_network_diagnostics();
        const auto st = SacdDlnaServer::instance().get_status();
        popup_message::g_show(st.networkDiagnostic.c_str(), ok ? "SACD DLNA Network Probe: PASS" : "SACD DLNA Network Probe: CHECK REQUIRED");
        UpdateStatus();
    }

    void OnConfigureDsp(UINT, int, CWindow) {
        if (!DsdProcessorBridge::installed()) {
            popup_message::g_show("Install foo_dsd_processor before configuring DLNA DSP processing.", "SACD DLNA");
            return;
        }
        if (!DsdProcessorBridge::configure(*this)) {
            popup_message::g_show("The DSD Processor preset was not changed.", "SACD DLNA");
        } else {
            popup_message::g_show("DLNA DSD Processor preset saved. It will be used for the next generated DSF stream and is included in cache invalidation.", "SACD DLNA");
        }
        UpdateStatus();
    }

    void OnRefreshLibrary(UINT, int, CWindow) {
        if (!sacd_plugin_installed()) { popup_message::g_show("foo_input_sacd.dll is required before the DSD library can be shared.", "SACD DLNA"); return; }
        SacdDlnaServer::instance().share_music_library();
        CheckDlgButton(IDC_SHARE_LIBRARY, BST_CHECKED); UpdateStatus(); OnChanged();
    }

    void OnOpenLibrary(UINT, int, CWindow) {
        library_manager::get()->show_preferences();
    }

    void OnClearLibrary(UINT, int, CWindow) {
        SacdDlnaServer::instance().clear_shared_library();
        sacd_dlna_cfg::share_library = false;
        CheckDlgButton(IDC_SHARE_LIBRARY, BST_UNCHECKED);
        UpdateStatus(); OnChanged();
    }

    void OnClearCache(UINT, int, CWindow) {
        SacdDlnaServer::instance().clear_persistent_cache();
        popup_message::g_show("The persistent DSF and artwork cache has been cleared. Original music files were not modified.", "SACD DLNA");
        UpdateStatus();
    }

    void OnHelp(UINT, int, CWindow) {
        const char* msg =
            "SACD DLNA\\n\\n"
            "This component exposes native DSD music from foobar2000 over UPnP/DLNA.\\n\\n"
            "Requirements:\\n"
            "- foobar2000 x64\\n"
            "- Super Audio CD Decoder (foo_input_sacd.dll) for SACD ISO\\n"
            "- DSD64, DSD128 or DSD256 source material\\n\\n"
            "DSD is kept native for the network stream. No DSD-to-PCM conversion is performed by this component.\\n\\n"
            "Music Library sharing uses foobar2000's Media Library and exposes DSD-capable files (ISO/DSF/DFF) grouped as Artist > Album > Track. Album art is served through UPnP when available.\\n\\n"
            "Status: BROADCASTING means SSDP/server discovery is active. TRANSMITTING means a renderer is actually downloading audio. TX is the measured TCP transmit rate. The T+A SDX state shows detected renderer identity and active-client correlation when possible.\\n\\n"
            "SACD ISO: the installed foo_input_sacd decoder supplies DSD through the public foobar2000 decoder interface; a persistent DSF cache is then delivered over HTTP/DLNA. Native DSF/DFF files are served directly.\\n\\n"
            "Stability Mode separates SACD conversion/cache work from network delivery. The pre-buffer/read-ahead is intended to absorb short disk/network fluctuations; it cannot compensate for a sustained network throughput deficit.\\n\\n"
            "Diagnostics: enable Verbose network logging to trace SSDP discovery, T+A renderer detection, ConnectionManager protocol negotiation, Browse/BrowseMetadata requests, media GET/Range requests, cache hits/misses, errors and stream termination in the foobar2000 Console.\\n\\n"
            "BrowseMetadata is handled separately from BrowseDirectChildren. StartingIndex and RequestedCount are honoured, and GetSystemUpdateID is exposed for Media Library change tracking.\\n\\n"
            "Gapless note: the server preserves track order and exact duration metadata, but gapless transition timing ultimately depends on the renderer/firmware because this component is a MediaServer rather than the SDX transport controller.\\n\\n"
            "For the full field-by-field reference, network examples and troubleshooting, see HELP.md, EXAMPLES.md and NETWORK_REQUIREMENTS.md in the project repository.\\n\\n"
            "This is an alpha development build. Protocol negotiation is renderer-aware, but final compatibility and gapless behaviour must be verified against the exact T+A SDX 3100 HV firmware in use.";
        popup_message::g_show(msg, "foo_sacd_dlna Help");
    }
};

class preferences_page_impl_sacd : public preferences_page_impl<preferences_impl> {
public:
    const char* get_name() { return "SACD DLNA"; }
    GUID get_guid() { return GUID{ 0x5fbb3c34, 0x8f75, 0x4e2d, { 0xb6, 0x7f, 0x1d, 0x37, 0x78, 0x8c, 0x0e, 0x29 } }; }
    GUID get_parent_guid() { return guid_tools; }
};
static preferences_page_factory_t<preferences_page_impl_sacd> g_preferences_factory;
}
