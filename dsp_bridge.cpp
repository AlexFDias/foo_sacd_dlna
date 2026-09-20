#include "stdafx.h"
#include "dsp_bridge.h"

namespace {

static bool nameLooksLikeDsdProcessor(const char* name) {
    if (!name) return false;
    const std::string n = name;
    return _stricmp(n.c_str(), "DSD Processor") == 0 ||
        (n.find("DSD") != std::string::npos && n.find("Processor") != std::string::npos);
}

static uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

static uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}

bool DsdProcessorBridge::installed(pfc::string_base* versionOut) {
    return dsd_processor_installed(versionOut);
}

bool DsdProcessorBridge::find_entry(service_ptr_t<dsp_entry>& out) {
    service_enum_t<dsp_entry> e;
    dsp_entry::ptr ptr;
    while (e.next(ptr)) {
        pfc::string8 name;
        ptr->get_name(name);
        if (nameLooksLikeDsdProcessor(name)) { out = ptr; return true; }
    }
    return false;
}

bool DsdProcessorBridge::find_default_preset(dsp_preset_impl& out) {
    service_ptr_t<dsp_entry> entry;
    if (!find_entry(entry)) return false;
    return entry->get_default_preset(out);
}

bool DsdProcessorBridge::load_saved_preset(dsp_preset_impl& out) {
    dsp_chain_config_impl chain;
    sacd_dlna_cfg::dsd_processor_preset.get_data(chain);
    if (chain.get_count() == 0) return find_default_preset(out);
    if (chain.get_count() != 1) return find_default_preset(out);
    out = chain.get_item(0);
    if (!out.is_valid() || !dsp_entry::g_dsp_exists(out.get_owner())) return find_default_preset(out);
    return true;
}

bool DsdProcessorBridge::save_preset(const dsp_preset& preset) {
    dsp_chain_config_impl chain;
    chain.add_item(preset);
    sacd_dlna_cfg::dsd_processor_preset.set_data(chain);
    return true;
}

bool DsdProcessorBridge::configure(HWND parent) {
    if (!installed()) return false;
    service_ptr_t<dsp_entry> entry;
    if (!find_entry(entry)) return false;
    if (!entry->have_config_popup()) return false;
    dsp_preset_impl preset;
    if (!load_saved_preset(preset)) return false;
    if (!dsp_entry::g_show_config_popup(preset, parent)) return false;
    return save_preset(preset);
}

std::string DsdProcessorBridge::preset_fingerprint() {
    dsp_preset_impl preset;
    if (!load_saved_preset(preset)) return "none";
    const GUID owner = preset.get_owner();
    std::vector<uint8_t> bytes(sizeof(GUID) + preset.get_data_size());
    memcpy(bytes.data(), &owner, sizeof(GUID));
    if (preset.get_data_size()) memcpy(bytes.data() + sizeof(GUID), preset.get_data(), preset.get_data_size());
    char hex[32]{};
    snprintf(hex, sizeof(hex), "%016llX", static_cast<unsigned long long>(fnv1a(bytes.data(), bytes.size())));
    return hex;
}

bool DsdProcessorBridge::process_chunk(dsp_manager& manager, const metadb_handle_ptr& track,
                                       const audio_chunk& input, std::vector<audio_chunk_impl>& output,
                                       abort_callback& abort) {
    dsp_chunk_list_impl list;
    list.add_chunk(&input);
    manager.run(&list, track, 0, abort);
    bool had = false;
    for (t_size i = 0; i < list.get_count(); ++i) {
        auto* c = list.get_item(i);
        if (!c || c->is_empty()) continue;
        output.emplace_back(*c);
        had = true;
    }
    return had;
}

bool DsdProcessorBridge::flush(dsp_manager& manager, const metadb_handle_ptr& track,
                               std::vector<audio_chunk_impl>& output, abort_callback& abort) {
    dsp_chunk_list_impl list;
    manager.run(&list, track, dsp::FLUSH, abort);
    bool had = false;
    for (t_size i = 0; i < list.get_count(); ++i) {
        auto* c = list.get_item(i);
        if (!c || c->is_empty()) continue;
        output.emplace_back(*c);
        had = true;
    }
    return had;
}
