#include "hmi_nexus/ui/screens/ge2d_diagnostics_screen.h"

#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_LVGL
#include "lvgl.h"
#endif

namespace hmi_nexus::ui {
namespace {

#if HMI_NEXUS_HAS_LVGL

constexpr int kGe2dCanvasWidth = 160;
constexpr int kGe2dCanvasHeight = 108;
constexpr char kHomeScreenId[] = "home";

lv_obj_t* CreateCard(lv_obj_t* parent) {
    auto* card = lv_obj_create(parent);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xD6D3D1), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFCF7), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_gap(card, 8, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    return card;
}

void PopulateGe2dCanvas(lv_obj_t* canvas) {
    auto* draw_buf = lv_draw_buf_create(kGe2dCanvasWidth,
                                        kGe2dCanvasHeight,
                                        LV_COLOR_FORMAT_ARGB8888,
                                        LV_STRIDE_AUTO);
    if (draw_buf == nullptr) {
        system::Logger::Warn("ui.ge2d", "Failed to allocate GE2D diagnostics canvas");
        return;
    }

    lv_canvas_set_draw_buf(canvas, draw_buf);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x0F172A), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.radius = 14;
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.bg_color = lv_color_hex(0x0F766E);
    rect_dsc.border_width = 2;
    rect_dsc.border_opa = LV_OPA_COVER;
    rect_dsc.border_color = lv_color_hex(0x99F6E4);
    lv_area_t rect_area = {8, 8, 151, 70};
    lv_draw_rect(&layer, &rect_dsc, &rect_area);

    lv_draw_rect_dsc_t accent_dsc;
    lv_draw_rect_dsc_init(&accent_dsc);
    accent_dsc.radius = 10;
    accent_dsc.bg_opa = LV_OPA_COVER;
    accent_dsc.bg_color = lv_color_hex(0xF59E0B);
    lv_area_t accent_area = {20, 56, 78, 96};
    lv_draw_rect(&layer, &accent_dsc, &accent_area);

    lv_draw_rect_dsc_t badge_dsc;
    lv_draw_rect_dsc_init(&badge_dsc);
    badge_dsc.radius = 999;
    badge_dsc.bg_opa = LV_OPA_COVER;
    badge_dsc.bg_color = lv_color_hex(0x111827);
    lv_area_t badge_area = {94, 56, 144, 96};
    lv_draw_rect(&layer, &badge_dsc, &badge_area);

    lv_draw_label_dsc_t title_dsc;
    lv_draw_label_dsc_init(&title_dsc);
    title_dsc.color = lv_color_hex(0xF8FAFC);
    title_dsc.font = &lv_font_montserrat_18;
    title_dsc.text = "GE2D";
    lv_area_t title_area = {22, 18, 126, 42};
    lv_draw_label(&layer, &title_dsc, &title_area);

    lv_draw_label_dsc_t subtitle_dsc;
    lv_draw_label_dsc_init(&subtitle_dsc);
    subtitle_dsc.color = lv_color_hex(0xCCFBF1);
    subtitle_dsc.text = "Image blit source";
    lv_area_t subtitle_area = {22, 40, 140, 64};
    lv_draw_label(&layer, &subtitle_dsc, &subtitle_area);

    lv_draw_label_dsc_t badge_text_dsc;
    lv_draw_label_dsc_init(&badge_text_dsc);
    badge_text_dsc.color = lv_color_hex(0xFFFFFF);
    badge_text_dsc.text = "90";
    lv_area_t badge_text_area = {109, 66, 128, 86};
    lv_draw_label(&layer, &badge_text_dsc, &badge_text_area);

    lv_canvas_finish_layer(canvas, &layer);
}

void Ge2dTimerCb(lv_timer_t* timer) {
    auto* screen = static_cast<Ge2dDiagnosticsScreen*>(lv_timer_get_user_data(timer));
    if (screen == nullptr) {
        return;
    }

    screen->updateGe2dTestFrame();
}

void Ge2dScrollEventCb(lv_event_t* event) {
    auto* screen = static_cast<Ge2dDiagnosticsScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    switch (lv_event_get_code(event)) {
    case LV_EVENT_SCROLL_BEGIN:
        screen->pauseAnimation();
        break;
    case LV_EVENT_SCROLL_END:
        screen->resumeAnimation();
        break;
    default:
        break;
    }
}

void NavigateHomeCb(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    auto* screen = static_cast<Ge2dDiagnosticsScreen*>(lv_event_get_user_data(event));
    if (screen == nullptr) {
        return;
    }

    const auto result = screen->navigateTo(kHomeScreenId);
    if (!result) {
        system::Logger::Warn("ui.ge2d", "Failed to return to home screen: " + result.message());
    }
}

