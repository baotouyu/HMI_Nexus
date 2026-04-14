#include "hmi_nexus/ui/app.h"

namespace hmi_nexus::ui {

App::App(ScreenManager& screen_manager,
         ThemeManager& theme_manager,
         system::UiDispatcher& ui_dispatcher,
         LvglPort& lvgl_port)
    : screen_manager_(screen_manager),
      theme_manager_(theme_manager),
      ui_dispatcher_(ui_dispatcher),
      lvgl_port_(lvgl_port) {}

common::Result App::start(const std::string& initial_screen_id) {
    auto lvgl_result = lvgl_port_.initialize();
    if (!lvgl_result) {
        return lvgl_result;
    }

    theme_manager_.setActiveTheme("default");
    auto theme_result = lvgl_port_.applyTheme(theme_manager_.activeTheme());
    if (!theme_result) {
        return theme_result;
    }

    return screen_manager_.show(initial_screen_id.empty() ? "home" : initial_screen_id);
}

void App::tick() {
    ui_dispatcher_.drain();
    lvgl_port_.pump();
}

}  // namespace hmi_nexus::ui
