#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum hmi_nexus_d211_lvgl_buffer_memory_type_t {
    HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_HOST = 0,
    HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF = 1,
    HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_PHYSICAL = 2,
} hmi_nexus_d211_lvgl_buffer_memory_type_t;

typedef struct hmi_nexus_d211_lvgl_buffer_desc_t {
    uint8_t * data;
    size_t size;
    size_t stride;
    int dma_fd;
    uintptr_t physical_address;
    hmi_nexus_d211_lvgl_buffer_memory_type_t memory_type;
} hmi_nexus_d211_lvgl_buffer_desc_t;

typedef struct hmi_nexus_d211_lvgl_ge2d_stats_t {
    uint64_t draw_buf_dma_alloc_count;
    uint64_t draw_buf_host_alloc_count;
    uint64_t fill_accept_count;
    uint64_t fill_hw_count;
    uint64_t fill_reject_count;
    uint64_t border_accept_count;
    uint64_t border_hw_count;
    uint64_t border_reject_count;
    uint64_t border_sw_fallback_count;
    uint64_t box_shadow_accept_count;
    uint64_t box_shadow_hw_count;
    uint64_t box_shadow_reject_count;
    uint64_t box_shadow_sw_fallback_count;
    uint64_t image_accept_count;
    uint64_t image_hw_count;
    uint64_t image_reject_count;
    uint64_t image_sw_fallback_count;
    uint64_t layer_accept_count;
    uint64_t layer_hw_count;
    uint64_t layer_reject_count;
    uint64_t layer_sw_fallback_count;
    uint64_t label_task_count;
    uint64_t label_hw_count;
    uint64_t label_task_sw_fallback_count;
    uint64_t label_fast_glyph_count;
    uint64_t label_sw_glyph_fallback_count;
    uint64_t arc_task_count;
    uint64_t hw_submit_fail_count;
} hmi_nexus_d211_lvgl_ge2d_stats_t;

void hmi_nexus_d211_lvgl_ge2d_init(void);
int hmi_nexus_d211_lvgl_ge2d_ready(void);
void hmi_nexus_d211_lvgl_ge2d_get_stats(hmi_nexus_d211_lvgl_ge2d_stats_t * stats);
void hmi_nexus_d211_lvgl_ge2d_register_external_buffer(
    const hmi_nexus_d211_lvgl_buffer_desc_t * desc);
void hmi_nexus_d211_lvgl_ge2d_unregister_external_buffer(uint8_t * data);

#ifdef __cplusplus
}
#endif
