#include "hmi_nexus/ui/screens/home_screen.h"

#include <sstream>

#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_LVGL
#include "lvgl.h"
#endif

namespace hmi_nexus::ui {
namespace {

#if HMI_NEXUS_HAS_LVGL

constexpr char kGe2dDiagnosticsScreenId[] = "ge2d_diagnostics";
constexpr char kLvglBenchmarkDemoScreenId[] = "lvgl_benchmark_demo";
constexpr char kLvglWidgetsDemoScreenId[] = "lvgl_widgets_demo";

std::string JoinPoints(const std::vector<int>& points) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0) {
            stream << "  ";
        }
        stream << points[index];
    }
    return stream.str();
}

std::string BuildSeriesSummary(const std::vector<widgets::ChartSeries>& charts) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < charts.size(); ++index) {
        const auto& series = charts[index];
        const int latest = series.points.empty() ? 0 : series.points.back();
        stream << series.name << "  latest " << latest << "  samples " << JoinPoints(series.points);
        if (index + 1U < charts.size()) {
            stream << '\n';
        }
    }
    return stream.str();
}

std::string BuildLongScrollNotes() {
    return
        "Scroll Verification Notes\n"
        "01. This page intentionally keeps a long vertical text body so you can verify real touch drag, inertia, and page-flip smoothness on the panel.\n"
        "02. The current layout is still performance-oriented: it prefers a few multiline labels instead of many rounded cards, layered panels, or decorative blocks.\n"
        "03. If you see stutter while dragging this text-only page, the bottleneck is much more likely to be LVGL text redraw or page invalidation than framebuffer present.\n"
        "04. Direct framebuffer page-flip is already active, so the display backend should now be close to the official SDK path in rotation 0 mode.\n"
        "05. GE2D acceleration is better suited to image, layer, and some fill tasks; plain text-heavy pages still spend most time in LVGL software drawing.\n"
        "06. Long pages like this are useful because they expose whether the system can maintain acceptable redraw cost during sustained finger movement rather than only short taps.\n"
        "07. Network runtime, touch input, TLS, MQTT, WebSocket, logging, and JSON helpers are all initialized in the background, but this screen tries to keep visual cost predictable.\n"
        "08. When you compare versions, watch the ui.lvgl.perf log: handler average and max values are the fastest way to tell whether a UI refactor actually helped.\n"
        "09. A healthy direction is lower handler average, fewer over16.67ms events, and a visibly more stable present rate while you continuously drag the page up and down.\n"
        "10. If needed later, static text blocks can be pre-rendered as image assets, which often gives the hardware path more opportunities than pure label-based composition.\n"
        "11. For product pages, reserve rich visuals for the top-level dashboard and move stress tools, diagnostics, engineering counters, and animated validation widgets to dedicated screens.\n"
        "12. This section is intentionally long so you can keep scrolling for a while, stop midway, reverse direction, and observe whether rebound, momentum, and re-entry feel consistent.\n"
        "13. You can also use this page to verify touch orientation, coordinate stability, and whether rapid short drags differ noticeably from long slow drags.\n"
        "14. Once this text page feels smooth enough, the next step is usually to add visuals back in gradually and measure exactly which widget style starts to hurt frame time.\n"
        "15. The target is not just a fast benchmark page, but a maintainable HMI structure where heavy diagnostics live separately and the daily product path stays responsive.\n";
}

void NavigateToGe2dDiagnosticsCb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* screen = static_cast<HomeScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    const auto result = screen->navigateTo(kGe2dDiagnosticsScreenId);
    if (!result) {
        system::Logger::Warn("ui.home",
                             "Failed to open GE2D diagnostics screen: " +
                                 result.message());
    }
}

void NavigateToLvglWidgetsDemoCb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* screen = static_cast<HomeScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    const auto result = screen->navigateTo(kLvglWidgetsDemoScreenId);
    if (!result) {
        system::Logger::Warn("ui.home",
                             "Failed to open LVGL widgets demo screen: " +
                                 result.message());
    }
}

void NavigateToLvglBenchmarkDemoCb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* screen = static_cast<HomeScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    const auto result = screen->navigateTo(kLvglBenchmarkDemoScreenId);
    if (!result) {
        system::Logger::Warn("ui.home",
                             "Failed to open LVGL benchmark demo screen: " +
                                 result.message());
    }
}

void SetBodyLabelStyle(lv_obj_t* label, lv_color_t text_color) {
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_obj_set_style_text_line_space(label, 4, 0);
}

void AddActionButton(lv_obj_t* parent,
                     const char* title,
                     lv_color_t background_color,
                     lv_color_t text_color,
                     bool outlined = false,
                     lv_event_cb_t event_cb = nullptr,
                     void* user_data = nullptr) {
    auto* button = lv_button_create(parent);
    lv_obj_set_height(button, 46);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_border_width(button, outlined ? 1 : 0, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0xD6D3D1), 0);
    lv_obj_set_style_bg_color(button, background_color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(button, 16, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    if (event_cb != nullptr) {
        lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, user_data);
    }

    auto* label = lv_label_create(button);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_obj_center(label);
}

#endif

}  // namespace

