#include "source/src/ui/lvgl/d211/d211_lvgl_ge2d.h"

#if HMI_NEXUS_HAS_D211_GE2D && HMI_NEXUS_HAS_LVGL

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <dma_allocator.h>
#include <mpp_ge.h>

#include "source/src/ui/lvgl/d211/d211_lvgl_fake_image.h"
#include "source/src/ui/lvgl/lvgl_private_compat.h"

#define HMI_NEXUS_D211_DRAW_UNIT_ID 10
#define HMI_NEXUS_D211_GE2D_IMAGE_BLIT_SIZE_LIMIT (100 * 100)
#define HMI_NEXUS_D211_GE2D_IMAGE_ROTATE_SIZE_LIMIT (20 * 20)
#define HMI_NEXUS_D211_GE2D_IMAGE_BLEND_SIZE_LIMIT (100 * 100)
#define HMI_NEXUS_D211_GE2D_IMAGE_CACHE_LIMIT 16U

typedef struct hmi_nexus_d211_ge2d_buffer_entry_t {
    void * data;
    size_t size;
    int dma_fd;
    uintptr_t physical_address;
    hmi_nexus_d211_lvgl_buffer_memory_type_t memory_type;
    bool owned;
    struct hmi_nexus_d211_ge2d_buffer_entry_t * next;
} hmi_nexus_d211_ge2d_buffer_entry_t;

typedef struct hmi_nexus_d211_ge2d_unit_t {
    lv_draw_unit_t base_unit;
    lv_draw_task_t * task_act;
} hmi_nexus_d211_ge2d_unit_t;

typedef struct hmi_nexus_d211_ge2d_image_cache_entry_t {
    const void * source_key;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    lv_color_format_t color_format;
    lv_draw_buf_t * dma_draw_buf;
    struct hmi_nexus_d211_ge2d_image_cache_entry_t * next;
} hmi_nexus_d211_ge2d_image_cache_entry_t;

typedef struct hmi_nexus_d211_ge2d_state_t {
    bool initialized;
    bool handlers_installed;
    struct mpp_ge * ge;
    int dma_heap_fd;
    lv_draw_buf_handlers_t original_handlers;
    lv_draw_buf_handlers_t image_cache_handlers;
    hmi_nexus_d211_ge2d_buffer_entry_t * buffers;
    hmi_nexus_d211_ge2d_image_cache_entry_t * image_cache;
    hmi_nexus_d211_lvgl_ge2d_stats_t stats;
} hmi_nexus_d211_ge2d_state_t;

static hmi_nexus_d211_ge2d_state_t g_state;

typedef struct hmi_nexus_d211_ge2d_inverse_transform_t {
    int32_t sinma;
    int32_t cosma;
    int32_t scale_x;
    int32_t scale_y;
    int32_t angle;
    int32_t pivot_x_256;
    int32_t pivot_y_256;
    lv_point_t pivot;
} hmi_nexus_d211_ge2d_inverse_transform_t;

typedef struct hmi_nexus_d211_ge2d_border_geometry_t {
    lv_area_t outer_area;
    lv_area_t inner_area;
    lv_area_t core_area;
    int32_t outer_radius;
    int32_t inner_radius;
    bool top_side;
    bool bottom_side;
    bool left_side;
    bool right_side;
    bool rounded;
} hmi_nexus_d211_ge2d_border_geometry_t;

static void image_draw_core(lv_draw_unit_t * draw_unit,
                            const lv_draw_image_dsc_t * draw_dsc,
                            const lv_image_decoder_dsc_t * decoder_dsc,
                            lv_draw_image_sup_t * sup,
                            const lv_area_t * img_coords,
                            const lv_area_t * clipped_img_area);
static bool destination_layer_supported(const lv_draw_dsc_base_t * base_dsc);
static bool bridgeable_source_color_format(lv_color_format_t color_format);
static bool execute_fake_image_fill(lv_draw_unit_t * draw_unit,
                                    const lv_draw_image_dsc_t * draw_dsc,
                                    const lv_area_t * coords);
static bool execute_border(lv_draw_unit_t * draw_unit,
                           const lv_draw_border_dsc_t * draw_dsc,
                           const lv_area_t * coords);
static void label_draw_letter_cb(lv_draw_unit_t * draw_unit,
                                 lv_draw_glyph_dsc_t * glyph_draw_dsc,
                                 lv_draw_fill_dsc_t * fill_draw_dsc,
                                 const lv_area_t * fill_area);

static enum mpp_pixel_format to_mpp_pixel_format(lv_color_format_t color_format)
{
    switch(color_format) {
        case LV_COLOR_FORMAT_RGB565:
            return MPP_FMT_RGB_565;
        case LV_COLOR_FORMAT_RGB888:
            return MPP_FMT_RGB_888;
        case LV_COLOR_FORMAT_ARGB8888:
            return MPP_FMT_ARGB_8888;
        case LV_COLOR_FORMAT_XRGB8888:
            return MPP_FMT_XRGB_8888;
        case LV_COLOR_FORMAT_I420:
            return MPP_FMT_YUV420P;
        case LV_COLOR_FORMAT_I422:
            return MPP_FMT_YUV422P;
        case LV_COLOR_FORMAT_I444:
            return MPP_FMT_YUV444P;
        case LV_COLOR_FORMAT_I400:
            return MPP_FMT_YUV400;
        default:
            break;
    }

    return MPP_FMT_MAX;
}

static bool is_draw_buf_supported(lv_color_format_t color_format)
{
    return to_mpp_pixel_format(color_format) != MPP_FMT_MAX;
}

static bool bridgeable_source_color_format(lv_color_format_t color_format)
{
    return is_draw_buf_supported(color_format) || color_format == LV_COLOR_FORMAT_RGB565A8;
}

static hmi_nexus_d211_ge2d_buffer_entry_t * find_buffer_entry(const void * data)
{
    hmi_nexus_d211_ge2d_buffer_entry_t * entry = g_state.buffers;
    while(entry != NULL) {
        if(entry->data == data) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

static void upsert_buffer_entry(const hmi_nexus_d211_lvgl_buffer_desc_t * desc, bool owned)
{
    hmi_nexus_d211_ge2d_buffer_entry_t * entry;

    if(desc == NULL || desc->data == NULL) {
        return;
    }

    entry = find_buffer_entry(desc->data);
    if(entry == NULL) {
        entry = malloc(sizeof(*entry));
        if(entry == NULL) {
            return;
        }
        memset(entry, 0, sizeof(*entry));
        entry->next = g_state.buffers;
        g_state.buffers = entry;
    }

    entry->data = desc->data;
    entry->size = desc->size;
    entry->dma_fd = desc->dma_fd;
    entry->physical_address = desc->physical_address;
    entry->memory_type = desc->memory_type;
    entry->owned = owned;
}

static void release_owned_buffer(hmi_nexus_d211_ge2d_buffer_entry_t * entry)
{
    if(entry == NULL || !entry->owned) {
        return;
    }

    if(entry->memory_type == HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF &&
       entry->data != NULL &&
       entry->size != 0) {
        dmabuf_munmap((unsigned char *)entry->data, (int)entry->size);
    }
    if(entry->memory_type == HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF &&
       entry->dma_fd >= 0) {
        dmabuf_free(entry->dma_fd);
    }
}

static void remove_buffer_entry(void * data)
{
    hmi_nexus_d211_ge2d_buffer_entry_t ** prev = &g_state.buffers;

    while(*prev != NULL) {
        if((*prev)->data == data) {
            hmi_nexus_d211_ge2d_buffer_entry_t * entry = *prev;
            *prev = entry->next;
            release_owned_buffer(entry);
            free(entry);
            return;
        }
        prev = &(*prev)->next;
    }
}

static void * draw_buf_align(void * data, lv_color_format_t color_format)
{
    if(find_buffer_entry(data) != NULL) {
        return data;
    }

    if(g_state.original_handlers.align_pointer_cb != NULL) {
        return g_state.original_handlers.align_pointer_cb(data, color_format);
    }

    return data;
}

static void * draw_buf_malloc(size_t size, lv_color_format_t color_format)
{
    if(g_state.dma_heap_fd >= 0 && is_draw_buf_supported(color_format)) {
        int dma_fd = dmabuf_alloc(g_state.dma_heap_fd, (int)size);
        if(dma_fd >= 0) {
            unsigned char * mapped = dmabuf_mmap(dma_fd, (int)size);
            if(mapped != NULL) {
                hmi_nexus_d211_lvgl_buffer_desc_t desc;
                memset(&desc, 0, sizeof(desc));
                desc.data = mapped;
                desc.size = size;
                desc.dma_fd = dma_fd;
                desc.memory_type = HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF;
                upsert_buffer_entry(&desc, true);
                g_state.stats.draw_buf_dma_alloc_count++;
                return mapped;
            }

            dmabuf_free(dma_fd);
        }
    }

    if(g_state.original_handlers.buf_malloc_cb != NULL) {
        void * host_buf = g_state.original_handlers.buf_malloc_cb(size, color_format);
        if(host_buf != NULL) {
            g_state.stats.draw_buf_host_alloc_count++;
        }
        return host_buf;
    }

    return NULL;
}

static void draw_buf_free(void * buf)
{
    hmi_nexus_d211_ge2d_buffer_entry_t * entry = find_buffer_entry(buf);
    if(entry != NULL && entry->owned) {
        remove_buffer_entry(buf);
        return;
    }

    if(g_state.original_handlers.buf_free_cb != NULL) {
        g_state.original_handlers.buf_free_cb(buf);
    }
}

static void draw_buf_invalidate_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    LV_UNUSED(draw_buf);
    if(g_state.original_handlers.invalidate_cache_cb != NULL) {
        g_state.original_handlers.invalidate_cache_cb(draw_buf, area);
    }
}

#if HMI_NEXUS_LVGL_HAS_DRAW_BUF_FLUSH_CACHE_CB
static void draw_buf_flush_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    hmi_nexus_d211_ge2d_buffer_entry_t * entry;

    LV_UNUSED(area);

    if(draw_buf == NULL) {
        return;
    }

    entry = find_buffer_entry(draw_buf->data);
    if(entry != NULL &&
       entry->memory_type == HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF &&
       entry->dma_fd >= 0) {
        dmabuf_sync(entry->dma_fd, CACHE_CLEAN);
        return;
    }

    if(g_state.original_handlers.flush_cache_cb != NULL) {
        g_state.original_handlers.flush_cache_cb(draw_buf, area);
    }
}
#endif

static uint32_t draw_buf_width_to_stride(uint32_t width, lv_color_format_t color_format)
{
    if(g_state.original_handlers.width_to_stride_cb != NULL) {
        return g_state.original_handlers.width_to_stride_cb(width, color_format);
    }

    return 0;
}

static int32_t normalize_rotation(int32_t rotation)
{
    while(rotation < 0) {
        rotation += 3600;
    }
    while(rotation >= 3600) {
        rotation -= 3600;
    }

    return rotation;
}

static void compute_rotation_q12(int32_t rotation, int32_t * sin_q12, int32_t * cos_q12)
{
    int32_t normalized;
    int32_t angle_low;
    int32_t angle_high;
    int32_t angle_remainder;
    int32_t sin_low;
    int32_t sin_high;
    int32_t cos_low;
    int32_t cos_high;

    if(sin_q12 == NULL || cos_q12 == NULL) {
        return;
    }

    normalized = normalize_rotation(rotation);
    angle_low = normalized / 10;
    angle_high = (angle_low + 1) % 360;
    angle_remainder = normalized - (angle_low * 10);

    sin_low = lv_trigo_sin((int16_t)angle_low);
    sin_high = lv_trigo_sin((int16_t)angle_high);
    cos_low = lv_trigo_cos((int16_t)angle_low);
    cos_high = lv_trigo_cos((int16_t)angle_high);

    *sin_q12 = (sin_low * (10 - angle_remainder) + sin_high * angle_remainder) / 10;
    *cos_q12 = (cos_low * (10 - angle_remainder) + cos_high * angle_remainder) / 10;
    *sin_q12 = *sin_q12 >> (LV_TRIGO_SHIFT - 12);
    *cos_q12 = *cos_q12 >> (LV_TRIGO_SHIFT - 12);
}

