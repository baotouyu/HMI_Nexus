#include "source/src/ui/lvgl/f133/f133_lvgl_g2d.h"

#if HMI_NEXUS_HAS_SUNXI_G2D && HMI_NEXUS_HAS_LVGL

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <g2d_driver_enh.h>
#include <ion_mem_alloc.h>
#include "source/src/ui/lvgl/lvgl_private_compat.h"

#define HMI_NEXUS_F133_DRAW_UNIT_ID 11
#define HMI_NEXUS_F133_G2D_IMAGE_CACHE_LIMIT 8U

typedef struct hmi_nexus_f133_g2d_buffer_entry_t {
    void * data;
    size_t size;
    size_t stride;
    int dma_fd;
    uintptr_t physical_address;
    hmi_nexus_f133_lvgl_buffer_memory_type_t memory_type;
    bool owned;
    struct hmi_nexus_f133_g2d_buffer_entry_t * next;
} hmi_nexus_f133_g2d_buffer_entry_t;

typedef struct hmi_nexus_f133_g2d_image_cache_entry_t {
    const void * source_key;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    lv_color_format_t color_format;
    lv_draw_buf_t * dma_draw_buf;
    struct hmi_nexus_f133_g2d_image_cache_entry_t * next;
} hmi_nexus_f133_g2d_image_cache_entry_t;

typedef struct hmi_nexus_f133_g2d_unit_t {
    lv_draw_unit_t base_unit;
    lv_draw_task_t * task_act;
} hmi_nexus_f133_g2d_unit_t;

typedef struct hmi_nexus_f133_g2d_state_t {
    bool initialized;
    int g2d_fd;
    struct SunxiMemOpsS * memops;
    lv_draw_buf_handlers_t original_handlers;
    lv_draw_buf_handlers_t image_cache_handlers;
    hmi_nexus_f133_g2d_buffer_entry_t * buffers;
    hmi_nexus_f133_g2d_image_cache_entry_t * image_cache;
    hmi_nexus_f133_lvgl_g2d_stats_t stats;
} hmi_nexus_f133_g2d_state_t;

static hmi_nexus_f133_g2d_state_t g_state = {
    .initialized = false,
    .g2d_fd = -1,
    .memops = NULL,
    .buffers = NULL,
};