HomeScreen::HomeScreen() {
    login_.user_label = "Operator";
    login_.online = true;
    charts_.push_back({"temperature", {22, 23, 25, 24}});
    charts_.push_back({"power", {12, 13, 15, 14}});
}

std::string HomeScreen::id() const {
    return "home";
}

void HomeScreen::build() {
#if HMI_NEXUS_HAS_LVGL
    if (built_) {
        return;
    }

    auto* screen = lv_obj_create(nullptr);
    screen_root_ = screen;
    built_ = true;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF5F1E8), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_outline_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 16, 0);
    lv_obj_set_style_pad_bottom(screen, 28, 0);
    lv_obj_set_style_pad_gap(screen, 10, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    auto* title = lv_label_create(screen);
    lv_label_set_text(title, "HMI Nexus");
    lv_obj_set_style_text_color(title, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    auto* subtitle = lv_label_create(screen);
    lv_label_set_text(subtitle, "Performance-first Linux HMI home page");
    SetBodyLabelStyle(subtitle, lv_color_hex(0x57534E));

    auto* status_line = lv_label_create(screen);
    lv_label_set_text_fmt(status_line,
                          "Status: %s    Operator: %s",
                          login_.online ? "ONLINE" : "OFFLINE",
                          login_.user_label.c_str());
    lv_obj_set_style_text_font(status_line, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(status_line,
                                login_.online ? lv_color_hex(0x0F766E)
                                              : lv_color_hex(0xB91C1C),
                                0);
    lv_obj_set_width(status_line, lv_pct(100));

    auto* actions = lv_obj_create(screen);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_gap(actions, 10, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);

    AddActionButton(actions, "Sync Now", lv_color_hex(0x0F766E), lv_color_hex(0xFFFFFF));
    AddActionButton(actions,
                    "LVGL Widgets Demo",
                    lv_color_hex(0xE0F2FE),
                    lv_color_hex(0x0C4A6E),
                    false,
                    NavigateToLvglWidgetsDemoCb,
                    this);
    AddActionButton(actions,
                    "LVGL Benchmark",
                    lv_color_hex(0xFEF3C7),
                    lv_color_hex(0x92400E),
                    false,
                    NavigateToLvglBenchmarkDemoCb,
                    this);
    AddActionButton(actions,
                    "GE2D Diagnostics",
                    lv_color_hex(0xFFFCF7),
                    lv_color_hex(0x292524),
                    true,
                    NavigateToGe2dDiagnosticsCb,
                    this);

    auto* summary = lv_label_create(screen);
    lv_label_set_text(summary,
                      "Main page keeps only text and primary actions. GE2D rotation, image stress, and layered opacity checks stay on the diagnostics screen so daily scrolling behaves closer to a shipping product.");
    SetBodyLabelStyle(summary, lv_color_hex(0x44403C));

    auto* metrics_title = lv_label_create(screen);
    lv_label_set_text(metrics_title, "Telemetry Snapshot");
    lv_obj_set_style_text_font(metrics_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(metrics_title, lv_color_hex(0x1C1917), 0);

    auto* metrics = lv_label_create(screen);
    const auto metrics_text = BuildSeriesSummary(charts_);
    lv_label_set_text(metrics, metrics_text.c_str());
    SetBodyLabelStyle(metrics, lv_color_hex(0x0F766E));

    auto* checklist_title = lv_label_create(screen);
    lv_label_set_text(checklist_title, "Runtime Checklist");
    lv_obj_set_style_text_font(checklist_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(checklist_title, lv_color_hex(0x1C1917), 0);

    auto* checklist = lv_label_create(screen);
    lv_label_set_text(
        checklist,
        "Direct framebuffer page-flip is active.\n"
        "Touch input stays attached to /dev/input/event0.\n"
        "HTTP, MQTT, and WebSocket services are initialized.\n"
        "Open GE2D diagnostics only when you want hardware counters.\n"
        "Use this page to validate real product interaction latency.");
    SetBodyLabelStyle(checklist, lv_color_hex(0x57534E));

    auto* footer_hint = lv_label_create(screen);
    lv_label_set_text(
        footer_hint,
        "If scrolling still stutters here, the next bottleneck is almost certainly text redraw or page-wide invalidation, not framebuffer present.");
    SetBodyLabelStyle(footer_hint, lv_color_hex(0x7C2D12));

    auto* long_notes = lv_label_create(screen);
    const auto long_notes_text = BuildLongScrollNotes();
    lv_label_set_text(long_notes, long_notes_text.c_str());
    SetBodyLabelStyle(long_notes, lv_color_hex(0x44403C));

    system::Logger::Info("ui.home", "Built home screen with LVGL 9.1.0 widgets");
#else
    built_ = true;
    system::Logger::Warn("ui.home", "LVGL is unavailable; using placeholder home screen");
#endif
}

void HomeScreen::onShow() {
#if HMI_NEXUS_HAS_LVGL
    if (!built_) {
        build();
    }
    if (screen_root_ != nullptr) {
        lv_screen_load(static_cast<lv_obj_t*>(screen_root_));
    }
#endif
    system::Logger::Info("ui.home", "Showing home screen for user: " + login_.user_label);
}

void HomeScreen::onHide() {
    system::Logger::Info("ui.home", "Hiding home screen");
}

}  // namespace hmi_nexus::ui