static void inverse_transform_point(const hmi_nexus_d211_ge2d_inverse_transform_t * transform,
                                    int32_t x_in,
                                    int32_t y_in,
                                    int32_t * x_out,
                                    int32_t * y_out,
                                    int32_t source_width,
                                    int32_t source_height)
{
    if(transform == NULL || x_out == NULL || y_out == NULL) {
        return;
    }

    if(transform->angle == 0 &&
       transform->scale_x == LV_SCALE_NONE &&
       transform->scale_y == LV_SCALE_NONE) {
        *x_out = x_in * 256;
        *y_out = y_in * 256;
    }
    else {
        x_in -= transform->pivot.x;
        y_in -= transform->pivot.y;

        if(transform->angle == 0) {
            *x_out = ((int32_t)(x_in * 256 * 256 / transform->scale_x)) +
                     transform->pivot_x_256;
            *y_out = ((int32_t)(y_in * 256 * 256 / transform->scale_y)) +
                     transform->pivot_y_256;
        }
        else if(transform->scale_x == LV_SCALE_NONE &&
                transform->scale_y == LV_SCALE_NONE) {
            *x_out = ((transform->cosma * x_in - transform->sinma * y_in) >> 2) +
                     transform->pivot_x_256;
            *y_out = ((transform->sinma * x_in + transform->cosma * y_in) >> 2) +
                     transform->pivot_y_256;
        }
        else {
            *x_out = (((transform->cosma * x_in - transform->sinma * y_in) * 256 /
                       transform->scale_x) >> 2) + transform->pivot_x_256;
            *y_out = (((transform->sinma * x_in + transform->cosma * y_in) * 256 /
                       transform->scale_y) >> 2) + transform->pivot_y_256;
        }
    }

    *x_out = LV_CLAMP(0, (*x_out + 128) >> 8, source_width);
    *y_out = LV_CLAMP(0, (*y_out + 128) >> 8, source_height);
}

static void compute_inverse_transform_area(const lv_area_t * input_area,
                                           lv_area_t * output_area,
                                           int32_t rotation,
                                           uint16_t scale_x,
                                           uint16_t scale_y,
                                           const lv_point_t * pivot,
                                           int32_t source_width,
                                           int32_t source_height)
{
    hmi_nexus_d211_ge2d_inverse_transform_t transform;
    lv_point_t transformed_points[4];
    int32_t angle_low;
    int32_t angle_high;
    int32_t angle_remainder;
    int32_t sinma_low;
    int32_t sinma_high;
    int32_t cosma_low;
    int32_t cosma_high;

    memset(&transform, 0, sizeof(transform));
    transform.angle = -rotation;
    transform.scale_x = scale_x;
    transform.scale_y = scale_y;
    transform.pivot = *pivot;
    transform.pivot_x_256 = transform.pivot.x * 256;
    transform.pivot_y_256 = transform.pivot.y * 256;

    angle_low = transform.angle / 10;
    angle_high = angle_low + 1;
    angle_remainder = transform.angle - (angle_low * 10);
    sinma_low = lv_trigo_sin((int16_t)angle_low);
    sinma_high = lv_trigo_sin((int16_t)angle_high);
    cosma_low = lv_trigo_sin((int16_t)(angle_low + 90));
    cosma_high = lv_trigo_sin((int16_t)(angle_high + 90));

    transform.sinma = (sinma_low * (10 - angle_remainder) +
                       sinma_high * angle_remainder) / 10;
    transform.cosma = (cosma_low * (10 - angle_remainder) +
                       cosma_high * angle_remainder) / 10;
    transform.sinma = transform.sinma >> (LV_TRIGO_SHIFT - 10);
    transform.cosma = transform.cosma >> (LV_TRIGO_SHIFT - 10);

    inverse_transform_point(&transform,
                            input_area->x1,
                            input_area->y1,
                            &transformed_points[0].x,
                            &transformed_points[0].y,
                            source_width,
                            source_height);
    inverse_transform_point(&transform,
                            input_area->x2 + 1,
                            input_area->y1,
                            &transformed_points[1].x,
                            &transformed_points[1].y,
                            source_width,
                            source_height);
    inverse_transform_point(&transform,
                            input_area->x1,
                            input_area->y2 + 1,
                            &transformed_points[2].x,
                            &transformed_points[2].y,
                            source_width,
                            source_height);
    inverse_transform_point(&transform,
                            input_area->x2 + 1,
                            input_area->y2 + 1,
                            &transformed_points[3].x,
                            &transformed_points[3].y,
                            source_width,
                            source_height);

    output_area->x1 = LV_MIN4(transformed_points[0].x,
                              transformed_points[1].x,
                              transformed_points[2].x,
                              transformed_points[3].x);
    output_area->x2 = LV_MAX4(transformed_points[0].x,
                              transformed_points[1].x,
                              transformed_points[2].x,
                              transformed_points[3].x) - 1;
    output_area->y1 = LV_MIN4(transformed_points[0].y,
                              transformed_points[1].y,
                              transformed_points[2].y,
                              transformed_points[3].y);
    output_area->y2 = LV_MAX4(transformed_points[0].y,
                              transformed_points[1].y,
                              transformed_points[2].y,
                              transformed_points[3].y) - 1;
}

static bool is_right_angle_rotation(int32_t rotation)
{
    rotation = normalize_rotation(rotation);
    return rotation == 0 || rotation == 900 || rotation == 1800 || rotation == 2700;
}

static void sync_for_hw(hmi_nexus_d211_ge2d_buffer_entry_t * entry)
{
    if(entry == NULL ||
       entry->memory_type != HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF ||
       entry->dma_fd < 0) {
        return;
    }

    dmabuf_sync(entry->dma_fd, CACHE_CLEAN);
}

static void sync_for_cpu(hmi_nexus_d211_ge2d_buffer_entry_t * entry)
{
    if(entry == NULL ||
       entry->memory_type != HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF ||
       entry->dma_fd < 0) {
        return;
    }

    dmabuf_sync(entry->dma_fd, CACHE_INVALID);
}

static bool fill_task_supported(lv_draw_task_t * task)
{
    const lv_draw_fill_dsc_t * draw_dsc;
    const lv_draw_dsc_base_t * base_dsc;
    lv_draw_buf_t * dest_buf;

    if(task == NULL || g_state.ge == NULL) {
        return false;
    }

    base_dsc = (const lv_draw_dsc_base_t *)task->draw_dsc;
    if(base_dsc == NULL || base_dsc->layer == NULL || base_dsc->layer->draw_buf == NULL) {
        return false;
    }

    if(!is_draw_buf_supported(base_dsc->layer->color_format)) {
        return false;
    }

    dest_buf = base_dsc->layer->draw_buf;
    if(find_buffer_entry(dest_buf->data) == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_fill_dsc_t *)task->draw_dsc;
    if(draw_dsc->opa <= LV_OPA_MIN) {
        return false;
    }
    /* Follow the vendor LVGL v9 GE2D port: only plain rectangular fills
     * are submitted through fillrect. Rounded corners and gradients stay
     * on LVGL's software path. */
    if(draw_dsc->radius != 0) {
        return false;
    }
    if(draw_dsc->grad.dir != LV_GRAD_DIR_NONE) {
        return false;
    }

    return true;
}

static bool border_task_supported(lv_draw_task_t * task)
{
    LV_UNUSED(task);
    /* Keep border rendering on the stock LVGL path for now.
     * The vendor SDK GE2D port does not offload border tasks and that
     * avoids the out-of-range fillrect decomposition that triggered
     * IOC_GE_FILLRECT failures on D211. */
    return false;
}

static bool box_shadow_task_supported(lv_draw_task_t * task)
{
    LV_UNUSED(task);
    /* Same as border: stay aligned with the vendor SDK task matrix. */
    return false;
}

static bool arc_task_supported(lv_draw_task_t * task)
{
    const lv_draw_arc_dsc_t * draw_dsc;

    if(task == NULL || g_state.ge == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_arc_dsc_t *)task->draw_dsc;
    if(draw_dsc == NULL ||
       draw_dsc->opa <= LV_OPA_MIN ||
       draw_dsc->width <= 0 ||
       draw_dsc->radius == 0 ||
       draw_dsc->start_angle == draw_dsc->end_angle) {
        return false;
    }

    if(!destination_layer_supported(&draw_dsc->base)) {
        return false;
    }

    if(draw_dsc->img_src != NULL) {
        return false;
    }

    /* A full ring can be redirected to the already optimized border path. */
    if(draw_dsc->start_angle + 360 == draw_dsc->end_angle ||
       draw_dsc->start_angle == draw_dsc->end_angle + 360) {
        return true;
    }

    /* Keep the first arc fast path intentionally narrow to avoid visual regressions. */
    return !draw_dsc->rounded;
}

static bool label_task_supported(lv_draw_task_t * task)
{
    const lv_draw_label_dsc_t * draw_dsc;
    const lv_layer_t * layer;

    if(task == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_label_dsc_t *)task->draw_dsc;
    if(draw_dsc == NULL ||
       draw_dsc->opa <= LV_OPA_MIN ||
       draw_dsc->text == NULL ||
       draw_dsc->font == NULL ||
       draw_dsc->blend_mode != LV_BLEND_MODE_NORMAL) {
        return false;
    }

    layer = draw_dsc->base.layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    return layer->color_format == LV_COLOR_FORMAT_RGB565 ||
           layer->color_format == LV_COLOR_FORMAT_RGB888 ||
           layer->color_format == LV_COLOR_FORMAT_ARGB8888 ||
           layer->color_format == LV_COLOR_FORMAT_XRGB8888;
}

static uint32_t image_pixel_count(const lv_draw_image_dsc_t * draw_dsc)
{
    if(draw_dsc == NULL) {
        return 0;
    }

    return (uint32_t)draw_dsc->header.w * (uint32_t)draw_dsc->header.h;
}

static bool is_fake_image_source(const lv_draw_image_dsc_t * draw_dsc)
{
    if(draw_dsc == NULL || draw_dsc->src == NULL ||
       lv_image_src_get_type(draw_dsc->src) != LV_IMAGE_SRC_FILE) {
        return false;
    }

    return draw_dsc->header.cf == LV_COLOR_FORMAT_RAW &&
           hmi_nexus_d211_fake_image_path_looks_like((const char *)draw_dsc->src);
}

static bool image_descriptor_base_supported(const lv_draw_image_dsc_t * draw_dsc)
{
    const bool scaled = draw_dsc != NULL &&
                        (draw_dsc->scale_x != LV_SCALE_NONE || draw_dsc->scale_y != LV_SCALE_NONE);

    if(draw_dsc == NULL) {
        return false;
    }

    if(draw_dsc->skew_x != 0 || draw_dsc->skew_y != 0) {
        return false;
    }
    if(draw_dsc->bitmap_mask_src != NULL) {
        return false;
    }
    if(draw_dsc->recolor_opa > LV_OPA_MIN) {
        return false;
    }
    if(hmi_nexus_lvgl_draw_image_has_clip_radius(draw_dsc)) {
        return false;
    }
    if(draw_dsc->blend_mode != LV_BLEND_MODE_NORMAL) {
        return false;
    }
    if(scaled && !is_right_angle_rotation(draw_dsc->rotation)) {
        return false;
    }

    return true;
}

static bool image_descriptor_supported(const lv_draw_image_dsc_t * draw_dsc)
{
    lv_display_t * display;
    uint32_t pixels;

    if(!image_descriptor_base_supported(draw_dsc)) {
        return false;
    }

    display = lv_display_get_default();
    if(display != NULL &&
       lv_display_get_rotation(display) != LV_DISPLAY_ROTATION_0 &&
       !is_right_angle_rotation(draw_dsc->rotation)) {
        /* The rotated framebuffer path is stable with right-angle image transforms,
         * but arbitrary-angle GE2D image rotation can still trigger vendor driver kills. */
        return false;
    }

    if(draw_dsc->header.cf >= LV_COLOR_FORMAT_YUV_START &&
       !is_right_angle_rotation(draw_dsc->rotation)) {
        return false;
    }
    if(draw_dsc->header.cf < LV_COLOR_FORMAT_YUV_START &&
       (draw_dsc->header.stride % 8U) != 0U) {
        return false;
    }

    pixels = image_pixel_count(draw_dsc);
    if(draw_dsc->rotation != 0) {
        if(pixels < HMI_NEXUS_D211_GE2D_IMAGE_ROTATE_SIZE_LIMIT) {
            return false;
        }
    }
    else if(pixels < HMI_NEXUS_D211_GE2D_IMAGE_BLIT_SIZE_LIMIT) {
        return false;
    }

    if(pixels < HMI_NEXUS_D211_GE2D_IMAGE_BLEND_SIZE_LIMIT && !is_fake_image_source(draw_dsc)) {
        return false;
    }

    return true;
}

static bool layer_descriptor_supported(const lv_draw_image_dsc_t * draw_dsc,
                                       const lv_layer_t * source_layer)
{
    lv_display_t * display;
    const lv_draw_buf_t * source_draw_buf;

    if(!image_descriptor_base_supported(draw_dsc) || source_layer == NULL) {
        return false;
    }

    display = lv_display_get_default();
    if(display != NULL &&
       lv_display_get_rotation(display) != LV_DISPLAY_ROTATION_0 &&
       !is_right_angle_rotation(draw_dsc->rotation)) {
        return false;
    }

    source_draw_buf = source_layer->draw_buf;
    if(source_draw_buf == NULL) {
        return false;
    }

    if(source_draw_buf->header.cf >= LV_COLOR_FORMAT_YUV_START &&
       !is_right_angle_rotation(draw_dsc->rotation)) {
        return false;
    }
    if(source_draw_buf->header.cf < LV_COLOR_FORMAT_YUV_START &&
       (source_draw_buf->header.stride % 8U) != 0U) {
        return false;
    }

    return true;
}

