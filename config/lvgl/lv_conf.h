#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * Minimal LVGL 9.2.0 configuration for the current Linux HMI skeleton.
 * Keep this file intentionally small and override only the options we rely on.
 */

#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (512U * 1024U)
#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 160
#define LV_DRAW_BUF_STRIDE_ALIGN 8
#define LV_DRAW_BUF_ALIGN 64

#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 0

#define LV_USE_FS_POSIX 1
#if LV_USE_FS_POSIX
#define LV_FS_POSIX_LETTER 'L'
#define LV_FS_DEFAULT_DRIVE_LETTER 'L'
#define LV_FS_POSIX_PATH ""
#define LV_FS_POSIX_CACHE_SIZE 0
#endif

#define LV_CACHE_DEF_SIZE (10U * 1024U * 1024U)
#define LV_IMAGE_HEADER_CACHE_DEF_CNT 20
#define LV_CACHE_IMG_NUM 15

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 0

#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1
#define LV_USE_PERF_MONITOR_LOG_MODE 1

#define LV_USE_DEMO_WIDGETS 1
#define LV_USE_DEMO_BENCHMARK 1

#endif /* LV_CONF_H */
