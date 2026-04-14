#include "hmi_nexus/ui/screens/lvgl_widgets_demo_screen.h"

#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_LVGL
#include "lvgl.h"
#endif

#if HMI_NEXUS_HAS_LVGL_DEMOS
#include "lv_demos.h"
#endif

namespace hmi_nexus::ui {
namespace {

#if HMI_NEXUS_HAS_LVGL

constexpr char kHomeScreenId[] = "home";

void NavigateBackHomeCb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* screen = static_cast<LvglWidgetsDemoScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    const auto result = screen->navigateTo(kHomeScreenId);
    if (!result) {
        system::Logger::Warn("ui.lvgl.demo",
                             "Failed to navigate back to home screen: " + result.message());
    }
}

void AddBackButton(lv_obj_t* parent, LvglWidgetsDemoScreen* screen) {
    auto* button = lv_button_create(parent);
    lv_obj_set_size(button, 120, 42);
    lv_obj_align(button, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x0F766E), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, NavigateBackHomeCb, LV_EVENT_CLICKED, screen);
    lv_obj_move_foreground(button);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, "Back Home");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
}

#endif

}  // namespace

std::string LvglWidgetsDemoScreen::id() const {
    return "lvgl_widgets_demo";
}

void LvglWidgetsDemoScreen::build() {
    if (built_) {
        return;
    }

#if HMI_NEXUS_HAS_LVGL && HMI_NEXUS_HAS_LVGL_DEMOS
    // LVGL widgets demo always renders to the current active screen, so create
    // and activate a dedicated root first to keep it isolated from other pages.
    auto* demo_screen = lv_obj_create(nullptr);
    if (demo_screen == nullptr) {
        system::Logger::Warn("ui.lvgl.demo", "Failed to create dedicated LVGL widgets demo screen");
        built_ = true;
        return;
    }

    lv_obj_set_style_bg_opa(demo_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(demo_screen, 0, 0);
    lv_obj_set_style_outline_width(demo_screen, 0, 0);
    lv_obj_set_style_radius(demo_screen, 0, 0);
    lv_obj_set_style_pad_all(demo_screen, 0, 0);
    lv_screen_load(demo_screen);

    lv_demo_widgets();
    screen_root_ = lv_screen_active();
    if (screen_root_ != nullptr) {
        AddBackButton(static_cast<lv_obj_t*>(screen_root_), this);
    }
    built_ = true;
    system::Logger::Info("ui.lvgl.demo", "Built LVGL widgets demo screen");
#else
    built_ = true;
    system::Logger::Warn("ui.lvgl.demo",
                         "LVGL widgets demo is unavailable in current build");
#endif
}

void LvglWidgetsDemoScreen::onShow() {
#if HMI_NEXUS_HAS_LVGL
    if (!built_) {
        build();
    }

    if (screen_root_ != nullptr) {
        lv_screen_load(static_cast<lv_obj_t*>(screen_root_));
    }
#endif

    system::Logger::Info("ui.lvgl.demo", "Showing LVGL widgets demo screen");
}

void LvglWidgetsDemoScreen::onHide() {
#if HMI_NEXUS_HAS_LVGL && HMI_NEXUS_HAS_LVGL_DEMOS
    if (screen_root_ != nullptr) {
        auto* old_screen = static_cast<lv_obj_t*>(screen_root_);
        lv_obj_delete_async(old_screen);
    }

    screen_root_ = nullptr;
    built_ = false;
#endif

    system::Logger::Info("ui.lvgl.demo", "Hiding LVGL widgets demo screen");
}

}  // namespace hmi_nexus::ui
