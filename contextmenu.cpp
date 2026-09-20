#include "stdafx.h"
#include "config.h"
#include "dlna_server.h"

namespace {
static const GUID guid_group = { 0x4d3c7b8a, 0x1c42, 0x4d5f, { 0x93, 0x21, 0x55, 0x9d, 0x2a, 0x0c, 0x1e, 0x77 } };

class group : public contextmenu_group_popup_factory {
public:
    group() : contextmenu_group_popup_factory(guid_group, contextmenu_groups::root, "SACD DLNA", 0) {}
};
static group g_group;

class item : public contextmenu_item_simple {
public:
    GUID get_parent() override { return guid_group; }
    unsigned get_num_items() override { return 1; }
    void get_item_name(unsigned, pfc::string_base& out) override { out = "Publish selected SACD tracks"; }
    void context_command(unsigned, metadb_handle_list_cref data, const GUID&) override {
        try {
            if (!sacd_plugin_installed()) {
            popup_message::g_show("foo_input_sacd.dll (Super Audio CD Decoder) is required.", "SACD DLNA");
            return;
        }
        if (!SacdDlnaServer::instance().is_running()) {
            sacd_dlna_cfg::enabled = true;
            SacdDlnaServer::instance().set_enabled(true);
        }
        SacdDlnaServer::instance().publish(data);
            popup_message::g_show("Selected SACD tracks were decoded as native DSD and published to the local UPnP server.", "SACD DLNA");
        } catch (std::exception const& e) {
            popup_message::g_complain("SACD DLNA", e);
        }
    }
    GUID get_item_guid(unsigned) override {
        return { 0x2c1b8f7e, 0x5d3e, 0x4c91, { 0x8a, 0x1f, 0x9e, 0x73, 0x24, 0x0b, 0x91, 0x44 } };
    }
    bool get_item_description(unsigned, pfc::string_base& out) override {
        out = "Use the installed Super Audio CD Decoder (foo_input_sacd) and expose selected SACD ISO tracks as native DSD/DSF over UPnP/DLNA.";
        return true;
    }
};
static contextmenu_item_factory_t<item> g_item;
}