static bool destination_layer_supported(const lv_draw_dsc_base_t * base_dsc)
{
    if(base_dsc == NULL || base_dsc->layer == NULL || base_dsc->layer->draw_buf == NULL) {
        return false;
    }

    if(!is_draw_buf_supported(base_dsc->layer->color_format)) {
        return false;
    }

    if(find_buffer_entry(base_dsc->layer->draw_buf->data) == NULL) {
        return false;
    }

    return true;
}

static bool source_draw_buf_supported(const lv_draw_buf_t * draw_buf)
{
    const struct mpp_buf * mpp_buf;

    if(draw_buf == NULL || draw_buf->data == NULL) {
        return false;
    }

    if(!is_draw_buf_supported(draw_buf->header.cf)) {
        return false;
    }

    if(find_buffer_entry(draw_buf->data) != NULL) {
        return true;
    }

    if(draw_buf->header.cf < LV_COLOR_FORMAT_YUV_START ||
       (draw_buf->header.flags & LV_IMAGE_FLAGS_USER8) == 0U) {
        return false;
    }

    mpp_buf = (const struct mpp_buf *)draw_buf->data;
    if(mpp_buf->buf_type == MPP_DMA_BUF_FD) {
        return mpp_buf->fd[0] >= 0;
    }
    if(mpp_buf->buf_type == MPP_PHY_ADDR) {
        return mpp_buf->phy_addr[0] != 0U;
    }

    return false;
}

