#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/device/display/types.h"

#if HMI_NEXUS_HAS_LVGL
#include "lvgl.h"
#endif

namespace hmi_nexus::device::display {
class Accel2DBackend;
class DisplayBackend;
}

namespace hmi_nexus::ui::detail {
class LinuxTouchInput;
}

namespace hmi_nexus::ui {

class LvglPort {
public:
    enum class DisplayBackendKind {
        kAuto = 0,
        kHeadless,
        kLinuxFramebuffer,
    };

    enum class Accel2DBackendKind {
        kAuto = 0,
        kNone,
        kD211Ge2D,
        kSunxiG2D,
    };

    struct Config {
        int horizontal_resolution = 800;
        int vertical_resolution = 480;
        int dpi = 160;
        std::size_t draw_buffer_lines = 40;
        std::string framebuffer_device = "/dev/fb0";
        bool prefer_native_render_format = false;
        DisplayBackendKind display_backend = DisplayBackendKind::kAuto;
        Accel2DBackendKind accel_2d_backend = Accel2DBackendKind::kAuto;
        device::display::Rotation rotation = device::display::Rotation::k0;
        bool enable_rotated_ge2d_draw_unit = false;
        bool enable_sunxi_g2d_draw_unit = false;
        bool touch_enabled = true;
        std::string touch_device = "auto";
        bool touch_swap_axes = false;
        bool touch_invert_x = false;
        bool touch_invert_y = false;
        bool touch_use_calibration = false;
        int touch_min_x = 0;
        int touch_max_x = 0;
        int touch_min_y = 0;
        int touch_max_y = 0;
        bool perf_stats_enabled = true;
        std::uint32_t perf_report_interval_ms = 3000;
    };

    LvglPort();
    explicit LvglPort(Config config);
    ~LvglPort();

    void setConfig(Config config);
    const Config& config() const;

    common::Result initialize();
    common::Result applyTheme(const std::string& theme_name);
    void pump();
    bool initialized() const;

private:
    common::Result initializeBackend();
    common::Result initializeLvglDisplay();
    const device::display::BufferDescriptor* resolveActiveBuffer(std::uint8_t* data) const;
    void resetPerformanceWindow(std::chrono::steady_clock::time_point now);
    void recordFlushDuration(std::uint64_t flush_time_us);
    void maybeReportPerformance(std::chrono::steady_clock::time_point now);
#if HMI_NEXUS_HAS_LVGL
    static void FlushCallback(lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map);
#endif
    void handleFlush();

    Config config_;
    void* display_ = nullptr;
    bool initialized_ = false;
    std::unique_ptr<device::display::DisplayBackend> display_backend_;
    std::unique_ptr<device::display::Accel2DBackend> accel_2d_backend_;
    std::unique_ptr<detail::LinuxTouchInput> touch_input_;
    std::chrono::steady_clock::time_point last_tick_time_{};
    std::chrono::steady_clock::time_point perf_window_start_{};
    std::uint64_t perf_pump_count_ = 0;
    std::uint64_t perf_present_count_ = 0;
    std::uint64_t perf_total_handler_time_us_ = 0;
    std::uint64_t perf_max_handler_time_us_ = 0;
    std::uint64_t perf_total_flush_time_us_ = 0;
    std::uint64_t perf_max_flush_time_us_ = 0;
    std::uint64_t perf_over_budget_count_ = 0;
};

}  // namespace hmi_nexus::ui
