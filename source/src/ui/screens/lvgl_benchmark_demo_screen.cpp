#include "hmi_nexus/ui/screens/lvgl_benchmark_demo_screen.h"

#include <vector>

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

    auto* screen = static_cast<LvglBenchmarkDemoScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    const auto result = screen->navigateTo(kHomeScreenId);
    if (!result) {
        system::Logger::Warn("ui.lvgl.benchmark",
                             "Failed to navigate back to home screen: " + result.message());
    }
}

void AddBackButton(LvglBenchmarkDemoScreen* screen) {
    auto* button = lv_button_create(lv_layer_top());
    lv_obj_set_size(button, 132, 42);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x7C2D12), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, NavigateBackHomeCb, LV_EVENT_CLICKED, screen);
    lv_obj_move_foreground(button);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, "Back Home");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
}

std::vector<lv_obj_t*> CollectTopLayerChildrenSince(std::uint32_t baseline) {
    std::vector<lv_obj_t*> children;
    auto* top_layer = lv_layer_top();
    const std::uint32_t child_count = lv_obj_get_child_count(top_layer);
    for (std::uint32_t index = baseline; index < child_count; ++index) {
        auto* child = lv_obj_get_child(top_layer, static_cast<int32_t>(index));
        if (child != nullptr) {
            children.push_back(child);
        }
    }
    return children;
}

#endif

}  // namespace

std::string LvglBenchmarkDemoScreen::id() const {
    return "lvgl_benchmark_demo";
}

void LvglBenchmarkDemoScreen::build() {
    if (built_) {
        return;
    }

#if HMI_NEXUS_HAS_LVGL && HMI_NEXUS_HAS_LVGL_DEMOS
    auto* demo_screen = lv_obj_create(nullptr);
    if (demo_screen == nullptr) {
        system::Logger::Warn("ui.lvgl.benchmark",
                             "Failed to create dedicated LVGL benchmark screen");
        built_ = true;
        return;
    }

    top_layer_child_baseline_ = lv_obj_get_child_count(lv_layer_top());
    lv_screen_load(demo_screen);

    lv_demo_benchmark();
    screen_root_ = lv_screen_active();
    AddBackButton(this);
    built_ = true;
    system::Logger::Info("ui.lvgl.benchmark", "Built LVGL benchmark demo screen");
#else
    built_ = true;
    system::Logger::Warn("ui.lvgl.benchmark",
                         "LVGL benchmark demo is unavailable in current build");
#endif
}

void LvglBenchmarkDemoScreen::onShow() {
#if HMI_NEXUS_HAS_LVGL
    if (!built_) {
        build();
    }

    if (screen_root_ != nullptr) {
        lv_screen_load(static_cast<lv_obj_t*>(screen_root_));
    }
#endif

    system::Logger::Info("ui.lvgl.benchmark", "Showing LVGL benchmark demo screen");
}

void LvglBenchmarkDemoScreen::onHide() {
#if HMI_NEXUS_HAS_LVGL && HMI_NEXUS_HAS_LVGL_DEMOS
    if (screen_root_ != nullptr) {
        auto* old_screen = static_cast<lv_obj_t*>(screen_root_);

        for (lv_timer_t* timer = lv_timer_get_next(nullptr); timer != nullptr;) {
            lv_timer_t* next = lv_timer_get_next(timer);
            if (lv_timer_get_user_data(timer) == old_screen) {
                lv_timer_delete(timer);
            }
            timer = next;
        }

        for (auto* child : CollectTopLayerChildrenSince(top_layer_child_baseline_)) {
            lv_obj_delete_async(child);
        }

        lv_obj_delete_async(old_screen);
    }

    screen_root_ = nullptr;
    top_layer_child_baseline_ = 0;
    built_ = false;
#endif

    system::Logger::Info("ui.lvgl.benchmark", "Hiding LVGL benchmark demo screen");
}

}  // namespace hmi_nexus::ui