static hmi_nexus_d211_ge2d_image_cache_entry_t * find_image_cache_entry(const void * source_key,
                                                                        const lv_draw_buf_t * draw_buf)
{
    hmi_nexus_d211_ge2d_image_cache_entry_t * entry = g_state.image_cache;

    if(source_key == NULL || draw_buf == NULL) {
        return NULL;
    }

    while(entry != NULL) {
        if(entry->source_key == source_key &&
           entry->width == draw_buf->header.w &&
           entry->height == draw_buf->header.h &&
           entry->stride == draw_buf->header.stride &&
           entry->color_format == draw_buf->header.cf) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

static void destroy_image_cache_entry(hmi_nexus_d211_ge2d_image_cache_entry_t * entry)
{
    if(entry == NULL) {
        return;
    }

    if(entry->dma_draw_buf != NULL) {
        lv_draw_buf_destroy(entry->dma_draw_buf);
    }

    lv_free(entry);
}

static void trim_image_cache(void)
{
    hmi_nexus_d211_ge2d_image_cache_entry_t * entry = g_state.image_cache;
    hmi_nexus_d211_ge2d_image_cache_entry_t * prev = NULL;
    uint32_t count = 0U;

    while(entry != NULL) {
        ++count;
        if(count > HMI_NEXUS_D211_GE2D_IMAGE_CACHE_LIMIT) {
            hmi_nexus_d211_ge2d_image_cache_entry_t * stale = entry;
            if(prev != NULL) {
                prev->next = NULL;
            }
            while(stale != NULL) {
                hmi_nexus_d211_ge2d_image_cache_entry_t * next = stale->next;
                destroy_image_cache_entry(stale);
                stale = next;
            }
            return;
        }

        prev = entry;
        entry = entry->next;
    }
}

static lv_draw_buf_t * duplicate_rgb565a8_to_argb8888_dma(const lv_draw_buf_t * source_draw_buf)
{
    lv_draw_buf_t * dma_draw_buf;
    const uint8_t * source_color_row;
    const uint8_t * source_alpha_row;
    uint32_t y;

    if(source_draw_buf == NULL || source_draw_buf->data == NULL ||
       source_draw_buf->header.cf != LV_COLOR_FORMAT_RGB565A8) {
        return NULL;
    }

    dma_draw_buf = hmi_nexus_lvgl_draw_buf_create_with_handlers(
        hmi_nexus_lvgl_image_cache_draw_buf_handlers(),
        source_draw_buf->header.w,
        source_draw_buf->header.h,
        LV_COLOR_FORMAT_ARGB8888,
        LV_STRIDE_AUTO);
    if(dma_draw_buf == NULL || dma_draw_buf->data == NULL) {
        if(dma_draw_buf != NULL) {
            lv_draw_buf_destroy(dma_draw_buf);
        }
        return NULL;
    }

    source_color_row = (const uint8_t *)source_draw_buf->data;
    source_alpha_row = source_color_row +
                       ((uint32_t)source_draw_buf->header.stride * source_draw_buf->header.h);

    for(y = 0; y < source_draw_buf->header.h; ++y) {
        const uint16_t * source_rgb565 = (const uint16_t *)source_color_row;
        const uint8_t * source_alpha = source_alpha_row;
        lv_color32_t * dest_argb8888 =
            (lv_color32_t *)((uint8_t *)dma_draw_buf->data + dma_draw_buf->header.stride * y);
        uint32_t x;

        for(x = 0; x < source_draw_buf->header.w; ++x) {
            const uint16_t pixel = source_rgb565[x];
            const uint8_t red5 = (uint8_t)((pixel >> 11) & 0x1fU);
            const uint8_t green6 = (uint8_t)((pixel >> 5) & 0x3fU);
            const uint8_t blue5 = (uint8_t)(pixel & 0x1fU);

            dest_argb8888[x].red = (uint8_t)((red5 << 3) | (red5 >> 2));
            dest_argb8888[x].green = (uint8_t)((green6 << 2) | (green6 >> 4));
            dest_argb8888[x].blue = (uint8_t)((blue5 << 3) | (blue5 >> 2));
            dest_argb8888[x].alpha = source_alpha[x];
        }

        source_color_row += source_draw_buf->header.stride;
        source_alpha_row += source_draw_buf->header.stride / 2U;
    }

    if(lv_draw_buf_has_flag((lv_draw_buf_t *)source_draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED)) {
        dma_draw_buf->header.flags |= LV_IMAGE_FLAGS_PREMULTIPLIED;
    }

    return dma_draw_buf;
}

static const lv_draw_buf_t * ensure_hw_source_draw_buf(const void * source_key,
                                                       const lv_draw_buf_t * draw_buf)
{
    hmi_nexus_d211_ge2d_image_cache_entry_t * cache_entry;
    lv_draw_buf_t * dma_draw_buf;

    if(draw_buf == NULL || draw_buf->data == NULL) {
        return NULL;
    }

    if(source_draw_buf_supported(draw_buf)) {
        return draw_buf;
    }

    if(g_state.dma_heap_fd < 0 || !bridgeable_source_color_format(draw_buf->header.cf)) {
        return NULL;
    }

    cache_entry = find_image_cache_entry(source_key, draw_buf);
    if(cache_entry != NULL) {
        if(cache_entry->dma_draw_buf != NULL &&
           source_draw_buf_supported(cache_entry->dma_draw_buf)) {
            return cache_entry->dma_draw_buf;
        }
        return NULL;
    }

    if(draw_buf->header.cf == LV_COLOR_FORMAT_RGB565A8) {
        dma_draw_buf = duplicate_rgb565a8_to_argb8888_dma(draw_buf);
    }
    else {
        dma_draw_buf = hmi_nexus_lvgl_draw_buf_dup_with_handlers(
            hmi_nexus_lvgl_image_cache_draw_buf_handlers(), draw_buf);
    }
    if(dma_draw_buf == NULL || !source_draw_buf_supported(dma_draw_buf)) {
        if(dma_draw_buf != NULL) {
            lv_draw_buf_destroy(dma_draw_buf);
        }
        return NULL;
    }

    cache_entry = lv_malloc_zeroed(sizeof(*cache_entry));
    if(cache_entry == NULL) {
        lv_draw_buf_destroy(dma_draw_buf);
        return NULL;
    }

    cache_entry->source_key = source_key;
    cache_entry->width = draw_buf->header.w;
    cache_entry->height = draw_buf->header.h;
    cache_entry->stride = draw_buf->header.stride;
    cache_entry->color_format = draw_buf->header.cf;
    cache_entry->dma_draw_buf = dma_draw_buf;
    cache_entry->next = g_state.image_cache;
    g_state.image_cache = cache_entry;
    trim_image_cache();

    return dma_draw_buf;
}

static bool image_task_supported(lv_draw_task_t * task)
{
    const lv_draw_image_dsc_t * draw_dsc;
    lv_image_src_t src_type;

    if(task == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
    if(!image_descriptor_supported(draw_dsc)) {
        return false;
    }

    if(!destination_layer_supported(&draw_dsc->base)) {
        return false;
    }

    /* Stay aligned with the vendor LVGL v9 GE2D port: only images backed by
     * MPP/DMA-aware buffers are scheduled onto the GE2D draw unit. */
    if(draw_dsc->header.cf == LV_COLOR_FORMAT_RAW) {
        return is_fake_image_source(draw_dsc);
    }
    if(draw_dsc->header.cf == LV_COLOR_FORMAT_RGB565A8) {
        return true;
    }
    if(!is_draw_buf_supported(draw_dsc->header.cf)) {
        return false;
    }

    src_type = lv_image_src_get_type(draw_dsc->src);
    return src_type == LV_IMAGE_SRC_FILE || src_type == LV_IMAGE_SRC_VARIABLE;
}

static bool layer_task_supported(lv_draw_task_t * task)
{
    const lv_draw_image_dsc_t * draw_dsc;
    const lv_layer_t * source_layer;

    if(task == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
    source_layer = (const lv_layer_t *)draw_dsc->src;
    if(!layer_descriptor_supported(draw_dsc, source_layer)) {
        return false;
    }

    if(!destination_layer_supported(&draw_dsc->base)) {
        return false;
    }

    return source_draw_buf_supported(source_layer->draw_buf);
}

static bool populate_mpp_buf_from_draw_buf(const lv_draw_buf_t * draw_buf,
                                           struct mpp_buf * mpp_buf,
                                           hmi_nexus_d211_ge2d_buffer_entry_t ** entry_out)
{
    hmi_nexus_d211_ge2d_buffer_entry_t * entry;

    if(draw_buf == NULL || mpp_buf == NULL) {
        return false;
    }

    entry = find_buffer_entry(draw_buf->data);
    memset(mpp_buf, 0, sizeof(*mpp_buf));
    if(entry != NULL) {
        mpp_buf->format = to_mpp_pixel_format(draw_buf->header.cf);
        if(mpp_buf->format == MPP_FMT_MAX) {
            return false;
        }

        if(entry->memory_type == HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF) {
            if(entry->dma_fd < 0) {
                return false;
            }
            mpp_buf->buf_type = MPP_DMA_BUF_FD;
            mpp_buf->fd[0] = entry->dma_fd;
        }
        else if(entry->memory_type == HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_PHYSICAL) {
            if(entry->physical_address == 0U) {
                return false;
            }
            mpp_buf->buf_type = MPP_PHY_ADDR;
            mpp_buf->phy_addr[0] = (unsigned int)entry->physical_address;
        }
        else {
            return false;
        }

        mpp_buf->stride[0] = draw_buf->header.stride;
        mpp_buf->size.width = draw_buf->header.w;
        mpp_buf->size.height = draw_buf->header.h;

        if(lv_draw_buf_has_flag((lv_draw_buf_t *)draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED)) {
            mpp_buf->flags |= MPP_BUF_IS_PREMULTIPLY;
        }
    }
    else if(draw_buf->header.cf >= LV_COLOR_FORMAT_YUV_START &&
            (draw_buf->header.flags & LV_IMAGE_FLAGS_USER8) != 0U) {
        memcpy(mpp_buf, draw_buf->data, sizeof(*mpp_buf));
        if(mpp_buf->format == MPP_FMT_MAX) {
            mpp_buf->format = to_mpp_pixel_format(draw_buf->header.cf);
        }
        if(mpp_buf->size.width <= 0) {
            mpp_buf->size.width = draw_buf->header.w;
        }
        if(mpp_buf->size.height <= 0) {
            mpp_buf->size.height = draw_buf->header.h;
        }
        if(mpp_buf->stride[0] == 0U) {
            mpp_buf->stride[0] = draw_buf->header.stride;
        }
    }
    else {
        return false;
    }

    if(entry_out != NULL) {
        *entry_out = entry;
    }

    return true;
}

static int begin_dma_access(hmi_nexus_d211_ge2d_buffer_entry_t * entry)
{
    if(entry == NULL ||
       entry->memory_type != HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF ||
       entry->dma_fd < 0) {
        return 0;
    }

    return mpp_ge_add_dmabuf(g_state.ge, entry->dma_fd);
}

static void end_dma_access(hmi_nexus_d211_ge2d_buffer_entry_t * entry)
{
    if(entry == NULL ||
       entry->memory_type != HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF ||
       entry->dma_fd < 0) {
        return;
    }

    mpp_ge_rm_dmabuf(g_state.ge, entry->dma_fd);
}

static void sync_mpp_buf_for_hw(const struct mpp_buf * mpp_buf)
{
    int i;

    if(mpp_buf == NULL || mpp_buf->buf_type != MPP_DMA_BUF_FD) {
        return;
    }

    for(i = 0; i < 3; ++i) {
        if(mpp_buf->fd[i] >= 0) {
            dmabuf_sync(mpp_buf->fd[i], CACHE_CLEAN);
        }
    }
}

static int begin_mpp_buf_dma_access(const struct mpp_buf * mpp_buf)
{
    int i;

    if(mpp_buf == NULL || mpp_buf->buf_type != MPP_DMA_BUF_FD) {
        return 0;
    }

    for(i = 0; i < 3; ++i) {
        if(mpp_buf->fd[i] >= 0 && mpp_ge_add_dmabuf(g_state.ge, mpp_buf->fd[i]) != 0) {
            while(--i >= 0) {
                if(mpp_buf->fd[i] >= 0) {
                    mpp_ge_rm_dmabuf(g_state.ge, mpp_buf->fd[i]);
                }
            }
            return -1;
        }
    }

    return 0;
}

static void end_mpp_buf_dma_access(const struct mpp_buf * mpp_buf)
{
    int i;

    if(mpp_buf == NULL || mpp_buf->buf_type != MPP_DMA_BUF_FD) {
        return;
    }

    for(i = 0; i < 3; ++i) {
        if(mpp_buf->fd[i] >= 0) {
            mpp_ge_rm_dmabuf(g_state.ge, mpp_buf->fd[i]);
        }
    }
}

static unsigned int to_ge2d_image_rotation(int32_t rotation)
{
    switch(normalize_rotation(rotation)) {
        case 900:
            return MPP_ROTATION_90;
        case 1800:
            return MPP_ROTATION_180;
        case 2700:
            return MPP_ROTATION_270;
        case 0:
        default:
            return MPP_ROTATION_0;
    }
}

static bool compute_source_crop_for_rotation(const lv_draw_image_dsc_t * draw_dsc,
                                             const lv_area_t * img_coords,
                                             const lv_area_t * clipped_img_area,
                                             int32_t source_width,
                                             int32_t source_height,
                                             lv_area_t * source_crop)
{
    lv_area_t input_area;

    if(draw_dsc == NULL || img_coords == NULL || clipped_img_area == NULL || source_crop == NULL) {
        return false;
    }
    if(source_width <= 0 || source_height <= 0) {
        return false;
    }

    input_area = *clipped_img_area;
    lv_area_move(&input_area, -img_coords->x1, -img_coords->y1);

    compute_inverse_transform_area(&input_area,
                                   source_crop,
                                   draw_dsc->rotation,
                                   draw_dsc->scale_x,
                                   draw_dsc->scale_y,
                                   &draw_dsc->pivot,
                                   source_width,
                                   source_height);

    if(source_crop->x1 < 0 || source_crop->y1 < 0 ||
       source_crop->x1 > source_crop->x2 || source_crop->y1 > source_crop->y2 ||
       source_crop->x2 >= source_width || source_crop->y2 >= source_height) {
        return false;
    }

    return true;
}

static bool rgb_transfer_size_valid(int32_t source_width,
                                    int32_t source_height,
                                    int32_t dest_width,
                                    int32_t dest_height)
{
    return source_width >= 4 && source_height >= 4 &&
           dest_width >= 4 && dest_height >= 4;
}

static bool yuv_transfer_size_valid(int32_t source_width,
                                    int32_t source_height,
                                    int32_t dest_width,
                                    int32_t dest_height)
{
    return source_width >= 8 && source_height >= 8 &&
           dest_width >= 8 && dest_height >= 8;
}

static bool execute_blit(lv_draw_unit_t * draw_unit,
                         const lv_draw_image_dsc_t * draw_dsc,
                         const lv_draw_buf_t * source_draw_buf,
                         const lv_area_t * img_coords,
                         const lv_area_t * clipped_img_area)
{
    lv_layer_t * layer;
    hmi_nexus_d211_ge2d_buffer_entry_t * source_entry = NULL;
    hmi_nexus_d211_ge2d_buffer_entry_t * dest_entry = NULL;
    struct ge_bitblt blt;
    int source_registered = 0;
    int dest_registered = 0;
    lv_area_t source_crop;

    if(draw_unit == NULL || draw_dsc == NULL ||
       source_draw_buf == NULL || img_coords == NULL || clipped_img_area == NULL) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    memset(&blt, 0, sizeof(blt));
    if(!populate_mpp_buf_from_draw_buf(source_draw_buf, &blt.src_buf, &source_entry)) {
        return false;
    }
    if(!populate_mpp_buf_from_draw_buf(layer->draw_buf, &blt.dst_buf, &dest_entry)) {
        return false;
    }

    if(!compute_source_crop_for_rotation(draw_dsc,
                                         img_coords,
                                         clipped_img_area,
                                         source_draw_buf->header.w,
                                         source_draw_buf->header.h,
                                         &source_crop)) {
        return false;
    }

    sync_for_hw(source_entry);
    sync_for_hw(dest_entry);
    if(source_entry == NULL) {
        sync_mpp_buf_for_hw(&blt.src_buf);
    }

    if(source_entry != NULL) {
        source_registered = begin_dma_access(source_entry);
    }
    else {
        source_registered = begin_mpp_buf_dma_access(&blt.src_buf);
    }
    if(source_registered < 0) {
        g_state.stats.hw_submit_fail_count++;
        return false;
    }
    dest_registered = begin_dma_access(dest_entry);
    if(dest_registered < 0) {
        g_state.stats.hw_submit_fail_count++;
        if(source_registered == 0) {
            if(source_entry != NULL) {
                end_dma_access(source_entry);
            }
            else {
                end_mpp_buf_dma_access(&blt.src_buf);
            }
        }
        return false;
    }

    blt.src_buf.crop_en = 1;
    blt.src_buf.crop.x = source_crop.x1;
    blt.src_buf.crop.y = source_crop.y1;
    blt.src_buf.crop.width = lv_area_get_width(&source_crop);
    blt.src_buf.crop.height = lv_area_get_height(&source_crop);

    if(source_draw_buf->header.cf >= LV_COLOR_FORMAT_YUV_START) {
        if(!yuv_transfer_size_valid(blt.src_buf.crop.width,
                                    blt.src_buf.crop.height,
                                    lv_area_get_width(clipped_img_area),
                                    lv_area_get_height(clipped_img_area))) {
            return false;
        }
    }
    else if(!rgb_transfer_size_valid(blt.src_buf.crop.width,
                                     blt.src_buf.crop.height,
                                     lv_area_get_width(clipped_img_area),
                                     lv_area_get_height(clipped_img_area))) {
        return false;
    }

    blt.dst_buf.crop_en = 1;
    blt.dst_buf.crop.x = clipped_img_area->x1 - layer->buf_area.x1;
    blt.dst_buf.crop.y = clipped_img_area->y1 - layer->buf_area.y1;
    blt.dst_buf.crop.width = lv_area_get_width(clipped_img_area);
    blt.dst_buf.crop.height = lv_area_get_height(clipped_img_area);

    if(draw_dsc->opa >= LV_OPA_MAX &&
       source_draw_buf->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        blt.ctrl.alpha_en = 0;
    }
    else {
        blt.ctrl.alpha_en = 1;
        blt.ctrl.src_alpha_mode = 2;
        blt.ctrl.src_global_alpha = draw_dsc->opa;
    }

    if(layer->color_format == LV_COLOR_FORMAT_RGB565 && !blt.ctrl.alpha_en) {
        blt.ctrl.dither_en = 1;
    }

    blt.ctrl.flags = to_ge2d_image_rotation(draw_dsc->rotation);

    if(mpp_ge_bitblt(g_state.ge, &blt) != 0 ||
       mpp_ge_emit(g_state.ge) != 0 ||
       mpp_ge_sync(g_state.ge) != 0) {
        g_state.stats.hw_submit_fail_count++;
        if(dest_registered == 0) {
            end_dma_access(dest_entry);
        }
        if(source_registered == 0) {
            if(source_entry != NULL) {
                end_dma_access(source_entry);
            }
            else {
                end_mpp_buf_dma_access(&blt.src_buf);
            }
        }
        return false;
    }

    if(dest_registered == 0) {
        end_dma_access(dest_entry);
    }
    if(source_registered == 0) {
        if(source_entry != NULL) {
            end_dma_access(source_entry);
        }
        else {
            end_mpp_buf_dma_access(&blt.src_buf);
        }
    }
    sync_for_cpu(dest_entry);
    return true;
}

static bool execute_rotate_any_degree(lv_draw_unit_t * draw_unit,
                                      const lv_draw_image_dsc_t * draw_dsc,
                                      const lv_draw_buf_t * source_draw_buf,
                                      const lv_area_t * img_coords,
                                      const lv_area_t * clipped_img_area)
{
    lv_layer_t * layer;
    hmi_nexus_d211_ge2d_buffer_entry_t * source_entry = NULL;
    hmi_nexus_d211_ge2d_buffer_entry_t * dest_entry = NULL;
    struct ge_rotation rot;
    int source_registered = 0;
    int dest_registered = 0;

    if(draw_unit == NULL || draw_dsc == NULL ||
       source_draw_buf == NULL || img_coords == NULL || clipped_img_area == NULL) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    memset(&rot, 0, sizeof(rot));
    if(!populate_mpp_buf_from_draw_buf(source_draw_buf, &rot.src_buf, &source_entry)) {
        return false;
    }
    if(!populate_mpp_buf_from_draw_buf(layer->draw_buf, &rot.dst_buf, &dest_entry)) {
        return false;
    }

    rot.src_rot_center.x = draw_dsc->pivot.x;
    rot.src_rot_center.y = draw_dsc->pivot.y;

    rot.dst_buf.crop_en = 1;
    rot.dst_buf.crop.x = clipped_img_area->x1 - layer->buf_area.x1;
    rot.dst_buf.crop.y = clipped_img_area->y1 - layer->buf_area.y1;
    rot.dst_buf.crop.width = lv_area_get_width(clipped_img_area);
    rot.dst_buf.crop.height = lv_area_get_height(clipped_img_area);

    if(!rgb_transfer_size_valid(source_draw_buf->header.w,
                                source_draw_buf->header.h,
                                rot.dst_buf.crop.width,
                                rot.dst_buf.crop.height)) {
        return false;
    }

    rot.dst_rot_center.x = img_coords->x1 + draw_dsc->pivot.x - clipped_img_area->x1;
    rot.dst_rot_center.y = img_coords->y1 + draw_dsc->pivot.y - clipped_img_area->y1;
    compute_rotation_q12(draw_dsc->rotation, &rot.angle_sin, &rot.angle_cos);

    if(draw_dsc->opa >= LV_OPA_MAX &&
       source_draw_buf->header.cf != LV_COLOR_FORMAT_ARGB8888) {
        rot.ctrl.alpha_en = 0;
    }
    else {
        rot.ctrl.alpha_en = 1;
        rot.ctrl.src_alpha_mode = 2;
        rot.ctrl.src_global_alpha = draw_dsc->opa;
    }

    sync_for_hw(source_entry);
    sync_for_hw(dest_entry);
    if(source_entry == NULL) {
        sync_mpp_buf_for_hw(&rot.src_buf);
    }

    if(source_entry != NULL) {
        source_registered = begin_dma_access(source_entry);
    }
    else {
        source_registered = begin_mpp_buf_dma_access(&rot.src_buf);
    }
    if(source_registered < 0) {
        g_state.stats.hw_submit_fail_count++;
        return false;
    }
    dest_registered = begin_dma_access(dest_entry);
    if(dest_registered < 0) {
        g_state.stats.hw_submit_fail_count++;
        if(source_registered == 0) {
            if(source_entry != NULL) {
                end_dma_access(source_entry);
            }
            else {
                end_mpp_buf_dma_access(&rot.src_buf);
            }
        }
        return false;
    }

    if(mpp_ge_rotate(g_state.ge, &rot) != 0 ||
       mpp_ge_emit(g_state.ge) != 0 ||
       mpp_ge_sync(g_state.ge) != 0) {
        g_state.stats.hw_submit_fail_count++;
        if(dest_registered == 0) {
            end_dma_access(dest_entry);
        }
        if(source_registered == 0) {
            if(source_entry != NULL) {
                end_dma_access(source_entry);
            }
            else {
                end_mpp_buf_dma_access(&rot.src_buf);
            }
        }
        return false;
    }

    if(dest_registered == 0) {
        end_dma_access(dest_entry);
    }
    if(source_registered == 0) {
        if(source_entry != NULL) {
            end_dma_access(source_entry);
        }
        else {
            end_mpp_buf_dma_access(&rot.src_buf);
        }
    }
    sync_for_cpu(dest_entry);
    return true;
}

static bool execute_image_transfer(lv_draw_unit_t * draw_unit,
                                   const lv_draw_image_dsc_t * draw_dsc,
                                   const lv_draw_buf_t * source_draw_buf,
                                   const lv_area_t * img_coords,
                                   const lv_area_t * clipped_img_area)
{
    if(is_right_angle_rotation(draw_dsc->rotation)) {
        return execute_blit(draw_unit, draw_dsc, source_draw_buf, img_coords, clipped_img_area);
    }

    /* Arbitrary-angle rotation lets GE2D clip on the destination side directly. */
    return execute_rotate_any_degree(draw_unit,
                                     draw_dsc,
                                     source_draw_buf,
                                     img_coords,
                                     clipped_img_area);
}

static int32_t clamp_fill_radius_for_area(const lv_area_t * area, int32_t requested_radius)
{
    int32_t short_side;

    if(area == NULL) {
        return 0;
    }

    short_side = LV_MIN(lv_area_get_width(area), lv_area_get_height(area));
    if(short_side <= 0) {
        return 0;
    }

    if(requested_radius < 0) {
        requested_radius = 0;
    }

    if(requested_radius > (short_side >> 1)) {
        requested_radius = short_side >> 1;
    }

    return requested_radius;
}

static bool submit_fill_rect(struct ge_fillrect * request,
                             const lv_area_t * rect_area,
                             const lv_layer_t * target_layer,
                             const lv_area_t * clip_area)
{
    lv_area_t clipped_area;
    lv_area_t blend_area;

    if(request == NULL || rect_area == NULL || target_layer == NULL || clip_area == NULL) {
        return false;
    }

    if(!lv_area_intersect(&clipped_area, rect_area, clip_area)) {
        return true;
    }

    /* LVGL task clip can extend outside the current layer buffer when
     * shadows/borders spill beyond the backing area. Clamp again to the
     * layer bounds so GE never sees negative/out-of-range crop values. */
    if(!lv_area_intersect(&blend_area, &clipped_area, &target_layer->buf_area)) {
        return true;
    }

    lv_area_move(&blend_area, -target_layer->buf_area.x1, -target_layer->buf_area.y1);
    request->dst_buf.crop_en = 1;
    request->dst_buf.crop.x = blend_area.x1;
    request->dst_buf.crop.y = blend_area.y1;
    request->dst_buf.crop.width = lv_area_get_width(&blend_area);
    request->dst_buf.crop.height = lv_area_get_height(&blend_area);

    return mpp_ge_fillrect(g_state.ge, request) == 0;
}

static bool setup_fill_request(const lv_draw_buf_t * draw_buf,
                               lv_color_t color,
                               lv_opa_t opa,
                               int alpha_en_override,
                               struct ge_fillrect * fill,
                               hmi_nexus_d211_ge2d_buffer_entry_t ** dest_entry)
{
    if(draw_buf == NULL || fill == NULL) {
        return false;
    }

    memset(fill, 0, sizeof(*fill));
    fill->type = GE_NO_GRADIENT;
    if(!populate_mpp_buf_from_draw_buf(draw_buf, &fill->dst_buf, dest_entry)) {
        return false;
    }

    if(opa >= LV_OPA_MAX) {
        fill->start_color = lv_color_to_u32(color);
    }
    else {
        fill->start_color = ((uint32_t)opa << 24) |
                            ((uint32_t)color.red << 16) |
                            ((uint32_t)color.green << 8) |
                            (uint32_t)color.blue;
    }

    if(alpha_en_override >= 0) {
        fill->ctrl.alpha_en = alpha_en_override != 0 ? 1 : 0;
    }
    else if(opa < LV_OPA_MAX) {
        fill->ctrl.alpha_en = 1;
    }

    fill->ctrl.src_alpha_mode = 0;
    return true;
}

static bool compute_border_geometry(const lv_area_t * coords,
                                    const lv_draw_border_dsc_t * draw_dsc,
                                    hmi_nexus_d211_ge2d_border_geometry_t * geometry)
{
    int32_t short_side;

    if(coords == NULL || draw_dsc == NULL || geometry == NULL) {
        return false;
    }

    if(lv_area_get_width(coords) <= 0 || lv_area_get_height(coords) <= 0) {
        return false;
    }

    memset(geometry, 0, sizeof(*geometry));
    geometry->outer_area = *coords;
    geometry->outer_radius = draw_dsc->radius;

    short_side = LV_MIN(lv_area_get_width(coords), lv_area_get_height(coords));
    if(geometry->outer_radius > (short_side >> 1)) {
        geometry->outer_radius = short_side >> 1;
    }
    if(geometry->outer_radius < 0) {
        geometry->outer_radius = 0;
    }

    geometry->inner_area = *coords;
    geometry->inner_area.x1 += ((draw_dsc->side & LV_BORDER_SIDE_LEFT) != 0U)
                                   ? draw_dsc->width
                                   : -(draw_dsc->width + geometry->outer_radius);
    geometry->inner_area.x2 -= ((draw_dsc->side & LV_BORDER_SIDE_RIGHT) != 0U)
                                   ? draw_dsc->width
                                   : -(draw_dsc->width + geometry->outer_radius);
    geometry->inner_area.y1 += ((draw_dsc->side & LV_BORDER_SIDE_TOP) != 0U)
                                   ? draw_dsc->width
                                   : -(draw_dsc->width + geometry->outer_radius);
    geometry->inner_area.y2 -= ((draw_dsc->side & LV_BORDER_SIDE_BOTTOM) != 0U)
                                   ? draw_dsc->width
                                   : -(draw_dsc->width + geometry->outer_radius);

    geometry->inner_radius = geometry->outer_radius - draw_dsc->width;
    if(geometry->inner_radius < 0) {
        geometry->inner_radius = 0;
    }

    geometry->top_side = geometry->outer_area.y1 <= geometry->inner_area.y1;
    geometry->bottom_side = geometry->outer_area.y2 >= geometry->inner_area.y2;
    geometry->left_side = geometry->outer_area.x1 <= geometry->inner_area.x1;
    geometry->right_side = geometry->outer_area.x2 >= geometry->inner_area.x2;
    geometry->rounded = !(geometry->outer_radius == 0 && geometry->inner_radius == 0);

    geometry->core_area.x1 = LV_MAX(geometry->outer_area.x1 + geometry->outer_radius,
                                    geometry->inner_area.x1);
    geometry->core_area.x2 = LV_MIN(geometry->outer_area.x2 - geometry->outer_radius,
                                    geometry->inner_area.x2);
    geometry->core_area.y1 = LV_MAX(geometry->outer_area.y1 + geometry->outer_radius,
                                    geometry->inner_area.y1);
    geometry->core_area.y2 = LV_MIN(geometry->outer_area.y2 - geometry->outer_radius,
                                    geometry->inner_area.y2);
    return true;
}

static void sw_border_clip(lv_draw_unit_t * draw_unit,
                           const lv_draw_border_dsc_t * draw_dsc,
                           const lv_area_t * coords,
                           const lv_area_t * original_clip_area,
                           const lv_area_t * corner_area)
{
    lv_area_t clipped_corner;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL ||
       original_clip_area == NULL || corner_area == NULL) {
        return;
    }

    if(!lv_area_intersect(&clipped_corner, corner_area, original_clip_area)) {
        return;
    }

    draw_unit->clip_area = &clipped_corner;
    lv_draw_sw_border(draw_unit, draw_dsc, coords);
}

static bool submit_border_rects(struct ge_fillrect * fill,
                                const hmi_nexus_d211_ge2d_border_geometry_t * geometry,
                                const lv_layer_t * layer,
                                const lv_area_t * clip_area,
                                bool * submitted)
{
    lv_area_t rect_area;

    if(fill == NULL || geometry == NULL || layer == NULL || clip_area == NULL) {
        return false;
    }

    if(submitted != NULL) {
        *submitted = false;
    }

    if(!geometry->rounded) {
        if(geometry->top_side) {
            lv_area_set(&rect_area,
                        geometry->outer_area.x1,
                        geometry->outer_area.y1,
                        geometry->outer_area.x2,
                        geometry->inner_area.y1 - 1);
            if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
                return false;
            }
            if(submitted != NULL) {
                *submitted = true;
            }
        }

        if(geometry->bottom_side) {
            lv_area_set(&rect_area,
                        geometry->outer_area.x1,
                        geometry->inner_area.y2 + 1,
                        geometry->outer_area.x2,
                        geometry->outer_area.y2);
            if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
                return false;
            }
            if(submitted != NULL) {
                *submitted = true;
            }
        }

        if(geometry->left_side) {
            lv_area_set(&rect_area,
                        geometry->outer_area.x1,
                        geometry->top_side ? geometry->inner_area.y1 : geometry->outer_area.y1,
                        geometry->inner_area.x1 - 1,
                        geometry->bottom_side ? geometry->inner_area.y2 : geometry->outer_area.y2);
            if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
                return false;
            }
            if(submitted != NULL) {
                *submitted = true;
            }
        }

        if(geometry->right_side) {
            lv_area_set(&rect_area,
                        geometry->inner_area.x2 + 1,
                        geometry->top_side ? geometry->inner_area.y1 : geometry->outer_area.y1,
                        geometry->outer_area.x2,
                        geometry->bottom_side ? geometry->inner_area.y2 : geometry->outer_area.y2);
            if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
                return false;
            }
            if(submitted != NULL) {
                *submitted = true;
            }
        }

        return true;
    }

    if(geometry->core_area.x1 > geometry->core_area.x2 ||
       geometry->core_area.y1 > geometry->core_area.y2) {
        return true;
    }

    if(geometry->top_side) {
        lv_area_set(&rect_area,
                    geometry->core_area.x1,
                    geometry->outer_area.y1,
                    geometry->core_area.x2,
                    geometry->inner_area.y1 - 1);
        if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
            return false;
        }
        if(submitted != NULL) {
            *submitted = true;
        }
    }

    if(geometry->bottom_side) {
        lv_area_set(&rect_area,
                    geometry->core_area.x1,
                    geometry->inner_area.y2 + 1,
                    geometry->core_area.x2,
                    geometry->outer_area.y2);
        if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
            return false;
        }
        if(submitted != NULL) {
            *submitted = true;
        }
    }

    if(geometry->left_side && geometry->right_side &&
       geometry->inner_area.x1 >= geometry->inner_area.x2) {
        lv_area_set(&rect_area,
                    geometry->outer_area.x1,
                    geometry->core_area.y1,
                    geometry->outer_area.x2,
                    geometry->core_area.y2);
        if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
            return false;
        }
        if(submitted != NULL) {
            *submitted = true;
        }
    }
    else {
        if(geometry->left_side) {
            lv_area_set(&rect_area,
                        geometry->outer_area.x1,
                        geometry->core_area.y1,
                        geometry->inner_area.x1 - 1,
                        geometry->core_area.y2);
            if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
                return false;
            }
            if(submitted != NULL) {
                *submitted = true;
            }
        }

        if(geometry->right_side) {
            lv_area_set(&rect_area,
                        geometry->inner_area.x2 + 1,
                        geometry->core_area.y1,
                        geometry->outer_area.x2,
                        geometry->core_area.y2);
            if(!submit_fill_rect(fill, &rect_area, layer, clip_area)) {
                return false;
            }
            if(submitted != NULL) {
                *submitted = true;
            }
        }
    }

    return true;
}

