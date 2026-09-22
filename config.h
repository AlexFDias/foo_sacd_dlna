#pragma once

#include "stdafx.h"
#include <SDK/cfg_var.h>
#include <helpers/cfg_dsp_chain_config.h>

namespace sacd_dlna_cfg {
    extern const GUID guid_cfg_enabled;
    extern const GUID guid_cfg_share_library;
    extern const GUID guid_cfg_port;
    extern const GUID guid_cfg_server_name;
    extern const GUID guid_cfg_stability_mode;
    extern const GUID guid_cfg_prebuffer_seconds;
    extern const GUID guid_cfg_network_logging;
    extern const GUID guid_cfg_debug_diagnostics;
    extern const GUID guid_cfg_shared_formats;
    extern const GUID guid_cfg_max_streams;
    extern const GUID guid_cfg_dsd_processor_enabled;
    extern const GUID guid_cfg_dsd_processor_preset;

    extern cfg_bool enabled;
    extern cfg_bool share_library;
    extern cfg_uint port;
    extern cfg_string server_name;
    extern cfg_bool stability_mode;
    extern cfg_uint prebuffer_seconds;
    extern cfg_bool network_logging;
    extern cfg_bool debug_diagnostics;
    extern cfg_string shared_formats;
    extern cfg_uint max_streams;             // maximum simultaneous audio streams (1..16, default 2)
    extern cfg_bool dsd_processor_enabled;
    extern cfg_dsp_chain_config dsd_processor_preset;
}

// Concurrent-stream limit from Preferences, always inside 1..16.
uint32_t sacd_dlna_max_streams();

bool sacd_plugin_installed(pfc::string_base* versionOut = nullptr);
bool dsd_processor_installed(pfc::string_base* versionOut = nullptr);
bool dvda_plugin_installed(pfc::string_base* versionOut = nullptr);
const char* sacd_plugin_required_name();
