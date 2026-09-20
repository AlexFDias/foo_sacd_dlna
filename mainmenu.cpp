#include "stdafx.h"
#include "config.h"
#include "dlna_server.h"
#include "dsp_bridge.h"
#include <helpers/BumpableElem.h>

namespace {
static const GUID guid_group = { 0x8b0c48f0, 0x2f77, 0x4f32, { 0x9b, 0x42, 0x7e, 0x0c, 0x61, 0x49, 0x8c, 0x22 } };
static const GUID guid_toggle = { 0x5a1a8d35, 0x5f5c, 0x4d6a, { 0x9a, 0x2b, 0x03, 0x87, 0x6e, 0x76, 0x84, 0x11 } };
static const GUID guid_share = { 0x3d8f32b4, 0xaec4, 0x4d6d, { 0x8f, 0x9c, 0x92, 0x55, 0x45, 0x39, 0x72, 0xe1 } };
static const GUID guid_preferences = { 0x62a9bf62, 0x6d0b, 0x4d2b, { 0xb3, 0x0e, 0x2a, 0xdc, 0xbb, 0x1d, 0x80, 0x72 } };
static const GUID guid_help = { 0x0c747d24, 0x8c3c, 0x45d1, { 0x81, 0x2d, 0x0a, 0x6c, 0x11, 0x49, 0xe7, 0xc2 } };
static const GUID guid_dsp = { 0x1aa39fe6, 0x7716, 0x4c6c, { 0x87, 0xa8, 0x9e, 0x2c, 0x39, 0x55, 0x62, 0x0d } };
static const GUID guid_status = { 0x2bc428e1, 0x0d55, 0x4ab8, { 0xa1, 0x11, 0x74, 0xc1, 0x59, 0x93, 0x12, 0x9a } };
static const GUID guid_dsp_config = { 0xd3e0e0a5, 0x3bbf, 0x4fe7, { 0x86, 0xb0, 0x54, 0xa5, 0x63, 0xbb, 0x28, 0x3d } };
static const GUID guid_preferences_page = { 0x5fbb3c34, 0x8f75, 0x4e2d, { 0xb6, 0x7f, 0x1d, 0x37, 0x78, 0x8c, 0x0e, 0x29 } };
static const GUID guid_network_probe = { 0x1c7e7c88, 0x3ad5, 0x4f84, { 0xb7, 0x3c, 0x1b, 0xc5, 0x91, 0x6e, 0x27, 0x5a } };
static const GUID guid_refresh_library = { 0x8d2f5e7c, 0xa4d3, 0x4c88, { 0x91, 0x6a, 0x39, 0x6c, 0x55, 0x47, 0x71, 0x2b } };
static const GUID guid_clear_library = { 0x4bb3e1f8, 0x7d2d, 0x46e7, { 0x82, 0x5c, 0x10, 0x5a, 0x74, 0x33, 0x8d, 0x5f } };
static const GUID guid_clear_cache = { 0x0efc5d21, 0x5cb5, 0x4b77, { 0x9d, 0x21, 0x4b, 0x7a, 0x3d, 0x8f, 0x69, 0x24 } };
static const GUID guid_debug = { 0x6c1d8e4b, 0x2e0c, 0x4c0f, { 0x9b, 0x51, 0x38, 0x71, 0x56, 0x90, 0xa4, 0x0e } };
static const GUID guid_sacd_dlna_element = { 0x8c0fe7e9, 0x5f8f, 0x4d33, { 0x9d, 0x9c, 0x2b, 0x64, 0xa8, 0x3d, 0x20, 0x11 } };

static mainmenu_group_popup_factory g_group(guid_group, mainmenu_groups::view, mainmenu_commands::sort_priority_dontcare, "SACD DLNA");

class commands : public mainmenu_commands {
public:
    enum { toggle, share, dsp, status, network_probe, debug, dsp_config, refresh_library, clear_library, clear_cache, preferences, help, total };
    t_uint32 get_command_count() override { return total; }
    GUID get_command(t_uint32 i) override {
        switch (i) {
        case toggle: return guid_toggle;
        case share: return guid_share;
        case dsp: return guid_dsp;
        case status: return guid_status;
        case network_probe: return guid_network_probe;
        case refresh_library: return guid_refresh_library;
        case clear_library: return guid_clear_library;
        case clear_cache: return guid_clear_cache;
        case debug: return guid_debug;
        case dsp_config: return guid_dsp_config;
        case preferences: return guid_preferences;
        case help: return guid_help;
        default: uBugCheck();
        }
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        switch (i) {
        case toggle: out = "Enable DLNA broadcasting"; break;
        case share: out = "Share DSD Music Library"; break;
        case dsp: out = "Enable DLNA DSD Processor"; break;
        case status: out = "Open SACD DLNA Status"; break;
        case network_probe: out = "Run Network Diagnostics"; break;
        case debug: out = "Enable Debug Diagnostics"; break;
        case dsp_config: out = "Configure DSD Processor..."; break;
        case refresh_library: out = "Refresh DSD Music Library"; break;
        case clear_library: out = "Stop Sharing Music Library"; break;
        case clear_cache: out = "Clear Persistent Cache"; break;
        case preferences: out = "Open SACD DLNA Preferences"; break;
        case help: out = "Help"; break;
        default: uBugCheck();
        }
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        switch (i) {
        case toggle: out = "Starts or stops the SACD DLNA Media Server."; return true;
        case share: out = "Publishes DSD-capable content from foobar2000's Media Library."; return true;
        case dsp: out = "Routes DLNA audio through the installed DSD Processor DSP."; return true;
        case status: out = "Opens the SACD DLNA status panel. Existing instances are activated when available."; return true;
        case network_probe: out = "Checks local UPnP XML endpoints and performs an SSDP MediaServer multicast self-probe."; return true;
        case debug: out = "Enables extra live UPnP/DLNA diagnostics and diagnostic logging."; return true;
        case dsp_config: out = "Opens the installed DSD Processor configuration dialog used by SACD DLNA."; return true;
        case refresh_library: out = "Re-indexes the configured foobar2000 Music Library for DSD-capable DLNA items."; return true;
        case clear_library: out = "Stops publishing the currently shared DSD Music Library without stopping the DLNA server."; return true;
        case clear_cache: out = "Removes generated DSF/artwork cache files without modifying the source music."; return true;
        case preferences: out = "Opens the dedicated SACD DLNA preferences page directly."; return true;
        case help: out = "Show SACD DLNA requirements and usage notes."; return true;
        default: return false;
        }
    }
    GUID get_parent() override { return guid_group; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        switch (i) {
        case toggle: {
            const bool on = !SacdDlnaServer::instance().is_running();
            if (on && !sacd_plugin_installed()) { popup_message::g_show("foo_input_sacd.dll (Super Audio CD Decoder) is required.", "SACD DLNA"); return; }
            sacd_dlna_cfg::enabled = on; SacdDlnaServer::instance().set_enabled(on);
            if (on && sacd_dlna_cfg::share_library) SacdDlnaServer::instance().share_music_library();
            break;
        }
        case dsp:
            if (!dsd_processor_installed()) { popup_message::g_show("foo_dsd_processor.dll is required.", "SACD DLNA"); return; }
            sacd_dlna_cfg::dsd_processor_enabled = !static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled);
            if (static_cast<bool>(sacd_dlna_cfg::share_library) && SacdDlnaServer::instance().is_running()) SacdDlnaServer::instance().share_music_library();
            break;
        case share: {
            const bool enableShare = !static_cast<bool>(sacd_dlna_cfg::share_library);
            if (enableShare) {
                if (!sacd_plugin_installed()) { popup_message::g_show("foo_input_sacd.dll (Super Audio CD Decoder) is required.", "SACD DLNA"); return; }
                sacd_dlna_cfg::share_library = true;
                if (!SacdDlnaServer::instance().is_running()) { sacd_dlna_cfg::enabled = true; SacdDlnaServer::instance().set_enabled(true); }
                SacdDlnaServer::instance().share_music_library();
            } else {
                SacdDlnaServer::instance().clear_shared_library();
                sacd_dlna_cfg::share_library = false;
            }
            break;
        }
        case status:
            ui_element_common_methods_v2::get()->spawn_host_simple(core_api::get_main_window(), guid_sacd_dlna_element, false);
            break;
        case network_probe: {
            const bool ok = SacdDlnaServer::instance().run_network_diagnostics();
            const auto st = SacdDlnaServer::instance().get_status();
            popup_message::g_show(st.networkDiagnostic.c_str(), ok ? (st.networkPresence ? "SACD DLNA Network Probe: PASS / REMOTE VISIBLE" : "SACD DLNA Network Probe: LOCAL PASS / REMOTE PENDING") : "SACD DLNA Network Probe: CHECK REQUIRED");
            break;
        }
        case debug:
            sacd_dlna_cfg::debug_diagnostics = !static_cast<bool>(sacd_dlna_cfg::debug_diagnostics);
            break;
        case dsp_config:
            if (!DsdProcessorBridge::installed()) { popup_message::g_show("foo_dsd_processor.dll is required.", "SACD DLNA"); return; }
            if (!DsdProcessorBridge::configure(core_api::get_main_window())) { popup_message::g_show("The DSD Processor configuration dialog was not opened or the preset was not changed.", "SACD DLNA"); }
            else { SacdDlnaServer::instance().share_music_library(); }
            break;
        case refresh_library:
            if (!sacd_plugin_installed()) { popup_message::g_show("foo_input_sacd.dll (Super Audio CD Decoder) is required.", "SACD DLNA"); return; }
            if (!SacdDlnaServer::instance().is_running()) { sacd_dlna_cfg::enabled = true; SacdDlnaServer::instance().set_enabled(true); }
            sacd_dlna_cfg::share_library = true;
            SacdDlnaServer::instance().share_music_library();
            break;
        case clear_library:
            SacdDlnaServer::instance().clear_shared_library();
            sacd_dlna_cfg::share_library = false;
            break;
        case clear_cache:
            SacdDlnaServer::instance().clear_persistent_cache();
            popup_message::g_show("The persistent SACD DLNA cache has been cleared. Source music files were not modified.", "SACD DLNA");
            break;
        case preferences:
            ui_control::get()->show_preferences(guid_preferences_page);
            break;
        case help: popup_message::g_show("Enable DLNA broadcasting, then share the DSD portion of foobar2000's Music Library. The server keeps DSD native and exposes DSF over UPnP/DLNA. foo_input_sacd.dll is required for SACD/DSD decoding.", "foo_sacd_dlna Help"); break;
        default: uBugCheck();
        }
    }
    bool get_display(t_uint32 i, pfc::string_base& text, t_uint32& flags) override {
        const bool rv = mainmenu_commands::get_display(i, text, flags);
        if (rv && i == toggle && SacdDlnaServer::instance().is_running()) flags |= flag_checked;
        if (rv && i == share && static_cast<bool>(sacd_dlna_cfg::share_library)) flags |= flag_checked;
        if (rv && i == clear_library && !static_cast<bool>(sacd_dlna_cfg::share_library)) flags |= flag_disabled;
        if (rv && i == dsp && static_cast<bool>(sacd_dlna_cfg::dsd_processor_enabled)) flags |= flag_checked;
        if (rv && i == debug && static_cast<bool>(sacd_dlna_cfg::debug_diagnostics)) flags |= flag_checked;
        return rv;
    }
};
static mainmenu_commands_factory_t<commands> g_commands;
}