static void sw_fill_corner_clip(lv_draw_unit_t * draw_unit,
                                const lv_draw_fill_dsc_t * draw_dsc,
                                const lv_area_t * coords,
                                const lv_area_t * original_clip_area,
                                const lv_area_t * corner_area)
{
    lv_draw_fill_dsc_t sw_fill_dsc;
    lv_area_t clipped_corner;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL ||
       original_clip_area == NULL || corner_area == NULL) {
        return;
    }

    if(!lv_area_intersect(&clipped_corner, corner_area, original_clip_area)) {
        return;
    }

    sw_fill_dsc = *draw_dsc;
    draw_unit->clip_area = &clipped_corner;
    lv_draw_sw_fill(draw_unit, &sw_fill_dsc, coords);
}

static bool execute_fill_core(lv_draw_unit_t * draw_unit,
                              const lv_draw_fill_dsc_t * draw_dsc,
                              const lv_area_t * coords,
                              int alpha_en_override,
                              bool count_fill_stats)
{
    lv_layer_t * layer;
    lv_draw_buf_t * draw_buf;
    hmi_nexus_d211_ge2d_buffer_entry_t * dest_entry = NULL;
    struct ge_fillrect fill;
    lv_area_t rects[3];
    lv_area_t corner;
    const lv_area_t * original_clip_area;
    int32_t radius;
    int rect_count = 0;
    int i;
    bool submitted = false;
    bool hw_success = false;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    draw_buf = layer->draw_buf;
    original_clip_area = draw_unit->clip_area;
    radius = clamp_fill_radius_for_area(coords, draw_dsc->radius);

    if(!setup_fill_request(draw_buf,
                           draw_dsc->color,
                           draw_dsc->opa,
                           alpha_en_override,
                           &fill,
                           &dest_entry)) {
        return false;
    }

    sync_for_hw(dest_entry);
    if(begin_dma_access(dest_entry) < 0) {
        g_state.stats.hw_submit_fail_count++;
        draw_unit->clip_area = original_clip_area;
        return false;
    }

    if(radius == 0) {
        rects[rect_count++] = *coords;
    }
    else {
        lv_area_set(&rects[rect_count++],
                    coords->x1,
                    coords->y1 + radius,
                    coords->x2,
                    coords->y2 - radius);
        lv_area_set(&rects[rect_count++],
                    coords->x1 + radius,
                    coords->y1,
                    coords->x2 - radius,
                    coords->y1 + radius - 1);
        lv_area_set(&rects[rect_count++],
                    coords->x1 + radius,
                    coords->y2 - radius + 1,
                    coords->x2 - radius,
                    coords->y2);
    }

    for(i = 0; i < rect_count; ++i) {
        if(rects[i].x1 > rects[i].x2 || rects[i].y1 > rects[i].y2) {
            continue;
        }
        if(!submit_fill_rect(&fill, &rects[i], layer, original_clip_area)) {
            g_state.stats.hw_submit_fail_count++;
            end_dma_access(dest_entry);
            sync_for_cpu(dest_entry);
            draw_unit->clip_area = original_clip_area;
            return false;
        }
        submitted = true;
    }

    if(submitted && mpp_ge_emit(g_state.ge) == 0 && mpp_ge_sync(g_state.ge) == 0) {
        if(count_fill_stats) {
            g_state.stats.fill_hw_count++;
        }
        hw_success = true;
    }
    else {
        if(submitted) {
            g_state.stats.hw_submit_fail_count++;
        }
    }

    end_dma_access(dest_entry);
    sync_for_cpu(dest_entry);
    draw_unit->clip_area = original_clip_area;

    if(radius > 0) {
        lv_area_set(&corner, coords->x1, coords->y1, coords->x1 + radius - 1, coords->y1 + radius - 1);
        sw_fill_corner_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);
        lv_area_set(&corner, coords->x2 - radius + 1, coords->y1, coords->x2, coords->y1 + radius - 1);
        sw_fill_corner_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);
        lv_area_set(&corner, coords->x1, coords->y2 - radius + 1, coords->x1 + radius - 1, coords->y2);
        sw_fill_corner_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);
        lv_area_set(&corner, coords->x2 - radius + 1, coords->y2 - radius + 1, coords->x2, coords->y2);
        sw_fill_corner_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);
    }

    return hw_success;
}

