#pragma once

#include "stdafx.h"
#include "config.h"
#include "dsf_writer.h"

class DsdProcessorBridge {
public:
    static bool installed(pfc::string_base* versionOut = nullptr);
    static bool find_default_preset(dsp_preset_impl& out);
    static bool load_saved_preset(dsp_preset_impl& out);
    static bool save_preset(const dsp_preset& preset);
    static bool configure(HWND parent);
    static std::string preset_fingerprint();

    // Returns true when the installed DSP Processor produced DoP/DSD chunks.
    static bool process_chunk(dsp_manager& manager, const metadb_handle_ptr& track,
                              const audio_chunk& input, std::vector<audio_chunk_impl>& output,
                              abort_callback& abort);
    static bool flush(dsp_manager& manager, const metadb_handle_ptr& track,
                      std::vector<audio_chunk_impl>& output, abort_callback& abort);

private:
    static bool find_entry(service_ptr_t<dsp_entry>& out);
};
