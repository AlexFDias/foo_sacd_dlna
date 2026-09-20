#include "stdafx.h"
#include "dlna_server.h"
#include "config.h"

namespace {
class initquit_impl : public initquit {
public:
    void on_init() override {
        if (sacd_dlna_cfg::enabled) {
            SacdDlnaServer::instance().start();
            if (sacd_dlna_cfg::share_library && SacdDlnaServer::instance().is_running()) SacdDlnaServer::instance().share_music_library();
        }
    }
    void on_quit() override { SacdDlnaServer::instance().stop(); }
};
FB2K_SERVICE_FACTORY(initquit_impl);
}