static void execute_fill(lv_draw_unit_t * draw_unit,
                         const lv_draw_fill_dsc_t * draw_dsc,
                         const lv_area_t * coords)
{
    (void)execute_fill_core(draw_unit, draw_dsc, coords, -1, true);
}

static inline uint8_t mix_u8_channel(uint8_t fg, uint8_t bg, lv_opa_t mix)
{
    return (uint8_t)LV_UDIV255((uint16_t)fg * mix + (uint16_t)bg * (255 - mix) +
                               LV_COLOR_MIX_ROUND_OFS);
}

/* The generic SW arc path blends one scanline at a time through lv_draw_sw_blend().
 * Replacing that hot inner loop with direct writes trims a lot of dispatch overhead
 * while keeping LVGL's exact angle/radius masks for visual correctness. */
static bool blend_solid_mask_row(lv_layer_t * layer,
                                 int32_t abs_x,
                                 int32_t abs_y,
                                 int32_t width,
                                 lv_color_t color,
                                 lv_opa_t opa,
                                 const lv_opa_t * mask_buf,
                                 lv_draw_sw_mask_res_t mask_res)
{
    uint8_t * row;
    int32_t i;

    if(layer == NULL || layer->draw_buf == NULL || width <= 0) {
        return false;
    }

    row = lv_draw_layer_go_to_xy(layer, abs_x - layer->buf_area.x1, abs_y - layer->buf_area.y1);
    if(row == NULL) {
        return false;
    }

    switch(layer->color_format) {
        case LV_COLOR_FORMAT_RGB565: {
            uint16_t * dest = (uint16_t *)row;
            const uint16_t color16 = lv_color_to_u16(color);

            if(mask_res == LV_DRAW_SW_MASK_RES_FULL_COVER) {
                if(opa >= LV_OPA_MAX) {
                    for(i = 0; i < width; ++i) {
                        dest[i] = color16;
                    }
                }
                else {
                    for(i = 0; i < width; ++i) {
                        dest[i] = lv_color_16_16_mix(color16, dest[i], opa);
                    }
                }
                return true;
            }

            for(i = 0; i < width; ++i) {
                lv_opa_t px_opa = mask_buf[i];
                if(px_opa <= LV_OPA_MIN) {
                    continue;
                }

                px_opa = LV_OPA_MIX2(opa, px_opa);
                if(px_opa >= LV_OPA_MAX) {
                    dest[i] = color16;
                }
                else if(px_opa > LV_OPA_MIN) {
                    dest[i] = lv_color_16_16_mix(color16, dest[i], px_opa);
                }
            }
            return true;
        }

        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_XRGB8888: {
            uint32_t * dest = (uint32_t *)row;

            if(mask_res == LV_DRAW_SW_MASK_RES_FULL_COVER) {
                for(i = 0; i < width; ++i) {
                    uint32_t bg = dest[i];
                    if(opa >= LV_OPA_MAX) {
                        dest[i] = (bg & 0xFF000000U) |
                                  ((uint32_t)color.red << 16) |
                                  ((uint32_t)color.green << 8) |
                                  (uint32_t)color.blue;
                    }
                    else {
                        uint8_t bg_red = (uint8_t)((bg >> 16) & 0xFFU);
                        uint8_t bg_green = (uint8_t)((bg >> 8) & 0xFFU);
                        uint8_t bg_blue = (uint8_t)(bg & 0xFFU);
                        uint8_t out_red = mix_u8_channel(color.red, bg_red, opa);
                        uint8_t out_green = mix_u8_channel(color.green, bg_green, opa);
                        uint8_t out_blue = mix_u8_channel(color.blue, bg_blue, opa);
                        dest[i] = (bg & 0xFF000000U) |
                                  ((uint32_t)out_red << 16) |
                                  ((uint32_t)out_green << 8) |
                                  (uint32_t)out_blue;
                    }
                }
                return true;
            }

            for(i = 0; i < width; ++i) {
                lv_opa_t px_opa = mask_buf[i];
                uint32_t bg;
                uint8_t bg_red;
                uint8_t bg_green;
                uint8_t bg_blue;

                if(px_opa <= LV_OPA_MIN) {
                    continue;
                }

                px_opa = LV_OPA_MIX2(opa, px_opa);
                if(px_opa <= LV_OPA_MIN) {
                    continue;
                }

                bg = dest[i];
                if(px_opa >= LV_OPA_MAX) {
                    dest[i] = (bg & 0xFF000000U) |
                              ((uint32_t)color.red << 16) |
                              ((uint32_t)color.green << 8) |
                              (uint32_t)color.blue;
                    continue;
                }

                bg_red = (uint8_t)((bg >> 16) & 0xFFU);
                bg_green = (uint8_t)((bg >> 8) & 0xFFU);
                bg_blue = (uint8_t)(bg & 0xFFU);
                dest[i] = (bg & 0xFF000000U) |
                          ((uint32_t)mix_u8_channel(color.red, bg_red, px_opa) << 16) |
                          ((uint32_t)mix_u8_channel(color.green, bg_green, px_opa) << 8) |
                          (uint32_t)mix_u8_channel(color.blue, bg_blue, px_opa);
            }
            return true;
        }

        case LV_COLOR_FORMAT_RGB888: {
            uint8_t * dest = row;

            if(mask_res == LV_DRAW_SW_MASK_RES_FULL_COVER) {
                for(i = 0; i < width; ++i) {
                    if(opa >= LV_OPA_MAX) {
                        dest[0] = color.blue;
                        dest[1] = color.green;
                        dest[2] = color.red;
                    }
                    else {
                        dest[0] = mix_u8_channel(color.blue, dest[0], opa);
                        dest[1] = mix_u8_channel(color.green, dest[1], opa);
                        dest[2] = mix_u8_channel(color.red, dest[2], opa);
                    }
                    dest += 3;
                }
                return true;
            }

            for(i = 0; i < width; ++i) {
                lv_opa_t px_opa = mask_buf[i];

                if(px_opa > LV_OPA_MIN) {
                    px_opa = LV_OPA_MIX2(opa, px_opa);
                    if(px_opa >= LV_OPA_MAX) {
                        dest[0] = color.blue;
                        dest[1] = color.green;
                        dest[2] = color.red;
                    }
                    else if(px_opa > LV_OPA_MIN) {
                        dest[0] = mix_u8_channel(color.blue, dest[0], px_opa);
                        dest[1] = mix_u8_channel(color.green, dest[1], px_opa);
                        dest[2] = mix_u8_channel(color.red, dest[2], px_opa);
                    }
                }

                dest += 3;
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

static bool execute_arc(lv_draw_unit_t * draw_unit,
                        const lv_draw_arc_dsc_t * draw_dsc,
                        const lv_area_t * coords)
{
    lv_layer_t * layer;
    lv_area_t area_out;
    lv_area_t area_in;
    lv_area_t clipped_area;
    lv_area_t blend_area;
    lv_draw_sw_mask_angle_param_t mask_angle_param;
    lv_draw_sw_mask_radius_param_t mask_out_param;
    lv_draw_sw_mask_radius_param_t mask_in_param;
    void * mask_list[4] = {0};
    lv_opa_t * mask_buf = NULL;
    int32_t start_angle;
    int32_t end_angle;
    int32_t blend_w;
    int32_t blend_h;
    int32_t width;
    int32_t h;
    bool mask_in_param_valid = false;
    bool handled = false;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    if(draw_dsc->img_src == NULL &&
       (draw_dsc->start_angle + 360 == draw_dsc->end_angle ||
        draw_dsc->start_angle == draw_dsc->end_angle + 360)) {
        lv_draw_border_dsc_t border_dsc;
        lv_draw_border_dsc_init(&border_dsc);
        border_dsc.base = draw_dsc->base;
        border_dsc.base.dsc_size = sizeof(border_dsc);
        border_dsc.color = draw_dsc->color;
        border_dsc.width = draw_dsc->width;
        border_dsc.opa = draw_dsc->opa;
        border_dsc.side = LV_BORDER_SIDE_FULL;
        border_dsc.radius = LV_RADIUS_CIRCLE;
        return execute_border(draw_unit, &border_dsc, coords);
    }

    if(draw_dsc->img_src != NULL || draw_dsc->rounded) {
        return false;
    }

    if(draw_dsc->opa <= LV_OPA_MIN || draw_dsc->width <= 0 ||
       draw_dsc->start_angle == draw_dsc->end_angle) {
        return true;
    }

    width = draw_dsc->width;
    if(width > draw_dsc->radius) {
        width = draw_dsc->radius;
    }

    area_out = *coords;
    if(!lv_area_intersect(&clipped_area, &area_out, draw_unit->clip_area)) {
        return true;
    }

    area_in = area_out;
    area_in.x1 += width;
    area_in.y1 += width;
    area_in.x2 -= width;
    area_in.y2 -= width;

    start_angle = (int32_t)draw_dsc->start_angle;
    end_angle = (int32_t)draw_dsc->end_angle;
    while(start_angle >= 360) {
        start_angle -= 360;
    }
    while(end_angle >= 360) {
        end_angle -= 360;
    }

    lv_draw_sw_mask_angle_init(&mask_angle_param,
                               draw_dsc->center.x,
                               draw_dsc->center.y,
                               start_angle,
                               end_angle);
    mask_list[0] = &mask_angle_param;

    lv_draw_sw_mask_radius_init(&mask_out_param, &area_out, LV_RADIUS_CIRCLE, false);
    mask_list[1] = &mask_out_param;

    if(lv_area_get_width(&area_in) > 0 && lv_area_get_height(&area_in) > 0) {
        lv_draw_sw_mask_radius_init(&mask_in_param, &area_in, LV_RADIUS_CIRCLE, true);
        mask_list[2] = &mask_in_param;
        mask_in_param_valid = true;
    }

    blend_h = lv_area_get_height(&clipped_area);
    blend_w = lv_area_get_width(&clipped_area);
    mask_buf = lv_malloc((size_t)blend_w);
    if(mask_buf == NULL) {
        handled = false;
        goto cleanup;
    }

    blend_area = clipped_area;
    blend_area.y2 = blend_area.y1;

    for(h = 0; h < blend_h; ++h) {
        lv_draw_sw_mask_res_t mask_res;

        lv_memset(mask_buf, 0xFF, (size_t)blend_w);
        mask_res = lv_draw_sw_mask_apply(mask_list, mask_buf, blend_area.x1, blend_area.y1, blend_w);
        if(mask_res != LV_DRAW_SW_MASK_RES_TRANSP &&
           !blend_solid_mask_row(layer,
                                 blend_area.x1,
                                 blend_area.y1,
                                 blend_w,
                                 draw_dsc->color,
                                 draw_dsc->opa,
                                 mask_buf,
                                 mask_res)) {
            handled = false;
            goto cleanup;
        }

        blend_area.y1++;
        blend_area.y2++;
    }

    handled = true;

cleanup:
    lv_draw_sw_mask_free_param(&mask_angle_param);
    lv_draw_sw_mask_free_param(&mask_out_param);
    if(mask_in_param_valid) {
        lv_draw_sw_mask_free_param(&mask_in_param);
    }
    if(mask_buf != NULL) {
        lv_free(mask_buf);
    }

    return handled;
}

static bool execute_label_glyph_mask(lv_draw_unit_t * draw_unit,
                                     const lv_draw_glyph_dsc_t * glyph_draw_dsc)
{
    lv_layer_t * layer;
    const lv_draw_buf_t * draw_buf;
    lv_area_t blend_area;
    int32_t blend_w;
    int32_t blend_h;
    int32_t y;

    if(draw_unit == NULL || glyph_draw_dsc == NULL || glyph_draw_dsc->glyph_data == NULL ||
       glyph_draw_dsc->letter_coords == NULL) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    draw_buf = (const lv_draw_buf_t *)glyph_draw_dsc->glyph_data;
    if(draw_buf->data == NULL || draw_buf->header.stride == 0U) {
        return false;
    }

    if(!lv_area_intersect(&blend_area, glyph_draw_dsc->letter_coords, draw_unit->clip_area)) {
        return true;
    }

    blend_w = lv_area_get_width(&blend_area);
    blend_h = lv_area_get_height(&blend_area);

    for(y = 0; y < blend_h; ++y) {
        const int32_t abs_y = blend_area.y1 + y;
        const int32_t mask_y = abs_y - glyph_draw_dsc->letter_coords->y1;
        const int32_t mask_x = blend_area.x1 - glyph_draw_dsc->letter_coords->x1;
        const lv_opa_t * mask_row =
            (const lv_opa_t *)draw_buf->data +
            ((size_t)mask_y * draw_buf->header.stride) + (size_t)mask_x;

        if(!blend_solid_mask_row(layer,
                                 blend_area.x1,
                                 abs_y,
                                 blend_w,
                                 glyph_draw_dsc->color,
                                 glyph_draw_dsc->opa,
                                 mask_row,
                                 LV_DRAW_SW_MASK_RES_CHANGED)) {
            return false;
        }
    }

    g_state.stats.label_fast_glyph_count++;
    return true;
}

static void label_draw_letter_cb(lv_draw_unit_t * draw_unit,
                                 lv_draw_glyph_dsc_t * glyph_draw_dsc,
                                 lv_draw_fill_dsc_t * fill_draw_dsc,
                                 const lv_area_t * fill_area)
{
    if(glyph_draw_dsc != NULL) {
        switch(glyph_draw_dsc->format) {
            case LV_FONT_GLYPH_FORMAT_NONE: {
#if LV_USE_FONT_PLACEHOLDER
                    lv_draw_border_dsc_t border_draw_dsc;
                    lv_draw_border_dsc_init(&border_draw_dsc);
                    border_draw_dsc.opa = glyph_draw_dsc->opa;
                    border_draw_dsc.color = glyph_draw_dsc->color;
                    border_draw_dsc.width = 1;
                    if(!execute_border(draw_unit, &border_draw_dsc, glyph_draw_dsc->bg_coords)) {
                        lv_draw_sw_border(draw_unit, &border_draw_dsc, glyph_draw_dsc->bg_coords);
                    }
#endif
                }
                break;

            case LV_FONT_GLYPH_FORMAT_A1:
            case LV_FONT_GLYPH_FORMAT_A2:
            case LV_FONT_GLYPH_FORMAT_A4:
            case LV_FONT_GLYPH_FORMAT_A8:
                if(!execute_label_glyph_mask(draw_unit, glyph_draw_dsc)) {
                    lv_area_t mask_area = *glyph_draw_dsc->letter_coords;
                    lv_draw_sw_blend_dsc_t blend_dsc;
                    lv_draw_buf_t * draw_buf = glyph_draw_dsc->glyph_data;

                    mask_area.x2 = mask_area.x1 +
                                   lv_draw_buf_width_to_stride(lv_area_get_width(&mask_area),
                                                               LV_COLOR_FORMAT_A8) - 1;
                    lv_memzero(&blend_dsc, sizeof(blend_dsc));
                    blend_dsc.color = glyph_draw_dsc->color;
                    blend_dsc.opa = glyph_draw_dsc->opa;
                    blend_dsc.mask_buf = draw_buf->data;
                    blend_dsc.mask_area = &mask_area;
                    blend_dsc.mask_stride = draw_buf->header.stride;
                    blend_dsc.blend_area = glyph_draw_dsc->letter_coords;
                    blend_dsc.mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
                    g_state.stats.label_sw_glyph_fallback_count++;
                    lv_draw_sw_blend(draw_unit, &blend_dsc);
                }
                break;

            case LV_FONT_GLYPH_FORMAT_IMAGE: {
#if LV_USE_IMGFONT
                    lv_draw_image_dsc_t img_dsc;
                    lv_draw_image_dsc_init(&img_dsc);
                    img_dsc.rotation = 0;
                    img_dsc.scale_x = LV_SCALE_NONE;
                    img_dsc.scale_y = LV_SCALE_NONE;
                    img_dsc.opa = glyph_draw_dsc->opa;
                    img_dsc.src = glyph_draw_dsc->glyph_data;
                    lv_draw_sw_image(draw_unit, &img_dsc, glyph_draw_dsc->letter_coords);
#endif
                }
                break;

            default:
                break;
        }
    }

    if(fill_draw_dsc != NULL && fill_area != NULL) {
        execute_fill(draw_unit, fill_draw_dsc, fill_area);
    }
}

static bool execute_label(lv_draw_unit_t * draw_unit,
                          const lv_draw_label_dsc_t * draw_dsc,
                          const lv_area_t * coords)
{
    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL) {
        return false;
    }

    if(draw_dsc->opa <= LV_OPA_MIN) {
        return true;
    }

    lv_draw_label_iterate_characters(draw_unit, draw_dsc, coords, label_draw_letter_cb);
    return true;
}

static bool execute_border(lv_draw_unit_t * draw_unit,
                           const lv_draw_border_dsc_t * draw_dsc,
                           const lv_area_t * coords)
{
    lv_layer_t * layer;
    const lv_area_t * original_clip_area;
    lv_draw_buf_t * draw_buf;
    hmi_nexus_d211_ge2d_border_geometry_t geometry;
    hmi_nexus_d211_ge2d_buffer_entry_t * dest_entry = NULL;
    struct ge_fillrect fill;
    lv_area_t corner;
    bool submitted = false;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL) {
        return false;
    }

    draw_buf = layer->draw_buf;
    original_clip_area = draw_unit->clip_area;
    if(!compute_border_geometry(coords, draw_dsc, &geometry)) {
        return false;
    }

    if(!setup_fill_request(draw_buf, draw_dsc->color, draw_dsc->opa, -1, &fill, &dest_entry)) {
        return false;
    }

    sync_for_hw(dest_entry);
    if(begin_dma_access(dest_entry) < 0) {
        g_state.stats.hw_submit_fail_count++;
        return false;
    }

    if(!submit_border_rects(&fill, &geometry, layer, original_clip_area, &submitted)) {
        g_state.stats.hw_submit_fail_count++;
        end_dma_access(dest_entry);
        sync_for_cpu(dest_entry);
        return false;
    }

    if(submitted) {
        if(mpp_ge_emit(g_state.ge) != 0 || mpp_ge_sync(g_state.ge) != 0) {
            g_state.stats.hw_submit_fail_count++;
            end_dma_access(dest_entry);
            sync_for_cpu(dest_entry);
            return false;
        }
    }

    end_dma_access(dest_entry);
    sync_for_cpu(dest_entry);
    draw_unit->clip_area = original_clip_area;

    if(!submitted) {
        return false;
    }

    if(geometry.rounded) {
        if(geometry.core_area.x1 > geometry.core_area.x2 ||
           geometry.core_area.y1 > geometry.core_area.y2) {
            return false;
        }

        lv_area_set(&corner,
                    geometry.outer_area.x1,
                    geometry.outer_area.y1,
                    geometry.core_area.x1 - 1,
                    geometry.core_area.y1 - 1);
        sw_border_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);

        lv_area_set(&corner,
                    geometry.core_area.x2 + 1,
                    geometry.outer_area.y1,
                    geometry.outer_area.x2,
                    geometry.core_area.y1 - 1);
        sw_border_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);

        lv_area_set(&corner,
                    geometry.outer_area.x1,
                    geometry.core_area.y2 + 1,
                    geometry.core_area.x1 - 1,
                    geometry.outer_area.y2);
        sw_border_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);

        lv_area_set(&corner,
                    geometry.core_area.x2 + 1,
                    geometry.core_area.y2 + 1,
                    geometry.outer_area.x2,
                    geometry.outer_area.y2);
        sw_border_clip(draw_unit, draw_dsc, coords, original_clip_area, &corner);
    }

    return true;
}