static hmi_nexus_f133_g2d_buffer_entry_t * find_buffer_entry(const void * data)
{
    hmi_nexus_f133_g2d_buffer_entry_t * entry = g_state.buffers;

    while(entry != NULL) {
        if(entry->data == data) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

static uint64_t count_buffer_entries(void)
{
    uint64_t count = 0;
    hmi_nexus_f133_g2d_buffer_entry_t * entry = g_state.buffers;

    while(entry != NULL) {
        count++;
        entry = entry->next;
    }

    return count;
}

static bool buffer_memory_is_dma_capable(const hmi_nexus_f133_g2d_buffer_entry_t * entry)
{
    uintptr_t resolved_physical_address;

    if(entry == NULL) {
        return false;
    }

    if(entry->memory_type == HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_DMABUF) {
        return entry->dma_fd >= 0;
    }

    if(entry->memory_type == HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_PHYSICAL) {
        if(entry->physical_address == 0 || g_state.memops == NULL || entry->data == NULL) {
            return false;
        }

        resolved_physical_address = (uintptr_t)SunxiMemGetPhysicAddressCpu(g_state.memops, entry->data);
        return resolved_physical_address != 0 &&
               resolved_physical_address == entry->physical_address;
    }

    return false;
}

static bool is_draw_buf_supported(lv_color_format_t color_format)
{
    switch(color_format) {
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB888:
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_XRGB8888:
            return true;
        default:
            break;
    }

    return false;
}

static uint32_t draw_buf_pitch_pixels(const lv_draw_buf_t * draw_buf)
{
    uint8_t bytes_per_pixel;

    if(draw_buf == NULL || draw_buf->header.stride == 0U) {
        return 0;
    }

    bytes_per_pixel = lv_color_format_get_size(draw_buf->header.cf);
    if(bytes_per_pixel == 0U || (draw_buf->header.stride % bytes_per_pixel) != 0U) {
        return 0;
    }

    return draw_buf->header.stride / bytes_per_pixel;
}

static g2d_fmt_enh to_sunxi_pixel_format(lv_color_format_t color_format)
{
    switch(color_format) {
        case LV_COLOR_FORMAT_RGB565:
            return G2D_FORMAT_RGB565;
        case LV_COLOR_FORMAT_RGB888:
            return G2D_FORMAT_RGB888;
        case LV_COLOR_FORMAT_ARGB8888:
            return G2D_FORMAT_ARGB8888;
        case LV_COLOR_FORMAT_XRGB8888:
            return G2D_FORMAT_XRGB8888;
        default:
            break;
    }

    return G2D_FORMAT_MAX;
}

static uint32_t to_sunxi_fill_color(lv_color_format_t color_format, lv_color_t color, lv_opa_t opa)
{
    uint32_t color32;

    switch(color_format) {
        case LV_COLOR_FORMAT_RGB565:
            return lv_color_to_u16(color);
        case LV_COLOR_FORMAT_RGB888:
            return ((uint32_t)color.red << 16) |
                   ((uint32_t)color.green << 8) |
                   (uint32_t)color.blue;
        case LV_COLOR_FORMAT_XRGB8888:
            return ((uint32_t)color.red << 16) |
                   ((uint32_t)color.green << 8) |
                   (uint32_t)color.blue;
        case LV_COLOR_FORMAT_ARGB8888:
            color32 = lv_color_to_u32(color);
            if(opa < LV_OPA_MAX) {
                color32 = (color32 & 0x00ffffffU) | ((uint32_t)opa << 24);
            }
            return color32;
        default:
            break;
    }

    return 0;
}

static void release_owned_buffer(hmi_nexus_f133_g2d_buffer_entry_t * entry)
{
    if(entry == NULL || !entry->owned || g_state.memops == NULL || entry->data == NULL) {
        return;
    }

    if(entry->memory_type == HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_DMABUF ||
       entry->memory_type == HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_PHYSICAL) {
        SunxiMemPfree(g_state.memops, entry->data);
    }
}

static void remove_buffer_entry(void * data)
{
    hmi_nexus_f133_g2d_buffer_entry_t ** prev = &g_state.buffers;

    while(*prev != NULL) {
        if((*prev)->data == data) {
            hmi_nexus_f133_g2d_buffer_entry_t * entry = *prev;
            *prev = entry->next;
            release_owned_buffer(entry);
            free(entry);
            g_state.stats.tracked_buffer_count = count_buffer_entries();
            return;
        }
        prev = &(*prev)->next;
    }
}

static void upsert_buffer_entry(const hmi_nexus_f133_lvgl_buffer_desc_t * desc, bool owned)
{
    hmi_nexus_f133_g2d_buffer_entry_t * entry;

    if(desc == NULL || desc->data == NULL) {
        return;
    }

    entry = find_buffer_entry(desc->data);
    if(entry == NULL) {
        entry = (hmi_nexus_f133_g2d_buffer_entry_t *)calloc(1, sizeof(*entry));
        if(entry == NULL) {
            return;
        }
        entry->next = g_state.buffers;
        g_state.buffers = entry;
    }

    entry->data = desc->data;
    entry->size = desc->size;
    entry->stride = desc->stride;
    entry->dma_fd = desc->dma_fd;
    entry->physical_address = desc->physical_address;
    entry->memory_type = desc->memory_type;
    entry->owned = owned;
    g_state.stats.tracked_buffer_count = count_buffer_entries();
}

static void flush_buffer_cache(const hmi_nexus_f133_g2d_buffer_entry_t * entry)
{
    uint32_t buffer_size;

    if(g_state.memops == NULL || entry == NULL || entry->data == NULL || entry->size == 0U) {
        return;
    }

    buffer_size = entry->size > (size_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)entry->size;
    SunxiMemFlushCache(g_state.memops, entry->data, (int)buffer_size);
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
    void * allocation;
    uintptr_t physical_address;
    int share_fd;
    hmi_nexus_f133_lvgl_buffer_desc_t desc;

    if(g_state.memops != NULL && is_draw_buf_supported(color_format)) {
        allocation = SunxiMemPalloc(g_state.memops, (int)size);
        if(allocation != NULL) {
            physical_address = (uintptr_t)SunxiMemGetPhysicAddressCpu(g_state.memops, allocation);
            if(physical_address != 0U) {
                memset(&desc, 0, sizeof(desc));
                share_fd = SunxiMemGetBufferFd(g_state.memops, allocation);
                desc.data = allocation;
                desc.size = size;
                desc.dma_fd = share_fd;
                desc.physical_address = physical_address;
                desc.memory_type = share_fd >= 0 ? HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_DMABUF
                                                 : HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_PHYSICAL;
                upsert_buffer_entry(&desc, true);
                return allocation;
            }

            SunxiMemPfree(g_state.memops, allocation);
        }
    }

    if(g_state.original_handlers.buf_malloc_cb != NULL) {
        return g_state.original_handlers.buf_malloc_cb(size, color_format);
    }

    return NULL;
}

static void draw_buf_free(void * buf)
{
    hmi_nexus_f133_g2d_buffer_entry_t * entry = find_buffer_entry(buf);

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
    if(g_state.original_handlers.invalidate_cache_cb != NULL) {
        g_state.original_handlers.invalidate_cache_cb(draw_buf, area);
    }
}

#if HMI_NEXUS_LVGL_HAS_DRAW_BUF_FLUSH_CACHE_CB
static void draw_buf_flush_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    hmi_nexus_f133_g2d_buffer_entry_t * entry;

    LV_UNUSED(area);

    if(draw_buf == NULL) {
        return;
    }

    entry = find_buffer_entry(draw_buf->data);
    if(entry != NULL) {
        flush_buffer_cache(entry);
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

static hmi_nexus_f133_g2d_image_cache_entry_t * find_image_cache_entry(const void * source_key,
                                                                       const lv_draw_buf_t * draw_buf)
{
    hmi_nexus_f133_g2d_image_cache_entry_t * entry = g_state.image_cache;

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

static void destroy_image_cache_entry(hmi_nexus_f133_g2d_image_cache_entry_t * entry)
{
    if(entry == NULL) {
        return;
    }

    if(entry->dma_draw_buf != NULL) {
        lv_draw_buf_destroy(entry->dma_draw_buf);
    }

    free(entry);
}

static void trim_image_cache(void)
{
    hmi_nexus_f133_g2d_image_cache_entry_t * entry = g_state.image_cache;
    hmi_nexus_f133_g2d_image_cache_entry_t * prev = NULL;
    uint32_t count = 0U;

    while(entry != NULL) {
        ++count;
        if(count > HMI_NEXUS_F133_G2D_IMAGE_CACHE_LIMIT) {
            hmi_nexus_f133_g2d_image_cache_entry_t * stale = entry;
            if(prev != NULL) {
                prev->next = NULL;
            }
            while(stale != NULL) {
                hmi_nexus_f133_g2d_image_cache_entry_t * next = stale->next;
                destroy_image_cache_entry(stale);
                stale = next;
            }
            return;
        }

        prev = entry;
        entry = entry->next;
    }
}

static bool destination_layer_supported(const lv_draw_dsc_base_t * base_dsc)
{
    const lv_layer_t * layer;
    const hmi_nexus_f133_g2d_buffer_entry_t * entry;

    if(base_dsc == NULL || base_dsc->layer == NULL) {
        return false;
    }

    layer = base_dsc->layer;
    if(layer->draw_buf == NULL || layer->draw_buf->data == NULL) {
        return false;
    }

    if(!is_draw_buf_supported(layer->color_format)) {
        return false;
    }

    entry = find_buffer_entry(layer->draw_buf->data);
    return buffer_memory_is_dma_capable(entry);
}

static bool fill_task_can_use_hw(const lv_draw_fill_dsc_t * draw_dsc, const lv_area_t * coords)
{
    if(draw_dsc == NULL || coords == NULL) {
        return false;
    }

    if(draw_dsc->grad.dir != LV_GRAD_DIR_NONE) {
        return false;
    }

    if(draw_dsc->radius != 0) {
        return false;
    }

    return lv_area_get_width(coords) > 0 && lv_area_get_height(coords) > 0;
}

static bool fill_task_supported(const lv_draw_task_t * task)
{
    const lv_draw_fill_dsc_t * draw_dsc;

    if(task == NULL || task->draw_dsc == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_fill_dsc_t *)task->draw_dsc;
    return destination_layer_supported(&draw_dsc->base) &&
           fill_task_can_use_hw(draw_dsc, &task->area);
}

static bool image_descriptor_can_use_hw(const lv_draw_image_dsc_t * draw_dsc)
{
    if(draw_dsc == NULL) {
        return false;
    }

    if(draw_dsc->tile) {
        return false;
    }

    if(draw_dsc->rotation != 0 ||
       draw_dsc->scale_x != LV_SCALE_NONE ||
       draw_dsc->scale_y != LV_SCALE_NONE ||
       draw_dsc->skew_x != 0 ||
       draw_dsc->skew_y != 0) {
        return false;
    }

    if(draw_dsc->bitmap_mask_src != NULL ||
       draw_dsc->recolor_opa > LV_OPA_MIN ||
       hmi_nexus_lvgl_draw_image_has_clip_radius(draw_dsc) ||
       draw_dsc->blend_mode != LV_BLEND_MODE_NORMAL) {
        return false;
    }

    /* Keep the F133 image path intentionally narrow, but allow global alpha
     * so the next step can cover common fade/overlay cases without opening up
     * transform, recolor or mask handling yet. */
    return draw_dsc->opa > LV_OPA_MIN;
}

static bool source_draw_buf_supported(const lv_draw_buf_t * draw_buf)
{
    const hmi_nexus_f133_g2d_buffer_entry_t * entry;

    if(draw_buf == NULL || draw_buf->data == NULL) {
        return false;
    }

    if(!is_draw_buf_supported(draw_buf->header.cf)) {
        return false;
    }

    if(draw_buf_pitch_pixels(draw_buf) == 0U) {
        return false;
    }

    entry = find_buffer_entry(draw_buf->data);
    return buffer_memory_is_dma_capable(entry);
}

static lv_draw_buf_t * duplicate_hw_source_draw_buf(const lv_draw_buf_t * draw_buf)
{
    lv_draw_buf_t * dma_draw_buf;

    if(draw_buf == NULL || draw_buf->data == NULL) {
        return NULL;
    }

    if(!is_draw_buf_supported(draw_buf->header.cf) ||
       draw_buf_pitch_pixels(draw_buf) == 0U) {
        return NULL;
    }

    dma_draw_buf = hmi_nexus_lvgl_draw_buf_dup_with_handlers(&g_state.image_cache_handlers, draw_buf);
    if(dma_draw_buf == NULL || !source_draw_buf_supported(dma_draw_buf)) {
        if(dma_draw_buf != NULL) {
            lv_draw_buf_destroy(dma_draw_buf);
        }
        return NULL;
    }

    return dma_draw_buf;
}

static const lv_draw_buf_t * ensure_hw_source_draw_buf(const void * source_key,
                                                       const lv_draw_buf_t * draw_buf)
{
    hmi_nexus_f133_g2d_image_cache_entry_t * cache_entry;
    lv_draw_buf_t * dma_draw_buf;

    if(draw_buf == NULL || draw_buf->data == NULL) {
        return NULL;
    }

    if(source_draw_buf_supported(draw_buf)) {
        return draw_buf;
    }

    if(!is_draw_buf_supported(draw_buf->header.cf)) {
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

    dma_draw_buf = duplicate_hw_source_draw_buf(draw_buf);
    if(dma_draw_buf == NULL || !source_draw_buf_supported(dma_draw_buf)) {
        if(dma_draw_buf != NULL) {
            lv_draw_buf_destroy(dma_draw_buf);
        }
        return NULL;
    }

    cache_entry = (hmi_nexus_f133_g2d_image_cache_entry_t *)calloc(1, sizeof(*cache_entry));
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

static bool image_task_supported(const lv_draw_task_t * task)
{
    const lv_draw_image_dsc_t * draw_dsc;
    lv_image_src_t src_type;

    if(task == NULL || task->draw_dsc == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
    if(!image_descriptor_can_use_hw(draw_dsc)) {
        return false;
    }

    if(!destination_layer_supported(&draw_dsc->base)) {
        return false;
    }

    if(!is_draw_buf_supported(draw_dsc->header.cf) ||
       draw_dsc->header.cf != draw_dsc->base.layer->color_format) {
        return false;
    }

    src_type = lv_image_src_get_type(draw_dsc->src);
    return src_type == LV_IMAGE_SRC_FILE || src_type == LV_IMAGE_SRC_VARIABLE;
}

static bool layer_task_supported(const lv_draw_task_t * task)
{
    const lv_draw_image_dsc_t * draw_dsc;
    const lv_layer_t * source_layer;

    if(task == NULL || task->draw_dsc == NULL) {
        return false;
    }

    draw_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
    source_layer = (const lv_layer_t *)draw_dsc->src;
    if(!image_descriptor_can_use_hw(draw_dsc) ||
       !destination_layer_supported(&draw_dsc->base) ||
       source_layer == NULL ||
       source_layer->draw_buf == NULL) {
        return false;
    }

    if(!is_draw_buf_supported(source_layer->draw_buf->header.cf) ||
       source_layer->draw_buf->header.cf != draw_dsc->base.layer->color_format) {
        return false;
    }

    return draw_buf_pitch_pixels(source_layer->draw_buf) != 0U;
}

static bool execute_fill_hw(hmi_nexus_f133_g2d_unit_t * unit,
                            const lv_draw_fill_dsc_t * draw_dsc,
                            const lv_area_t * coords)
{
    lv_layer_t * layer;
    const hmi_nexus_f133_g2d_buffer_entry_t * entry;
    lv_area_t clipped_area;
    lv_area_t layer_area;
    g2d_fillrect_h info;
    lv_color_format_t color_format;
    uint32_t pitch_pixels;

    if(unit == NULL || draw_dsc == NULL || coords == NULL || g_state.g2d_fd < 0) {
        return false;
    }

    if(!fill_task_can_use_hw(draw_dsc, coords)) {
        return false;
    }

    layer = unit->base_unit.target_layer;
    if(layer == NULL || layer->draw_buf == NULL || layer->draw_buf->data == NULL ||
       unit->base_unit.clip_area == NULL) {
        return false;
    }

    if(!lv_area_intersect(&clipped_area, coords, unit->base_unit.clip_area)) {
        return true;
    }

    if(!lv_area_intersect(&layer_area, &clipped_area, &layer->buf_area)) {
        return true;
    }

    entry = find_buffer_entry(layer->draw_buf->data);
    if(!buffer_memory_is_dma_capable(entry)) {
        return false;
    }

    color_format = layer->draw_buf->header.cf;
    if(!is_draw_buf_supported(color_format)) {
        return false;
    }

    pitch_pixels = draw_buf_pitch_pixels(layer->draw_buf);
    if(pitch_pixels == 0U) {
        return false;
    }

    memset(&info, 0, sizeof(info));
    info.dst_image_h.format = to_sunxi_pixel_format(color_format);
    if(info.dst_image_h.format == G2D_FORMAT_MAX) {
        return false;
    }

    info.dst_image_h.mode = G2D_PIXEL_ALPHA;
    info.dst_image_h.alpha = draw_dsc->opa >= LV_OPA_MAX ? 255 : draw_dsc->opa;
    info.dst_image_h.color = to_sunxi_fill_color(color_format, draw_dsc->color, draw_dsc->opa);
    info.dst_image_h.width = pitch_pixels;
    info.dst_image_h.height = layer->draw_buf->header.h;
    info.dst_image_h.clip_rect.x = layer_area.x1 - layer->buf_area.x1;
    info.dst_image_h.clip_rect.y = layer_area.y1 - layer->buf_area.y1;
    info.dst_image_h.clip_rect.w = lv_area_get_width(&layer_area);
    info.dst_image_h.clip_rect.h = lv_area_get_height(&layer_area);
    info.dst_image_h.align[0] = 0;
    info.dst_image_h.align[1] = 0;
    info.dst_image_h.align[2] = 0;
    info.dst_image_h.laddr[0] = (unsigned long)entry->physical_address;
    info.dst_image_h.use_phy_addr = 1;
    info.dst_image_h.fd = entry->dma_fd;

    flush_buffer_cache(entry);

    if(ioctl(g_state.g2d_fd, G2D_CMD_FILLRECT_H, (unsigned long)(&info)) < 0) {
        g_state.stats.hw_submit_fail_count++;
        return false;
    }

    flush_buffer_cache(entry);

    g_state.stats.fill_hw_count++;
    return true;
}

static bool compute_image_source_crop(const lv_draw_buf_t * source_draw_buf,
                                      const lv_area_t * img_coords,
                                      const lv_area_t * clipped_img_area,
                                      lv_area_t * source_crop)
{
    if(source_draw_buf == NULL || img_coords == NULL || clipped_img_area == NULL ||
       source_crop == NULL) {
        return false;
    }

    if(clipped_img_area->x1 < img_coords->x1 ||
       clipped_img_area->y1 < img_coords->y1 ||
       clipped_img_area->x2 > img_coords->x2 ||
       clipped_img_area->y2 > img_coords->y2) {
        return false;
    }

    source_crop->x1 = clipped_img_area->x1 - img_coords->x1;
    source_crop->y1 = clipped_img_area->y1 - img_coords->y1;
    source_crop->x2 = clipped_img_area->x2 - img_coords->x1;
    source_crop->y2 = clipped_img_area->y2 - img_coords->y1;

    if(source_crop->x1 < 0 || source_crop->y1 < 0 ||
       source_crop->x2 >= source_draw_buf->header.w ||
       source_crop->y2 >= source_draw_buf->header.h) {
        return false;
    }

    return true;
}

static bool execute_image_blit_hw(lv_draw_unit_t * draw_unit,
                                  const lv_draw_image_dsc_t * draw_dsc,
                                  const lv_draw_buf_t * source_draw_buf,
                                  const lv_area_t * img_coords,
                                  const lv_area_t * clipped_img_area)
{
    lv_layer_t * layer;
    const hmi_nexus_f133_g2d_buffer_entry_t * source_entry;
    const hmi_nexus_f133_g2d_buffer_entry_t * dest_entry;
    lv_area_t task_area;
    lv_area_t dest_area;
    lv_area_t source_crop;
    lv_color_format_t color_format;
    uint32_t source_pitch_pixels;
    uint32_t dest_pitch_pixels;
    bool use_blend;

    if(draw_unit == NULL || draw_dsc == NULL || source_draw_buf == NULL ||
       img_coords == NULL || clipped_img_area == NULL || g_state.g2d_fd < 0) {
        return false;
    }

    if(!image_descriptor_can_use_hw(draw_dsc)) {
        return false;
    }

    layer = draw_unit->target_layer;
    if(layer == NULL || layer->draw_buf == NULL || layer->draw_buf->data == NULL) {
        return false;
    }

    if(draw_unit->clip_area != NULL) {
        if(!lv_area_intersect(&task_area, clipped_img_area, draw_unit->clip_area)) {
            return true;
        }
    }
    else {
        task_area = *clipped_img_area;
    }

    if(!lv_area_intersect(&dest_area, &task_area, &layer->buf_area)) {
        return true;
    }

    if(!compute_image_source_crop(source_draw_buf, img_coords, &dest_area, &source_crop)) {
        return false;
    }

    if(!source_draw_buf_supported(source_draw_buf)) {
        return false;
    }

    dest_entry = find_buffer_entry(layer->draw_buf->data);
    if(!buffer_memory_is_dma_capable(dest_entry)) {
        return false;
    }

    source_entry = find_buffer_entry(source_draw_buf->data);
    if(!buffer_memory_is_dma_capable(source_entry)) {
        return false;
    }

    color_format = source_draw_buf->header.cf;
    if(color_format != layer->draw_buf->header.cf ||
       !is_draw_buf_supported(color_format)) {
        return false;
    }

    source_pitch_pixels = draw_buf_pitch_pixels(source_draw_buf);
    dest_pitch_pixels = draw_buf_pitch_pixels(layer->draw_buf);
    if(source_pitch_pixels == 0U || dest_pitch_pixels == 0U) {
        return false;
    }

    use_blend = draw_dsc->opa < LV_OPA_MAX;

    flush_buffer_cache(source_entry);
    flush_buffer_cache(dest_entry);

    if(use_blend) {
        g2d_bld info;

        memset(&info, 0, sizeof(info));
        info.bld_cmd = G2D_BLD_SRCOVER;

        info.src_image[1].format = to_sunxi_pixel_format(color_format);
        info.src_image[1].mode = G2D_MIXER_ALPHA;
        info.src_image[1].alpha = draw_dsc->opa;
        info.src_image[1].width = source_pitch_pixels;
        info.src_image[1].height = source_draw_buf->header.h;
        info.src_image[1].clip_rect.x = source_crop.x1;
        info.src_image[1].clip_rect.y = source_crop.y1;
        info.src_image[1].clip_rect.w = lv_area_get_width(&source_crop);
        info.src_image[1].clip_rect.h = lv_area_get_height(&source_crop);
        info.src_image[1].align[0] = 0;
        info.src_image[1].align[1] = 0;
        info.src_image[1].align[2] = 0;
        info.src_image[1].laddr[0] = (unsigned long)source_entry->physical_address;
        info.src_image[1].use_phy_addr = 1;
        info.src_image[1].fd = source_entry->dma_fd;

        info.dst_image.format = to_sunxi_pixel_format(color_format);
        info.dst_image.mode = G2D_PIXEL_ALPHA;
        info.dst_image.alpha = 255;
        info.dst_image.width = dest_pitch_pixels;
        info.dst_image.height = layer->draw_buf->header.h;
        info.dst_image.clip_rect.x = dest_area.x1 - layer->buf_area.x1;
        info.dst_image.clip_rect.y = dest_area.y1 - layer->buf_area.y1;
        info.dst_image.clip_rect.w = lv_area_get_width(&dest_area);
        info.dst_image.clip_rect.h = lv_area_get_height(&dest_area);
        info.dst_image.align[0] = 0;
        info.dst_image.align[1] = 0;
        info.dst_image.align[2] = 0;
        info.dst_image.laddr[0] = (unsigned long)dest_entry->physical_address;
        info.dst_image.use_phy_addr = 1;
        info.dst_image.fd = dest_entry->dma_fd;

        /* G2D blend uses src_image[0] as the bottom layer and src_image[1] as
         * the top layer. Reuse the destination as the bottom source to stay
         * aligned with Tina's sunxig2d blend path. */
        info.src_image[0] = info.dst_image;

        if(ioctl(g_state.g2d_fd, G2D_CMD_BLD_H, (unsigned long)(&info)) < 0) {
            g_state.stats.hw_submit_fail_count++;
            return false;
        }
    }
    else {
        g2d_blt_h info;

        memset(&info, 0, sizeof(info));
        info.flag_h = G2D_ROT_0;

        info.src_image_h.format = to_sunxi_pixel_format(color_format);
        info.src_image_h.mode = G2D_PIXEL_ALPHA;
        info.src_image_h.alpha = 255;
        info.src_image_h.width = source_pitch_pixels;
        info.src_image_h.height = source_draw_buf->header.h;
        info.src_image_h.clip_rect.x = source_crop.x1;
        info.src_image_h.clip_rect.y = source_crop.y1;
        info.src_image_h.clip_rect.w = lv_area_get_width(&source_crop);
        info.src_image_h.clip_rect.h = lv_area_get_height(&source_crop);
        info.src_image_h.align[0] = 0;
        info.src_image_h.align[1] = 0;
        info.src_image_h.align[2] = 0;
        info.src_image_h.laddr[0] = (unsigned long)source_entry->physical_address;
        info.src_image_h.use_phy_addr = 1;
        info.src_image_h.fd = source_entry->dma_fd;

        info.dst_image_h.format = to_sunxi_pixel_format(color_format);
        info.dst_image_h.mode = G2D_GLOBAL_ALPHA;
        info.dst_image_h.alpha = 255;
        info.dst_image_h.width = dest_pitch_pixels;
        info.dst_image_h.height = layer->draw_buf->header.h;
        info.dst_image_h.clip_rect.x = dest_area.x1 - layer->buf_area.x1;
        info.dst_image_h.clip_rect.y = dest_area.y1 - layer->buf_area.y1;
        info.dst_image_h.clip_rect.w = lv_area_get_width(&dest_area);
        info.dst_image_h.clip_rect.h = lv_area_get_height(&dest_area);
        info.dst_image_h.align[0] = 0;
        info.dst_image_h.align[1] = 0;
        info.dst_image_h.align[2] = 0;
        info.dst_image_h.laddr[0] = (unsigned long)dest_entry->physical_address;
        info.dst_image_h.use_phy_addr = 1;
        info.dst_image_h.fd = dest_entry->dma_fd;

        if(ioctl(g_state.g2d_fd, G2D_CMD_BITBLT_H, (unsigned long)(&info)) < 0) {
            g_state.stats.hw_submit_fail_count++;
            return false;
        }
    }

    flush_buffer_cache(dest_entry);
    return true;
}

static void image_draw_core(lv_draw_unit_t * draw_unit,
                            const lv_draw_image_dsc_t * draw_dsc,
                            const lv_image_decoder_dsc_t * decoder_dsc,
                            lv_draw_image_sup_t * sup,
                            const lv_area_t * img_coords,
                            const lv_area_t * clipped_img_area)
{
    const lv_draw_buf_t * source_draw_buf;

    LV_UNUSED(sup);

    if(decoder_dsc == NULL || decoder_dsc->decoded == NULL) {
        g_state.stats.image_sw_fallback_count++;
        lv_draw_sw_image(draw_unit, draw_dsc, img_coords);
        return;
    }

    source_draw_buf = ensure_hw_source_draw_buf(draw_dsc->src, decoder_dsc->decoded);
    if(!execute_image_blit_hw(draw_unit,
                              draw_dsc,
                              source_draw_buf,
                              img_coords,
                              clipped_img_area)) {
        g_state.stats.image_sw_fallback_count++;
        lv_draw_sw_image(draw_unit, draw_dsc, img_coords);
    }
    else {
        g_state.stats.image_hw_count++;
    }
}

static void execute_task(hmi_nexus_f133_g2d_unit_t * unit)
{
    lv_draw_task_t * task;
    const lv_draw_fill_dsc_t * fill_dsc;
    const lv_draw_image_dsc_t * image_dsc;

    if(unit == NULL || unit->task_act == NULL) {
        return;
    }

    task = unit->task_act;

    /* Keep the F133 bring-up conservative: plain fills and same-format,
     * full-opa image blits run on G2D, everything else still falls back to
     * LVGL's stock software renderer. */
    switch(task->type) {
        case LV_DRAW_TASK_TYPE_FILL:
            fill_dsc = (const lv_draw_fill_dsc_t *)task->draw_dsc;
            if(!execute_fill_hw(unit, fill_dsc, &task->area)) {
                g_state.stats.fill_sw_fallback_count++;
                lv_draw_sw_fill(&unit->base_unit, task->draw_dsc, &task->area);
            }
            break;
        case LV_DRAW_TASK_TYPE_IMAGE:
            image_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
            if(image_dsc != NULL && image_dsc->tile) {
                lv_draw_image_tiled_helper(&unit->base_unit, image_dsc, &task->area, image_draw_core);
            }
            else {
                lv_draw_image_normal_helper(&unit->base_unit, image_dsc, &task->area, image_draw_core);
            }
            break;
        case LV_DRAW_TASK_TYPE_LAYER:
            image_dsc = (const lv_draw_image_dsc_t *)task->draw_dsc;
            if(image_dsc != NULL) {
                const lv_layer_t * source_layer = (const lv_layer_t *)image_dsc->src;
                const lv_draw_buf_t * source_draw_buf = NULL;
                lv_draw_buf_t * temporary_dma_draw_buf = NULL;
                lv_draw_image_dsc_t layer_draw_dsc;
                lv_area_t clipped_area;
                bool used_hw = false;

                if(source_layer != NULL &&
                   source_layer->draw_buf != NULL &&
                   unit->base_unit.clip_area != NULL &&
                   lv_area_intersect(&clipped_area, &task->area, unit->base_unit.clip_area)) {
                    source_draw_buf = source_layer->draw_buf;
                    if(!source_draw_buf_supported(source_draw_buf)) {
                        temporary_dma_draw_buf = duplicate_hw_source_draw_buf(source_draw_buf);
                        source_draw_buf = temporary_dma_draw_buf;
                    }

                    if(source_draw_buf != NULL) {
                        layer_draw_dsc = *image_dsc;
                        layer_draw_dsc.src = source_draw_buf;
                        layer_draw_dsc.header = source_draw_buf->header;
                        used_hw = execute_image_blit_hw(&unit->base_unit,
                                                        &layer_draw_dsc,
                                                        source_draw_buf,
                                                        &task->area,
                                                        &clipped_area);
                    }
                }

                if(temporary_dma_draw_buf != NULL) {
                    lv_draw_buf_destroy(temporary_dma_draw_buf);
                }

                if(used_hw) {
                    g_state.stats.layer_hw_count++;
                }
                else {
                    g_state.stats.layer_sw_fallback_count++;
                    lv_draw_sw_layer(&unit->base_unit, task->draw_dsc, &task->area);
                }
            }
            break;
        default:
            break;
    }

    task->state = LV_DRAW_TASK_STATE_READY;
    unit->task_act = NULL;
    lv_draw_dispatch_request();
}

static int32_t g2d_dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer)
{
    hmi_nexus_f133_g2d_unit_t * unit = (hmi_nexus_f133_g2d_unit_t *)draw_unit;
    lv_draw_task_t * task;

    if(unit->task_act != NULL) {
        return 0;
    }

    task = lv_draw_get_next_available_task(layer, NULL, HMI_NEXUS_F133_DRAW_UNIT_ID);
    if(task == NULL || task->preferred_draw_unit_id != HMI_NEXUS_F133_DRAW_UNIT_ID) {
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

static int32_t g2d_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task)
{
    bool supported = false;

    LV_UNUSED(draw_unit);

    if(task == NULL || !g_state.initialized) {
        return 0;
    }

    switch(task->type) {
        case LV_DRAW_TASK_TYPE_FILL:
            supported = fill_task_supported(task);
            if(supported) {
                g_state.stats.fill_accept_count++;
                if(task->preference_score > 95) {
                    task->preference_score = 95;
                    task->preferred_draw_unit_id = HMI_NEXUS_F133_DRAW_UNIT_ID;
                }
            }
            else {
                g_state.stats.fill_reject_count++;
            }
            break;
        case LV_DRAW_TASK_TYPE_IMAGE:
            supported = image_task_supported(task);
            if(supported) {
                g_state.stats.image_accept_count++;
                if(task->preference_score > 95) {
                    task->preference_score = 95;
                    task->preferred_draw_unit_id = HMI_NEXUS_F133_DRAW_UNIT_ID;
                }
            }
            else {
                g_state.stats.image_reject_count++;
            }
            break;
        case LV_DRAW_TASK_TYPE_LAYER:
            supported = layer_task_supported(task);
            if(supported) {
                g_state.stats.layer_accept_count++;
                if(task->preference_score > 95) {
                    task->preference_score = 95;
                    task->preferred_draw_unit_id = HMI_NEXUS_F133_DRAW_UNIT_ID;
                }
            }
            else {
                g_state.stats.layer_reject_count++;
            }
            break;
        default:
            break;
    }

    return 0;
}

void hmi_nexus_f133_lvgl_g2d_init(void)
{
    hmi_nexus_f133_g2d_unit_t * unit;
    lv_draw_buf_handlers_t * handlers;

    if(g_state.initialized) {
        return;
    }

    g_state.memops = GetMemAdapterOpsS();
    if(g_state.memops == NULL || SunxiMemOpen(g_state.memops) < 0) {
        g_state.memops = NULL;
        return;
    }

    g_state.g2d_fd = open("/dev/g2d", O_RDWR | O_CLOEXEC);
    if(g_state.g2d_fd < 0) {
        SunxiMemClose(g_state.memops);
        g_state.memops = NULL;
        return;
    }

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
    hmi_nexus_lvgl_install_image_cache_draw_buf_handlers(&g_state.image_cache_handlers);

    unit = lv_draw_create_unit(sizeof(*unit));
    if(unit == NULL) {
        close(g_state.g2d_fd);
        g_state.g2d_fd = -1;
        SunxiMemClose(g_state.memops);
        g_state.memops = NULL;
        return;
    }

    unit->base_unit.dispatch_cb = g2d_dispatch;
    unit->base_unit.evaluate_cb = g2d_evaluate;
    g_state.initialized = true;
}

int hmi_nexus_f133_lvgl_g2d_ready(void)
{
    return g_state.initialized ? 1 : 0;
}

void hmi_nexus_f133_lvgl_g2d_get_stats(hmi_nexus_f133_lvgl_g2d_stats_t * stats)
{
    if(stats == NULL) {
        return;
    }

    *stats = g_state.stats;
}

void hmi_nexus_f133_lvgl_g2d_register_external_buffer(
    const hmi_nexus_f133_lvgl_buffer_desc_t * desc)
{
    if(!g_state.initialized || desc == NULL) {
        return;
    }

    if(desc->memory_type == HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_HOST) {
        remove_buffer_entry(desc->data);
        return;
    }

    upsert_buffer_entry(desc, false);
}

void hmi_nexus_f133_lvgl_g2d_unregister_external_buffer(uint8_t * data)
{
    hmi_nexus_f133_g2d_buffer_entry_t * entry;

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

void hmi_nexus_f133_lvgl_g2d_init(void)
{
}

int hmi_nexus_f133_lvgl_g2d_ready(void)
{
    return 0;
}

void hmi_nexus_f133_lvgl_g2d_get_stats(hmi_nexus_f133_lvgl_g2d_stats_t * stats)
{
    if(stats == NULL) {
        return;
    }

    *stats = (hmi_nexus_f133_lvgl_g2d_stats_t){0};
}

void hmi_nexus_f133_lvgl_g2d_register_external_buffer(
    const hmi_nexus_f133_lvgl_buffer_desc_t * desc)
{
    (void)desc;
}

void hmi_nexus_f133_lvgl_g2d_unregister_external_buffer(uint8_t * data)
{
    (void)data;
}

#endif
