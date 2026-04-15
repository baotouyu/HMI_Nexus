#pragma once

#include "lvgl.h"
#include "third_party/lvgl/src/core/lv_global.h"
#include "third_party/lvgl/src/draw/lv_draw.h"
#include "third_party/lvgl/src/draw/lv_draw_buf.h"
#include "third_party/lvgl/src/draw/lv_draw_image.h"
#include "third_party/lvgl/src/draw/lv_draw_label.h"
#include "third_party/lvgl/src/draw/lv_image_decoder.h"
#include "third_party/lvgl/src/draw/sw/blend/lv_draw_sw_blend.h"
#include "third_party/lvgl/src/draw/sw/lv_draw_sw.h"
#include "third_party/lvgl/src/draw/sw/lv_draw_sw_mask.h"
#include "third_party/lvgl/src/misc/lv_area.h"

#if LV_VERSION_CHECK(9, 2, 0)
#define HMI_NEXUS_LVGL_HAS_DRAW_BUF_FLUSH_CACHE_CB 1
#define HMI_NEXUS_LVGL_HAS_IMAGE_CACHE_DRAW_BUF_HANDLERS 1
#define HMI_NEXUS_LVGL_HAS_DRAW_IMAGE_CLIP_RADIUS 1
#else
#define HMI_NEXUS_LVGL_HAS_DRAW_BUF_FLUSH_CACHE_CB 0
#define HMI_NEXUS_LVGL_HAS_IMAGE_CACHE_DRAW_BUF_HANDLERS 0
#define HMI_NEXUS_LVGL_HAS_DRAW_IMAGE_CLIP_RADIUS 0
#endif

#if !LV_VERSION_CHECK(9, 2, 0)
#define lv_area_intersect _lv_area_intersect
#define lv_image_buf_get_transformed_area _lv_image_buf_get_transformed_area
#define lv_draw_image_normal_helper _lv_draw_image_normal_helper
#define lv_draw_image_tiled_helper _lv_draw_image_tiled_helper
#define LV_DRAW_UNIT_IDLE 0
#endif

static inline lv_draw_buf_handlers_t * hmi_nexus_lvgl_image_cache_draw_buf_handlers(void)
{
#if HMI_NEXUS_LVGL_HAS_IMAGE_CACHE_DRAW_BUF_HANDLERS
    return &LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers;
#else
    return lv_draw_buf_get_handlers();
#endif
}

static inline void hmi_nexus_lvgl_install_image_cache_draw_buf_handlers(
    const lv_draw_buf_handlers_t * handlers)
{
    if(handlers == NULL) {
        return;
    }

#if HMI_NEXUS_LVGL_HAS_IMAGE_CACHE_DRAW_BUF_HANDLERS
    LV_GLOBAL_DEFAULT()->image_cache_draw_buf_handlers = *handlers;
#else
    *lv_draw_buf_get_handlers() = *handlers;
#endif
}

static inline lv_draw_buf_t * hmi_nexus_lvgl_draw_buf_create_with_handlers(
    lv_draw_buf_handlers_t * handlers,
    uint32_t w,
    uint32_t h,
    lv_color_format_t color_format,
    uint32_t stride)
{
#if HMI_NEXUS_LVGL_HAS_IMAGE_CACHE_DRAW_BUF_HANDLERS
    return lv_draw_buf_create_ex(handlers, w, h, color_format, stride);
#else
    LV_UNUSED(handlers);
    return lv_draw_buf_create(w, h, color_format, stride);
#endif
}

static inline lv_draw_buf_t * hmi_nexus_lvgl_draw_buf_dup_with_handlers(
    lv_draw_buf_handlers_t * handlers,
    const lv_draw_buf_t * draw_buf)
{
#if HMI_NEXUS_LVGL_HAS_IMAGE_CACHE_DRAW_BUF_HANDLERS
    return lv_draw_buf_dup_ex(handlers, draw_buf);
#else
    LV_UNUSED(handlers);
    return lv_draw_buf_dup(draw_buf);
#endif
}

static inline bool hmi_nexus_lvgl_draw_image_has_clip_radius(const lv_draw_image_dsc_t * draw_dsc)
{
#if HMI_NEXUS_LVGL_HAS_DRAW_IMAGE_CLIP_RADIUS
    return draw_dsc != NULL && draw_dsc->clip_radius != 0;
#else
    LV_UNUSED(draw_dsc);
    return false;
#endif
}
