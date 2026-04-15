#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/system/ui_dispatcher.h"
#include "hmi_nexus/ui/lvgl_port.h"
#include "hmi_nexus/ui/screen_manager.h"
#include "hmi_nexus/ui/theme_manager.h"

namespace hmi_nexus::ui {

class App {
public:
    App(ScreenManager& screen_manager,
        ThemeManager& theme_manager,
        system::UiDispatcher& ui_dispatcher,
        LvglPort& lvgl_port);

    common::Result start(const std::string& initial_screen_id = "home");
    uint32_t tick();

private:
    ScreenManager& screen_manager_;
    ThemeManager& theme_manager_;
    system::UiDispatcher& ui_dispatcher_;
    LvglPort& lvgl_port_;
};

}  // namespace hmi_nexus::ui
