#pragma once

#include <cstddef>

#include "hmi_nexus/ui/lvgl_port.h"
#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::product {

#ifndef HMI_NEXUS_TARGET_F133
#define HMI_NEXUS_TARGET_F133 0
#endif

inline constexpr char kProductName[] = "default_panel";
#if HMI_NEXUS_TARGET_F133
/* F133 bring-up defaults to UI-only mode because many Tina images do not ship
 * libcurl/OpenSSL in the rootfs yet. Re-enable these once the runtime deps are
 * present on the board. */
inline constexpr bool kEnableHttps = false;
inline constexpr bool kEnableMqtt = false;
inline constexpr bool kEnableWebSocket = false;
inline constexpr bool kEnableOta = false;
#else
inline constexpr bool kEnableHttps = true;
inline constexpr bool kEnableMqtt = true;
inline constexpr bool kEnableWebSocket = true;
inline constexpr bool kEnableOta = true;
#endif
inline constexpr bool kEnableConsoleLog = true;
inline constexpr bool kEnableFileLog = true;
inline constexpr bool kEnableLogColor = true;
/* Leave empty to use only the built-in defaults below. Setting
 * HMI_NEXUS_CONFIG at runtime still overrides this. */
inline constexpr char kRuntimeConfigPath[] = "";
inline constexpr char kLogFilePath[] = "logs/hmi.log";
inline constexpr std::size_t kLogMaxFileSize = 2 * 1024 * 1024;
inline constexpr std::size_t kLogMaxBackupFiles = 5;
inline constexpr system::Logger::Level kLogLevel = system::Logger::Level::kInfo;

inline ui::LvglPort::Config MakeDefaultLvglPortConfig() {
    ui::LvglPort::Config config;
    config.horizontal_resolution = 800;
    config.vertical_resolution = 480;
    config.dpi = 160;
    config.draw_buffer_lines = 40;
    config.framebuffer_device = "/dev/fb0";
    config.display_backend = ui::LvglPort::DisplayBackendKind::kLinuxFramebuffer;
#if HMI_NEXUS_TARGET_F133
    /* F133 follows the Tina SDK fbdev + Sunxi G2D present path and keeps the
     * panel in its native orientation by default.
     * Keep LVGL in the framebuffer's native scanout format too: on this board
     * the RGB565 offscreen -> ARGB8888 present path causes green/half-screen
     * corruption, while native-format rendering is stable. */
    config.prefer_native_render_format = true;
    config.accel_2d_backend = ui::LvglPort::Accel2DBackendKind::kSunxiG2D;
    config.rotation = device::display::Rotation::k0;
    config.enable_rotated_ge2d_draw_unit = false;
    /* Keep the new F133 LVGL draw-unit skeleton opt-in for now.
     * The framebuffer present path is already stable; hardware task submit for
     * fill/image/layer will be enabled after each family is validated. */
    config.enable_sunxi_g2d_draw_unit = false;
#else
    config.prefer_native_render_format = false;
    config.accel_2d_backend = ui::LvglPort::Accel2DBackendKind::kD211Ge2D;
    config.rotation = device::display::Rotation::k90;
    config.enable_rotated_ge2d_draw_unit = true;
    config.enable_sunxi_g2d_draw_unit = false;
#endif
    config.touch_enabled = true;
    config.touch_device = "auto";
    /* These knobs are for panel/touch wiring differences only.
     * Display rotation is handled by LVGL, so keep them at neutral defaults
     * unless the touch controller itself reports swapped or mirrored axes. */
    config.touch_swap_axes = false;
    config.touch_invert_x = false;
    config.touch_invert_y = false;
    /* false = auto-detect axis range from the input device.
     * true = use the manual min/max values below. */
    config.touch_use_calibration = false;
    config.touch_min_x = 0;
    config.touch_max_x = 0;
    config.touch_min_y = 0;
    config.touch_max_y = 0;
    config.perf_stats_enabled = true;
    config.perf_report_interval_ms = 3000;
    return config;
}

}  // namespace hmi_nexus::product