static lv_opa_t shadow_ring_opa(const lv_draw_box_shadow_dsc_t * draw_dsc, int32_t ring_index)
{
    int32_t distance_to_core;

    if(draw_dsc == NULL || draw_dsc->width <= 0) {
        return LV_OPA_TRANSP;
    }

    distance_to_core = draw_dsc->width - ring_index;
    return (lv_opa_t)LV_CLAMP(0,
                              (int32_t)((draw_dsc->opa * distance_to_core) /
                                        draw_dsc->width),
                              LV_OPA_MAX);
}

static bool execute_box_shadow(lv_draw_unit_t * draw_unit,
                               const lv_draw_box_shadow_dsc_t * draw_dsc,
                               const lv_area_t * coords)
{
    lv_area_t core_area;
    int32_t shadow_radius;
    int32_t ring;
    bool used_hw = false;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL || !draw_dsc->bg_cover) {
        return false;
    }

    core_area.x1 = coords->x1 + draw_dsc->ofs_x - draw_dsc->spread;
    core_area.x2 = coords->x2 + draw_dsc->ofs_x + draw_dsc->spread;
    core_area.y1 = coords->y1 + draw_dsc->ofs_y - draw_dsc->spread;
    core_area.y2 = coords->y2 + draw_dsc->ofs_y + draw_dsc->spread;
    if(core_area.x1 > core_area.x2 || core_area.y1 > core_area.y2) {
        return false;
    }

    shadow_radius = clamp_fill_radius_for_area(&core_area, draw_dsc->radius);

    for(ring = 0; ring < draw_dsc->width; ++ring) {
        lv_draw_border_dsc_t ring_dsc;
        lv_area_t ring_area = core_area;
        lv_opa_t ring_opa = shadow_ring_opa(draw_dsc, ring);

        if(ring_opa <= LV_OPA_MIN) {
            continue;
        }

        lv_area_increase(&ring_area, ring + 1, ring + 1);
        lv_draw_border_dsc_init(&ring_dsc);
        ring_dsc.base = draw_dsc->base;
        ring_dsc.base.dsc_size = sizeof(ring_dsc);
        ring_dsc.radius = shadow_radius + ring + 1;
        ring_dsc.color = draw_dsc->color;
        ring_dsc.width = 1;
        ring_dsc.opa = ring_opa;
        ring_dsc.side = LV_BORDER_SIDE_FULL;

        if(execute_border(draw_unit, &ring_dsc, &ring_area)) {
            used_hw = true;
        }
    }

    return used_hw;
}