lv_obj_t* CreateButton(lv_obj_t* parent,
                       const char* title,
                       lv_color_t background_color,
                       lv_color_t text_color,
                       lv_event_cb_t event_cb,
                       void* user_data) {
    auto* button = lv_button_create(parent);
    lv_obj_set_height(button, 46);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, background_color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(button, 18, 0);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, user_data);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_obj_center(label);
    return button;
}

#endif

}  // namespace

std::string Ge2dDiagnosticsScreen::id() const {
    return "ge2d_diagnostics";
}

void Ge2dDiagnosticsScreen::build() {
#if HMI_NEXUS_HAS_LVGL
    if (built_) {
        return;
    }

    auto* screen = lv_obj_create(nullptr);
    screen_root_ = screen;
    built_ = true;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEEF6FF), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 20, 0);
    lv_obj_set_style_pad_bottom(screen, 36, 0);
    lv_obj_set_style_pad_gap(screen, 16, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_event_cb(screen, Ge2dScrollEventCb, LV_EVENT_SCROLL_BEGIN, this);
    lv_obj_add_event_cb(screen, Ge2dScrollEventCb, LV_EVENT_SCROLL_END, this);

    auto* header = lv_obj_create(screen);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_gap(header, 6, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);

    auto* title = lv_label_create(header);
    lv_label_set_text(title, "GE2D Diagnostics");
    lv_obj_set_style_text_color(title, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    auto* subtitle = lv_label_create(header);
    lv_label_set_text(subtitle,
                      "Dedicated runtime page for watching image, layer, and draw-buffer counters without impacting the home page.");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x475569), 0);

    auto* actions = lv_obj_create(screen);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_gap(actions, 12, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);

    CreateButton(actions,
                 "Back Home",
                 lv_color_hex(0x0F766E),
                 lv_color_hex(0xFFFFFF),
                 NavigateHomeCb,
                 this);

    auto* status_card = CreateCard(screen);

    auto* status_title = lv_label_create(status_card);
    lv_label_set_text(status_title, "What To Watch");
    lv_obj_set_style_text_font(status_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(status_title, lv_color_hex(0x0F172A), 0);

    auto* status_body = lv_label_create(status_card);
    lv_label_set_text(status_body,
                      "Check `ui.lvgl.ge2d` logs while this page animates. `image(a/h/sw/r)` and `layer(a/h/sw/r)` should keep hardware counts rising, software fallback near zero, and `hw_fail` at zero.");
    lv_obj_set_style_text_color(status_body, lv_color_hex(0x475569), 0);

    auto* ge2d_card = CreateCard(screen);

    auto* ge2d_title = lv_label_create(ge2d_card);
    lv_label_set_text(ge2d_title, "Image / Layer Stress Block");
    lv_obj_set_style_text_font(ge2d_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ge2d_title, lv_color_hex(0x1C1917), 0);

    auto* ge2d_desc = lv_label_create(ge2d_card);
    lv_label_set_text(ge2d_desc,
                      "This page intentionally rotates the image source and changes layered opacity so D211 GE2D runtime counters keep moving while you inspect hardware acceleration behavior.");
    lv_obj_set_style_text_color(ge2d_desc, lv_color_hex(0x57534E), 0);

    auto* ge2d_media_row = lv_obj_create(ge2d_card);
    lv_obj_remove_flag(ge2d_media_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(ge2d_media_row, lv_pct(100));
    lv_obj_set_height(ge2d_media_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ge2d_media_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ge2d_media_row, 0, 0);
    lv_obj_set_style_pad_all(ge2d_media_row, 0, 0);
    lv_obj_set_style_pad_gap(ge2d_media_row, 14, 0);
    lv_obj_set_flex_flow(ge2d_media_row, LV_FLEX_FLOW_ROW_WRAP);

    auto* ge2d_canvas = lv_canvas_create(ge2d_media_row);
    lv_obj_remove_flag(ge2d_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ge2d_canvas, kGe2dCanvasWidth, kGe2dCanvasHeight);
    lv_obj_set_style_radius(ge2d_canvas, 12, 0);
    lv_obj_set_style_clip_corner(ge2d_canvas, true, 0);
    PopulateGe2dCanvas(ge2d_canvas);

    auto* ge2d_image = lv_image_create(ge2d_media_row);
    lv_image_set_src(ge2d_image, lv_canvas_get_draw_buf(ge2d_canvas));
    lv_image_set_pivot(ge2d_image, kGe2dCanvasWidth / 2, kGe2dCanvasHeight / 2);
    lv_image_set_rotation(ge2d_image, 0);
    lv_obj_set_style_bg_opa(ge2d_image, LV_OPA_TRANSP, 0);
    image_obj_ = ge2d_image;

    auto* layer_panel = lv_obj_create(ge2d_card);
    lv_obj_remove_flag(layer_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(layer_panel, lv_pct(100));
    lv_obj_set_height(layer_panel, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(layer_panel, 0, 0);
    lv_obj_set_style_border_width(layer_panel, 1, 0);
    lv_obj_set_style_border_color(layer_panel, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_bg_color(layer_panel, lv_color_hex(0xE0F2FE), 0);
    lv_obj_set_style_bg_opa(layer_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(layer_panel, 14, 0);
    lv_obj_set_style_pad_gap(layer_panel, 8, 0);
    lv_obj_set_style_opa_layered(layer_panel, LV_OPA_70, 0);
    lv_obj_set_flex_flow(layer_panel, LV_FLEX_FLOW_COLUMN);
    layer_panel_ = layer_panel;

    auto* layer_title = lv_label_create(layer_panel);
    lv_label_set_text(layer_title, "Layer blend test");
    lv_obj_set_style_text_font(layer_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(layer_title, lv_color_hex(0x0F172A), 0);

    auto* layer_desc = lv_label_create(layer_panel);
    lv_label_set_text(layer_desc,
                      "Layer opacity stays below 255 and changes on every test tick to force LV_DRAW_TASK_TYPE_LAYER composition.");
    lv_obj_set_style_text_color(layer_desc, lv_color_hex(0x334155), 0);

    auto* layer_status = lv_label_create(layer_panel);
    lv_label_set_text(layer_status, "image rot=0 deg | layer opa=178");
    lv_obj_set_style_text_color(layer_status, lv_color_hex(0x0F766E), 0);
    status_label_ = layer_status;

    phase_ = 0;
    if (timer_ == nullptr) {
        timer_ = lv_timer_create(Ge2dTimerCb, 900, this);
    }

    system::Logger::Info("ui.ge2d", "Built GE2D diagnostics screen");
#else
    built_ = true;
    system::Logger::Warn("ui.ge2d", "LVGL is unavailable; diagnostics page disabled");
#endif
}

void Ge2dDiagnosticsScreen::onShow() {
#if HMI_NEXUS_HAS_LVGL
    if (!built_) {
        build();
    }
    if (screen_root_ != nullptr) {
        resumeAnimation();
        lv_screen_load(static_cast<lv_obj_t*>(screen_root_));
    }
#endif
    system::Logger::Info("ui.ge2d", "Showing GE2D diagnostics screen");
}

void Ge2dDiagnosticsScreen::onHide() {
#if HMI_NEXUS_HAS_LVGL
    pauseAnimation();
#endif
    system::Logger::Info("ui.ge2d", "Hiding GE2D diagnostics screen");
}

void Ge2dDiagnosticsScreen::updateGe2dTestFrame() {
#if HMI_NEXUS_HAS_LVGL
    auto* image = static_cast<lv_obj_t*>(image_obj_);
    auto* layer_panel = static_cast<lv_obj_t*>(layer_panel_);
    auto* status_label = static_cast<lv_obj_t*>(status_label_);
    if (image == nullptr || layer_panel == nullptr || status_label == nullptr) {
        return;
    }

    static constexpr int32_t kRotations[] = {0, 900, 1800, 2700};
    static constexpr lv_opa_t kLayerOpacities[] = {
        LV_OPA_70, LV_OPA_50, LV_OPA_80, LV_OPA_60};

    phase_ = (phase_ + 1) % 4;
    const int32_t rotation = kRotations[phase_];
    const lv_opa_t opacity = kLayerOpacities[phase_];

    lv_image_set_rotation(image, rotation);
    lv_obj_set_style_opa_layered(layer_panel, opacity, 0);
    lv_label_set_text_fmt(status_label,
                          "image rot=%d deg | layer opa=%d",
                          static_cast<int>(rotation / 10),
                          static_cast<int>(opacity));
#endif
}

void Ge2dDiagnosticsScreen::pauseAnimation() {
#if HMI_NEXUS_HAS_LVGL
    auto* timer = static_cast<lv_timer_t*>(timer_);
    if (timer != nullptr) {
        lv_timer_pause(timer);
    }
#endif
}

void Ge2dDiagnosticsScreen::resumeAnimation() {
#if HMI_NEXUS_HAS_LVGL
    auto* timer = static_cast<lv_timer_t*>(timer_);
    if (timer != nullptr) {
        lv_timer_resume(timer);
        lv_timer_reset(timer);
    }
#endif
}

}  // namespace hmi_nexus::ui