static void image_draw_core(lv_draw_unit_t * draw_unit,
                            const lv_draw_image_dsc_t * draw_dsc,
                            const lv_image_decoder_dsc_t * decoder_dsc,
                            lv_draw_image_sup_t * sup,
                            const lv_area_t * img_coords,
                            const lv_area_t * clipped_img_area)
{
    const lv_draw_buf_t * decoded_draw_buf;

    LV_UNUSED(sup);

    if(decoder_dsc == NULL || decoder_dsc->decoded == NULL) {
        g_state.stats.image_sw_fallback_count++;
        lv_draw_sw_image(draw_unit, draw_dsc, img_coords);
        return;
    }

    decoded_draw_buf = ensure_hw_source_draw_buf(draw_dsc->src, decoder_dsc->decoded);
    if(!source_draw_buf_supported(decoded_draw_buf) ||
       !execute_image_transfer(draw_unit,
                               draw_dsc,
                               decoded_draw_buf,
                               img_coords,
                               clipped_img_area)) {
        g_state.stats.image_sw_fallback_count++;
        lv_draw_sw_image(draw_unit, draw_dsc, img_coords);
        return;
    }

    g_state.stats.image_hw_count++;
}

static bool execute_fake_image_fill(lv_draw_unit_t * draw_unit,
                                    const lv_draw_image_dsc_t * draw_dsc,
                                    const lv_area_t * coords)
{
    uint16_t width;
    uint16_t height;
    bool blend;
    uint32_t color_value;
    lv_draw_fill_dsc_t fill_dsc;
    lv_area_t transformed_area;

    if(draw_unit == NULL || draw_dsc == NULL || coords == NULL || draw_dsc->src == NULL ||
       !hmi_nexus_d211_parse_fake_image_path((const char *)draw_dsc->src,
                                             &width,
                                             &height,
                                             &blend,
                                             &color_value)) {
        return false;
    }

    LV_UNUSED(width);
    LV_UNUSED(height);

    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = lv_color_hex(color_value & 0x00ffffffU);
    fill_dsc.opa = (lv_opa_t)((color_value >> 24) & 0xffU);

    lv_image_buf_get_transformed_area(&transformed_area,
                                      lv_area_get_width(coords),
                                      lv_area_get_height(coords),
                                      draw_dsc->rotation,
                                      draw_dsc->scale_x,
                                      draw_dsc->scale_y,
                                      &draw_dsc->pivot);
    lv_area_move(&transformed_area, coords->x1, coords->y1);

    if(!execute_fill_core(draw_unit, &fill_dsc, &transformed_area, blend ? 1 : 0, false)) {
        lv_draw_sw_fill(draw_unit, &fill_dsc, &transformed_area);
        g_state.stats.image_sw_fallback_count++;
        return true;
    }

    g_state.stats.image_hw_count++;
    return true;
}

static void execute_task(hmi_nexus_d211_ge2d_unit_t * unit)
{
    lv_draw_task_t * task;

    task = unit->task_act;
    if(task == NULL) {
        return;
    }

    if(task->type == LV_DRAW_TASK_TYPE_FILL) {
        execute_fill(&unit->base_unit, (const lv_draw_fill_dsc_t *)task->draw_dsc, &task->area);
    }
    else if(task->type == LV_DRAW_TASK_TYPE_BORDER) {
        g_state.stats.border_sw_fallback_count++;
        lv_draw_sw_border(&unit->base_unit,
                          (const lv_draw_border_dsc_t *)task->draw_dsc,
                          &task->area);
    }
    else if(task->type == LV_DRAW_TASK_TYPE_BOX_SHADOW) {
        g_state.stats.box_shadow_sw_fallback_count++;
        lv_draw_sw_box_shadow(&unit->base_unit,
                              (const lv_draw_box_shadow_dsc_t *)task->draw_dsc,
                              &task->area);
    }
    else if(task->type == LV_DRAW_TASK_TYPE_LABEL) {
        g_state.stats.label_task_sw_fallback_count++;
        lv_draw_sw_label(&unit->base_unit,
                         (const lv_draw_label_dsc_t *)task->draw_dsc,
                         &task->area);
    }
    else if(task->type == LV_DRAW_TASK_TYPE_IMAGE) {
        const lv_draw_image_dsc_t * draw_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
        if(is_fake_image_source(draw_dsc)) {
            if(!execute_fake_image_fill(&unit->base_unit, draw_dsc, &task->area)) {
                g_state.stats.image_sw_fallback_count++;
                lv_draw_sw_image(&unit->base_unit, draw_dsc, &task->area);
            }
        }
        else if(draw_dsc->tile) {
            lv_draw_image_tiled_helper(&unit->base_unit, draw_dsc, &task->area, image_draw_core);
        }
        else {
            lv_draw_image_normal_helper(&unit->base_unit, draw_dsc, &task->area, image_draw_core);
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_LAYER) {
        const lv_draw_image_dsc_t * draw_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
        const lv_layer_t * source_layer = (const lv_layer_t *)draw_dsc->src;
        lv_draw_image_dsc_t layer_draw_dsc;
        lv_area_t clipped_area;

        if(source_layer != NULL &&
           source_layer->draw_buf != NULL &&
           lv_area_intersect(&clipped_area, &task->area, unit->base_unit.clip_area)) {
            layer_draw_dsc = *draw_dsc;
            layer_draw_dsc.src = source_layer->draw_buf;
            layer_draw_dsc.header = source_layer->draw_buf->header;
            if(!execute_image_transfer(&unit->base_unit,
                                       &layer_draw_dsc,
                                       source_layer->draw_buf,
                                       &task->area,
                                       &clipped_area)) {
                g_state.stats.layer_sw_fallback_count++;
                lv_draw_sw_layer(&unit->base_unit, draw_dsc, &task->area);
            }
            else {
                g_state.stats.layer_hw_count++;
            }
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_ARC) {
        lv_draw_sw_arc(&unit->base_unit,
                       (const lv_draw_arc_dsc_t *)task->draw_dsc,
                       &task->area);
    }

    task->state = LV_DRAW_TASK_STATE_READY;
    unit->task_act = NULL;
    lv_draw_dispatch_request();
}

static int32_t ge2d_dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer)
{
    hmi_nexus_d211_ge2d_unit_t * unit = (hmi_nexus_d211_ge2d_unit_t *)draw_unit;
    lv_draw_task_t * task;

    if(unit->task_act != NULL) {
        return 0;
    }

    task = lv_draw_get_next_available_task(layer, NULL, HMI_NEXUS_D211_DRAW_UNIT_ID);
    if(task == NULL || task->preferred_draw_unit_id != HMI_NEXUS_D211_DRAW_UNIT_ID) {
        return LV_DRAW_UNIT_IDLE;
    }

    if(lv_draw_layer_alloc_buf(layer) == NULL) {
        return LV_DRAW_UNIT_IDLE;
    }

    task->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    unit->base_unit.target_layer = layer;
    unit->base_unit.clip_area = &task->clip_area;
    unit->task_act = task;

    execute_task(unit);
    return 1;
}

static int32_t ge2d_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)
{
    bool supported = false;

    LV_UNUSED(draw_unit);

    if(task == NULL || g_state.ge == NULL) {
        return 0;
    }

    if(task->type == LV_DRAW_TASK_TYPE_FILL) {
        supported = fill_task_supported(task);
        if(supported) {
            g_state.stats.fill_accept_count++;
            if(task->preference_score > 70) {
                task->preference_score = 70;
                task->preferred_draw_unit_id = HMI_NEXUS_D211_DRAW_UNIT_ID;
            }
        }
        else {
            g_state.stats.fill_reject_count++;
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_BORDER) {
        supported = border_task_supported(task);
        if(supported) {
            g_state.stats.border_accept_count++;
            if(task->preference_score > 70) {
                task->preference_score = 70;
                task->preferred_draw_unit_id = HMI_NEXUS_D211_DRAW_UNIT_ID;
            }
        }
        else {
            g_state.stats.border_reject_count++;
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_BOX_SHADOW) {
        supported = box_shadow_task_supported(task);
        if(supported) {
            g_state.stats.box_shadow_accept_count++;
            if(task->preference_score > 70) {
                task->preference_score = 70;
                task->preferred_draw_unit_id = HMI_NEXUS_D211_DRAW_UNIT_ID;
            }
        }
        else {
            g_state.stats.box_shadow_reject_count++;
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_IMAGE) {
        supported = image_task_supported(task);
        if(supported) {
            g_state.stats.image_accept_count++;
            if(task->preference_score > 70) {
                task->preference_score = 70;
                task->preferred_draw_unit_id = HMI_NEXUS_D211_DRAW_UNIT_ID;
            }
        }
        else {
            g_state.stats.image_reject_count++;
        }
    }
    else if(task->type == LV_DRAW_TASK_TYPE_LAYER) {
        supported = layer_task_supported(task);
        if(supported) {
            g_state.stats.layer_accept_count++;
            if(task->preference_score > 70) {
                task->preference_score = 70;
                task->preferred_draw_unit_id = HMI_NEXUS_D211_DRAW_UNIT_ID;
            }
        }
        else {
            g_state.stats.layer_reject_count++;
        }
    }
    /* Keep the task matrix aligned with the vendor LVGL v9 GE2D port.
     * Non-official paths such as label/arc stay on the stock LVGL
     * software renderer instead of being scheduled onto this draw unit. */

    return 0;
}

void hmi_nexus_d211_lvgl_ge2d_init(void)
{
    hmi_nexus_d211_ge2d_unit_t * unit;
    lv_draw_buf_handlers_t * handlers;

    if(g_state.initialized) {
        return;
    }

    g_state.ge = mpp_ge_open();
    if(g_state.ge == NULL) {
        return;
    }

    g_state.dma_heap_fd = dmabuf_device_open();

    handlers = lv_draw_buf_get_handlers();
    g_state.original_handlers = *handlers;
    g_state.image_cache_handlers = *handlers;
    g_state.image_cache_handlers.buf_malloc_cb = draw_buf_malloc;
    g_state.image_cache_handlers.buf_free_cb = draw_buf_free;
    g_state.image_cache_handlers.align_pointer_cb = draw_buf_align;
    g_state.image_cache_handlers.invalidate_cache_cb = draw_buf_invalidate_cache;
#if HMI_NEXUS_LVGL_HAS_DRAW_BUF_FLUSH_CACHE_CB
    g_state.image_cache_handlers.flush_cache_cb = draw_buf_flush_cache;
#endif
    g_state.image_cache_handlers.width_to_stride_cb = draw_buf_width_to_stride;
    g_state.handlers_installed = true;
    hmi_nexus_lvgl_install_image_cache_draw_buf_handlers(&g_state.image_cache_handlers);

    unit = lv_draw_create_unit(sizeof(*unit));
    if(unit == NULL) {
        return;
    }

    unit->base_unit.dispatch_cb = ge2d_dispatch;
    unit->base_unit.evaluate_cb = ge2d_evaluate;
    g_state.initialized = true;
}

int hmi_nexus_d211_lvgl_ge2d_ready(void)
{
    return g_state.initialized ? 1 : 0;
}

void hmi_nexus_d211_lvgl_ge2d_get_stats(hmi_nexus_d211_lvgl_ge2d_stats_t * stats)
{
    if(stats == NULL) {
        return;
    }

    *stats = g_state.stats;
}

void hmi_nexus_d211_lvgl_ge2d_register_external_buffer(
    const hmi_nexus_d211_lvgl_buffer_desc_t * desc)
{
    if(!g_state.initialized || desc == NULL) {
        return;
    }

    if(desc->memory_type == HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_HOST) {
        remove_buffer_entry(desc->data);
        return;
    }

    upsert_buffer_entry(desc, false);
}

void hmi_nexus_d211_lvgl_ge2d_unregister_external_buffer(uint8_t * data)
{
    hmi_nexus_d211_ge2d_buffer_entry_t * entry;

    if(data == NULL) {
        return;
    }

    entry = find_buffer_entry(data);
    if(entry == NULL || entry->owned) {
        return;
    }

    remove_buffer_entry(data);
}

#else

void hmi_nexus_d211_lvgl_ge2d_init(void)
{
}

int hmi_nexus_d211_lvgl_ge2d_ready(void)
{
    return 0;
}

void hmi_nexus_d211_lvgl_ge2d_get_stats(hmi_nexus_d211_lvgl_ge2d_stats_t * stats)
{
    if(stats == NULL) {
        return;
    }

    *stats = (hmi_nexus_d211_lvgl_ge2d_stats_t){0};
}

void hmi_nexus_d211_lvgl_ge2d_register_external_buffer(
    const hmi_nexus_d211_lvgl_buffer_desc_t * desc)
{
    (void)desc;
}

void hmi_nexus_d211_lvgl_ge2d_unregister_external_buffer(uint8_t * data)
{
    (void)data;
}

#endif
