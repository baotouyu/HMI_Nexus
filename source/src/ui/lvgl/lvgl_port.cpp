#include "hmi_nexus/ui/lvgl_port.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/device/display/accel_2d_backend.h"
#include "hmi_nexus/device/display/backend_factory.h"
#include "hmi_nexus/device/display/display_backend.h"
#include "hmi_nexus/system/logger.h"
#include "source/src/ui/lvgl/d211/d211_lvgl_ge2d.h"
#include "source/src/ui/lvgl/d211/d211_lvgl_mpp_decoder.h"
#include "source/src/ui/lvgl/f133/f133_lvgl_g2d.h"
#include "source/src/ui/lvgl/linux_touch_input.h"

namespace hmi_nexus::ui {
namespace {

using hmi_nexus::device::display::Accel2DBackend;
using hmi_nexus::device::display::BufferDescriptor;
using hmi_nexus::device::display::BufferMemoryType;
using hmi_nexus::device::display::CreateD211Ge2DAccel2DBackend;
using hmi_nexus::device::display::CreateHeadlessDisplayBackend;
using hmi_nexus::device::display::CreateLinuxFbdevDisplayBackend;
using hmi_nexus::device::display::CreateNullAccel2DBackend;
using hmi_nexus::device::display::CreateSunxiG2DAccel2DBackend;
using hmi_nexus::device::display::DisplayBackend;
using hmi_nexus::device::display::DisplayConfig;
using hmi_nexus::device::display::HasD211Ge2DBackend;
using hmi_nexus::device::display::HasSunxiG2DBackend;
using hmi_nexus::device::display::PixelFormat;
using hmi_nexus::device::display::PixelFormatName;
using hmi_nexus::device::display::Rotation;
using hmi_nexus::device::display::RotationName;
using hmi_nexus::device::display::SurfaceInfo;

struct Ge2DStatsReportState {
    bool enabled = false;
    std::chrono::steady_clock::time_point last_report_time {};
    hmi_nexus_d211_lvgl_ge2d_stats_t last_stats {};
};

struct SunxiG2DStatsReportState {
    bool enabled = false;
    std::chrono::steady_clock::time_point last_report_time {};
    hmi_nexus_f133_lvgl_g2d_stats_t last_stats {};
};

std::unordered_map<const LvglPort*, Ge2DStatsReportState> g_ge2d_stats_report_states;
std::unordered_map<const LvglPort*, SunxiG2DStatsReportState> g_sunxi_g2d_stats_report_states;
constexpr std::uint64_t kFrameBudgetUs = 16667;

bool StatsChanged(const hmi_nexus_d211_lvgl_ge2d_stats_t& lhs,
                  const hmi_nexus_d211_lvgl_ge2d_stats_t& rhs) {
    return lhs.draw_buf_dma_alloc_count != rhs.draw_buf_dma_alloc_count ||
           lhs.draw_buf_host_alloc_count != rhs.draw_buf_host_alloc_count ||
           lhs.fill_accept_count != rhs.fill_accept_count ||
           lhs.fill_hw_count != rhs.fill_hw_count ||
           lhs.fill_reject_count != rhs.fill_reject_count ||
           lhs.border_accept_count != rhs.border_accept_count ||
           lhs.border_hw_count != rhs.border_hw_count ||
           lhs.border_reject_count != rhs.border_reject_count ||
           lhs.border_sw_fallback_count != rhs.border_sw_fallback_count ||
           lhs.box_shadow_accept_count != rhs.box_shadow_accept_count ||
           lhs.box_shadow_hw_count != rhs.box_shadow_hw_count ||
           lhs.box_shadow_reject_count != rhs.box_shadow_reject_count ||
           lhs.box_shadow_sw_fallback_count != rhs.box_shadow_sw_fallback_count ||
           lhs.image_accept_count != rhs.image_accept_count ||
           lhs.image_hw_count != rhs.image_hw_count ||
           lhs.image_reject_count != rhs.image_reject_count ||
           lhs.image_sw_fallback_count != rhs.image_sw_fallback_count ||
           lhs.layer_accept_count != rhs.layer_accept_count ||
           lhs.layer_hw_count != rhs.layer_hw_count ||
           lhs.layer_reject_count != rhs.layer_reject_count ||
           lhs.layer_sw_fallback_count != rhs.layer_sw_fallback_count ||
           lhs.label_task_count != rhs.label_task_count ||
           lhs.label_hw_count != rhs.label_hw_count ||
           lhs.label_task_sw_fallback_count != rhs.label_task_sw_fallback_count ||
           lhs.label_fast_glyph_count != rhs.label_fast_glyph_count ||
           lhs.label_sw_glyph_fallback_count != rhs.label_sw_glyph_fallback_count ||
           lhs.arc_task_count != rhs.arc_task_count ||
           lhs.hw_submit_fail_count != rhs.hw_submit_fail_count;
}

bool StatsChanged(const hmi_nexus_f133_lvgl_g2d_stats_t& lhs,
                  const hmi_nexus_f133_lvgl_g2d_stats_t& rhs) {
    return lhs.tracked_buffer_count != rhs.tracked_buffer_count ||
           lhs.fill_accept_count != rhs.fill_accept_count ||
           lhs.fill_hw_count != rhs.fill_hw_count ||
           lhs.fill_sw_fallback_count != rhs.fill_sw_fallback_count ||
           lhs.fill_reject_count != rhs.fill_reject_count ||
           lhs.image_accept_count != rhs.image_accept_count ||
           lhs.image_hw_count != rhs.image_hw_count ||
           lhs.image_sw_fallback_count != rhs.image_sw_fallback_count ||
           lhs.image_reject_count != rhs.image_reject_count ||
           lhs.layer_accept_count != rhs.layer_accept_count ||
           lhs.layer_hw_count != rhs.layer_hw_count ||
           lhs.layer_sw_fallback_count != rhs.layer_sw_fallback_count ||
           lhs.layer_reject_count != rhs.layer_reject_count ||
           lhs.hw_submit_fail_count != rhs.hw_submit_fail_count;
}

std::string FormatCounter(const char* label, std::uint64_t total, std::uint64_t delta) {
    std::ostringstream stream;
    stream << label << '=' << total << "(+" << delta << ')';
    return stream.str();
}

std::string StatsDeltaToString(const hmi_nexus_d211_lvgl_ge2d_stats_t& current,
                               const hmi_nexus_d211_lvgl_ge2d_stats_t& previous) {
    std::ostringstream stream;
    stream << FormatCounter("dma_alloc",
                            current.draw_buf_dma_alloc_count,
                            current.draw_buf_dma_alloc_count - previous.draw_buf_dma_alloc_count)
           << ", "
           << FormatCounter("host_alloc",
                            current.draw_buf_host_alloc_count,
                            current.draw_buf_host_alloc_count - previous.draw_buf_host_alloc_count)
           << ", fill(a/h/r)="
           << current.fill_accept_count << '/' << current.fill_hw_count << '/'
           << current.fill_reject_count << " (+"
           << (current.fill_accept_count - previous.fill_accept_count) << "/+"
           << (current.fill_hw_count - previous.fill_hw_count) << "/+"
           << (current.fill_reject_count - previous.fill_reject_count) << "), border(a/h/sw/r)="
           << current.border_accept_count << '/' << current.border_hw_count << '/'
           << current.border_sw_fallback_count << '/' << current.border_reject_count << " (+"
           << (current.border_accept_count - previous.border_accept_count) << "/+"
           << (current.border_hw_count - previous.border_hw_count) << "/+"
           << (current.border_sw_fallback_count - previous.border_sw_fallback_count) << "/+"
           << (current.border_reject_count - previous.border_reject_count)
           << "), shadow(a/h/sw/r)="
           << current.box_shadow_accept_count << '/' << current.box_shadow_hw_count << '/'
           << current.box_shadow_sw_fallback_count << '/' << current.box_shadow_reject_count << " (+"
           << (current.box_shadow_accept_count - previous.box_shadow_accept_count) << "/+"
           << (current.box_shadow_hw_count - previous.box_shadow_hw_count) << "/+"
           << (current.box_shadow_sw_fallback_count - previous.box_shadow_sw_fallback_count) << "/+"
           << (current.box_shadow_reject_count - previous.box_shadow_reject_count)
           << "), image(a/h/sw/r)="
           << current.image_accept_count << '/' << current.image_hw_count << '/'
           << current.image_sw_fallback_count << '/' << current.image_reject_count << " (+"
           << (current.image_accept_count - previous.image_accept_count) << "/+"
           << (current.image_hw_count - previous.image_hw_count) << "/+"
           << (current.image_sw_fallback_count - previous.image_sw_fallback_count) << "/+"
           << (current.image_reject_count - previous.image_reject_count)
           << "), layer(a/h/sw/r)="
           << current.layer_accept_count << '/' << current.layer_hw_count << '/'
           << current.layer_sw_fallback_count << '/' << current.layer_reject_count << " (+"
           << (current.layer_accept_count - previous.layer_accept_count) << "/+"
           << (current.layer_hw_count - previous.layer_hw_count) << "/+"
           << (current.layer_sw_fallback_count - previous.layer_sw_fallback_count) << "/+"
           << (current.layer_reject_count - previous.layer_reject_count)
           << "), label(task/h/sw)="
           << current.label_task_count << '/' << current.label_hw_count << '/'
           << current.label_task_sw_fallback_count << " (+"
           << (current.label_task_count - previous.label_task_count) << "/+"
           << (current.label_hw_count - previous.label_hw_count) << "/+"
           << (current.label_task_sw_fallback_count - previous.label_task_sw_fallback_count)
           << "), label_glyph(fast/sw)="
           << current.label_fast_glyph_count << '/'
           << current.label_sw_glyph_fallback_count << " (+"
           << (current.label_fast_glyph_count - previous.label_fast_glyph_count) << "/+"
           << (current.label_sw_glyph_fallback_count - previous.label_sw_glyph_fallback_count)
           << "), "
           << FormatCounter("arc",
                            current.arc_task_count,
                            current.arc_task_count - previous.arc_task_count)
           << ", "
           << FormatCounter("hw_fail",
                            current.hw_submit_fail_count,
                            current.hw_submit_fail_count - previous.hw_submit_fail_count);
    return stream.str();
}

std::string StatsDeltaToString(const hmi_nexus_f133_lvgl_g2d_stats_t& current,
                               const hmi_nexus_f133_lvgl_g2d_stats_t& previous) {
    std::ostringstream stream;
    stream << FormatCounter("tracked_buffers",
                            current.tracked_buffer_count,
                            current.tracked_buffer_count - previous.tracked_buffer_count)
           << ", fill(a/h/sw/r)="
           << current.fill_accept_count << '/' << current.fill_hw_count << '/'
           << current.fill_sw_fallback_count << '/' << current.fill_reject_count << " (+"
           << (current.fill_accept_count - previous.fill_accept_count) << "/+"
           << (current.fill_hw_count - previous.fill_hw_count) << "/+"
           << (current.fill_sw_fallback_count - previous.fill_sw_fallback_count) << "/+"
           << (current.fill_reject_count - previous.fill_reject_count)
           << "), image(a/h/sw/r)="
           << current.image_accept_count << '/' << current.image_hw_count << '/'
           << current.image_sw_fallback_count << '/' << current.image_reject_count << " (+"
           << (current.image_accept_count - previous.image_accept_count) << "/+"
           << (current.image_hw_count - previous.image_hw_count) << "/+"
           << (current.image_sw_fallback_count - previous.image_sw_fallback_count) << "/+"
           << (current.image_reject_count - previous.image_reject_count)
           << "), layer(a/h/sw/r)="
           << current.layer_accept_count << '/' << current.layer_hw_count << '/'
           << current.layer_sw_fallback_count << '/' << current.layer_reject_count << " (+"
           << (current.layer_accept_count - previous.layer_accept_count) << "/+"
           << (current.layer_hw_count - previous.layer_hw_count) << "/+"
           << (current.layer_sw_fallback_count - previous.layer_sw_fallback_count) << "/+"
           << (current.layer_reject_count - previous.layer_reject_count)
           << "), "
           << FormatCounter("hw_fail",
                            current.hw_submit_fail_count,
                            current.hw_submit_fail_count - previous.hw_submit_fail_count);
    return stream.str();
}

std::string FormatMilliseconds(double value_ms) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value_ms;
    return stream.str();
}

void EnableGe2DStatsReporting(const LvglPort* port) {
    auto& state = g_ge2d_stats_report_states[port];
    state.enabled = true;
    state.last_report_time = std::chrono::steady_clock::now();
    hmi_nexus_d211_lvgl_ge2d_get_stats(&state.last_stats);
}

void DisableGe2DStatsReporting(const LvglPort* port) {
    g_ge2d_stats_report_states.erase(port);
}

void EnableSunxiG2DStatsReporting(const LvglPort* port) {
    auto& state = g_sunxi_g2d_stats_report_states[port];
    state.enabled = true;
    state.last_report_time = std::chrono::steady_clock::now();
    hmi_nexus_f133_lvgl_g2d_get_stats(&state.last_stats);
}

void DisableSunxiG2DStatsReporting(const LvglPort* port) {
    g_sunxi_g2d_stats_report_states.erase(port);
}

void MaybeReportGe2DStats(const LvglPort* port, std::chrono::steady_clock::time_point now) {
    constexpr auto kGe2DStatsReportPeriod = std::chrono::seconds(3);

    const auto it = g_ge2d_stats_report_states.find(port);
    if (it == g_ge2d_stats_report_states.end() || !it->second.enabled) {
        return;
    }

    if (now - it->second.last_report_time < kGe2DStatsReportPeriod) {
        return;
    }

    hmi_nexus_d211_lvgl_ge2d_stats_t current {};
    hmi_nexus_d211_lvgl_ge2d_get_stats(&current);
    if (StatsChanged(current, it->second.last_stats)) {
        system::Logger::Info("ui.lvgl.ge2d",
                             std::string("GE2D stats: ") +
                                 StatsDeltaToString(current, it->second.last_stats));
    }

    it->second.last_report_time = now;
    it->second.last_stats = current;
}

void MaybeReportSunxiG2DStats(const LvglPort* port, std::chrono::steady_clock::time_point now) {
    constexpr auto kGe2DStatsReportPeriod = std::chrono::seconds(3);

    const auto it = g_sunxi_g2d_stats_report_states.find(port);
    if (it == g_sunxi_g2d_stats_report_states.end() || !it->second.enabled) {
        return;
    }

    if (now - it->second.last_report_time < kGe2DStatsReportPeriod) {
        return;
    }

    hmi_nexus_f133_lvgl_g2d_stats_t current {};
    hmi_nexus_f133_lvgl_g2d_get_stats(&current);
    if (StatsChanged(current, it->second.last_stats)) {
        system::Logger::Info("ui.lvgl.sunxi_g2d",
                             std::string("Sunxi G2D draw-unit stats: ") +
                                 StatsDeltaToString(current, it->second.last_stats));
    }

    it->second.last_report_time = now;
    it->second.last_stats = current;
}

#if HMI_NEXUS_HAS_LVGL
PixelFormat FromLvglColorFormat(lv_color_format_t color_format) {
    switch (color_format) {
    case LV_COLOR_FORMAT_RGB565:
        return PixelFormat::kRgb565;
    case LV_COLOR_FORMAT_RGB888:
        return PixelFormat::kRgb888;
    case LV_COLOR_FORMAT_ARGB8888:
        return PixelFormat::kArgb8888;
    case LV_COLOR_FORMAT_XRGB8888:
        return PixelFormat::kXrgb8888;
    default:
        return PixelFormat::kUnknown;
    }
}

lv_color_format_t ToLvglColorFormat(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::kRgb565:
        return LV_COLOR_FORMAT_RGB565;
    case PixelFormat::kRgb888:
        return LV_COLOR_FORMAT_RGB888;
    case PixelFormat::kArgb8888:
        return LV_COLOR_FORMAT_ARGB8888;
    case PixelFormat::kXrgb8888:
        return LV_COLOR_FORMAT_XRGB8888;
    case PixelFormat::kUnknown:
        break;
    }
    return LV_COLOR_FORMAT_UNKNOWN;
}

lv_display_rotation_t ToLvglRotation(Rotation rotation) {
    switch (rotation) {
    case Rotation::k0:
        return LV_DISPLAY_ROTATION_0;
    case Rotation::k90:
        return LV_DISPLAY_ROTATION_90;
    case Rotation::k180:
        return LV_DISPLAY_ROTATION_180;
    case Rotation::k270:
        return LV_DISPLAY_ROTATION_270;
    }
    return LV_DISPLAY_ROTATION_0;
}
#endif

hmi_nexus_d211_lvgl_buffer_memory_type_t ToD211BufferMemoryType(BufferMemoryType memory_type) {
    switch (memory_type) {
    case BufferMemoryType::kDmaBuf:
        return HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_DMABUF;
    case BufferMemoryType::kPhysical:
        return HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_PHYSICAL;
    case BufferMemoryType::kHost:
        return HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_HOST;
    }
    return HMI_NEXUS_D211_LVGL_BUFFER_MEMORY_HOST;
}

hmi_nexus_f133_lvgl_buffer_memory_type_t ToF133BufferMemoryType(BufferMemoryType memory_type) {
    switch (memory_type) {
    case BufferMemoryType::kDmaBuf:
        return HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_DMABUF;
    case BufferMemoryType::kPhysical:
        return HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_PHYSICAL;
    case BufferMemoryType::kHost:
        return HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_HOST;
    }
    return HMI_NEXUS_F133_LVGL_BUFFER_MEMORY_HOST;
}

hmi_nexus_d211_lvgl_buffer_desc_t MakeD211BufferDesc(const BufferDescriptor& buffer) {
    hmi_nexus_d211_lvgl_buffer_desc_t desc {};
    desc.data = buffer.data;
    desc.size = buffer.size;
    desc.stride = buffer.stride;
    desc.dma_fd = buffer.dma_fd;
    desc.physical_address = buffer.physical_address;
    desc.memory_type = ToD211BufferMemoryType(buffer.memory_type);
    return desc;
}

hmi_nexus_f133_lvgl_buffer_desc_t MakeF133BufferDesc(const BufferDescriptor& buffer) {
    hmi_nexus_f133_lvgl_buffer_desc_t desc {};
    desc.data = buffer.data;
    desc.size = buffer.size;
    desc.stride = buffer.stride;
    desc.dma_fd = buffer.dma_fd;
    desc.physical_address = buffer.physical_address;
    desc.memory_type = ToF133BufferMemoryType(buffer.memory_type);
    return desc;
}

const char* DisplayBackendKindName(LvglPort::DisplayBackendKind kind) {
    switch (kind) {
    case LvglPort::DisplayBackendKind::kAuto:
        return "auto";
    case LvglPort::DisplayBackendKind::kHeadless:
        return "headless";
    case LvglPort::DisplayBackendKind::kLinuxFramebuffer:
        return "linux-fbdev";
    }
    return "auto";
}

const char* Accel2DBackendKindName(LvglPort::Accel2DBackendKind kind) {
    switch (kind) {
    case LvglPort::Accel2DBackendKind::kAuto:
        return "auto";
    case LvglPort::Accel2DBackendKind::kNone:
        return "none";
    case LvglPort::Accel2DBackendKind::kD211Ge2D:
        return "d211-ge2d";
    case LvglPort::Accel2DBackendKind::kSunxiG2D:
        return "sunxi-g2d";
    }
    return "auto";
}

std::unique_ptr<DisplayBackend> MakeDisplayBackend(LvglPort::DisplayBackendKind kind) {
    switch (kind) {
    case LvglPort::DisplayBackendKind::kHeadless:
        return CreateHeadlessDisplayBackend();
    case LvglPort::DisplayBackendKind::kLinuxFramebuffer:
        return CreateLinuxFbdevDisplayBackend();
    case LvglPort::DisplayBackendKind::kAuto:
        break;
    }
    return nullptr;
}

std::unique_ptr<Accel2DBackend> MakeAccel2DBackend(LvglPort::Accel2DBackendKind kind) {
    switch (kind) {
    case LvglPort::Accel2DBackendKind::kNone:
        return CreateNullAccel2DBackend();
    case LvglPort::Accel2DBackendKind::kD211Ge2D:
        return CreateD211Ge2DAccel2DBackend();
    case LvglPort::Accel2DBackendKind::kSunxiG2D:
        return CreateSunxiG2DAccel2DBackend();
    case LvglPort::Accel2DBackendKind::kAuto:
        break;
    }
    return nullptr;
}

}  // namespace

LvglPort::LvglPort() = default;

LvglPort::LvglPort(Config config)
    : config_(std::move(config)) {}

LvglPort::~LvglPort() {
    DisableGe2DStatsReporting(this);
    DisableSunxiG2DStatsReporting(this);

    if (display_backend_ == nullptr) {
        hmi_nexus_d211_lvgl_mpp_decoder_deinit();
        return;
    }

    const auto& primary = display_backend_->primaryBuffer();
    hmi_nexus_d211_lvgl_ge2d_unregister_external_buffer(primary.data);
    hmi_nexus_f133_lvgl_g2d_unregister_external_buffer(primary.data);

    const auto* secondary = display_backend_->secondaryBuffer();
    if (secondary != nullptr) {
        hmi_nexus_d211_lvgl_ge2d_unregister_external_buffer(secondary->data);
        hmi_nexus_f133_lvgl_g2d_unregister_external_buffer(secondary->data);
    }

    hmi_nexus_d211_lvgl_mpp_decoder_deinit();
}

void LvglPort::setConfig(Config config) {
    config_ = std::move(config);
}

const LvglPort::Config& LvglPort::config() const {
    return config_;
}

common::Result LvglPort::initialize() {
#if HMI_NEXUS_HAS_LVGL
    if (initialized_) {
        return common::Result::Ok();
    }

    if (config_.horizontal_resolution <= 0 || config_.vertical_resolution <= 0) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "LVGL resolution must be greater than zero");
    }

    lv_init();
    hmi_nexus_d211_lvgl_mpp_decoder_init();

    auto backend_result = initializeBackend();
    if (!backend_result) {
        return backend_result;
    }

    auto display_result = initializeLvglDisplay();
    if (!display_result) {
        return display_result;
    }

    initialized_ = true;
    last_tick_time_ = std::chrono::steady_clock::now();
    return common::Result::Ok();
#else
    system::Logger::Warn("ui.lvgl", "LVGL support is unavailable; UI remains in stub mode");
    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "LVGL support is not available");
#endif
}

common::Result LvglPort::initializeBackend() {
    const bool has_framebuffer_device =
        std::filesystem::exists(config_.framebuffer_device);
    bool rgb565_offscreen_requested = false;

    LvglPort::DisplayBackendKind resolved_display_kind = config_.display_backend;
    if (resolved_display_kind == LvglPort::DisplayBackendKind::kAuto) {
        resolved_display_kind = has_framebuffer_device ? LvglPort::DisplayBackendKind::kLinuxFramebuffer
                                                       : LvglPort::DisplayBackendKind::kHeadless;
    }

    LvglPort::Accel2DBackendKind resolved_accel_kind = config_.accel_2d_backend;
    if (resolved_accel_kind == LvglPort::Accel2DBackendKind::kAuto) {
        resolved_accel_kind =
            (resolved_display_kind == LvglPort::DisplayBackendKind::kLinuxFramebuffer &&
             HasD211Ge2DBackend())
                ? LvglPort::Accel2DBackendKind::kD211Ge2D
                : ((resolved_display_kind == LvglPort::DisplayBackendKind::kLinuxFramebuffer &&
                    HasSunxiG2DBackend())
                       ? LvglPort::Accel2DBackendKind::kSunxiG2D
                       : LvglPort::Accel2DBackendKind::kNone);
    }

    display_backend_ = MakeDisplayBackend(resolved_display_kind);
    if (!display_backend_) {
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "failed to create display backend: " +
                                        std::string(DisplayBackendKindName(resolved_display_kind)));
    }

    DisplayConfig display_config;
    display_config.requested_width = config_.horizontal_resolution;
    display_config.requested_height = config_.vertical_resolution;
    display_config.dpi = config_.dpi;
    display_config.draw_buffer_lines = config_.draw_buffer_lines;
    display_config.device_path = config_.framebuffer_device;
    display_config.rotation = config_.rotation;
    display_config.prefer_dma_draw_buffer =
        resolved_accel_kind == LvglPort::Accel2DBackendKind::kD211Ge2D ||
        resolved_accel_kind == LvglPort::Accel2DBackendKind::kSunxiG2D;
    if (resolved_display_kind == LvglPort::DisplayBackendKind::kLinuxFramebuffer &&
        (resolved_accel_kind == LvglPort::Accel2DBackendKind::kD211Ge2D ||
         resolved_accel_kind == LvglPort::Accel2DBackendKind::kSunxiG2D)) {
        if (config_.prefer_native_render_format) {
            display_config.draw_buffer_pixel_format = PixelFormat::kUnknown;
        } else {
            display_config.draw_buffer_pixel_format = PixelFormat::kRgb565;
            rgb565_offscreen_requested = true;
        }
    }

    auto display_result = display_backend_->initialize(display_config);
    if (!display_result &&
        config_.display_backend == LvglPort::DisplayBackendKind::kAuto &&
        resolved_display_kind == LvglPort::DisplayBackendKind::kLinuxFramebuffer) {
        system::Logger::Warn("ui.lvgl.display",
                             "Framebuffer backend initialization failed, fallback to headless: " +
                                 display_result.message());
        resolved_display_kind = LvglPort::DisplayBackendKind::kHeadless;
        resolved_accel_kind = LvglPort::Accel2DBackendKind::kNone;
        display_backend_ = CreateHeadlessDisplayBackend();
        display_config.prefer_dma_draw_buffer = false;
        display_result = display_backend_->initialize(display_config);
    }
    if (!display_result) {
        return display_result;
    }

    if (resolved_accel_kind == LvglPort::Accel2DBackendKind::kSunxiG2D) {
        const auto& surface = display_backend_->surface();
        const auto& primary = display_backend_->primaryBuffer();
        if (surface.pixel_format != PixelFormat::kRgb565 &&
            primary.pixel_format != surface.pixel_format) {
            display_config.draw_buffer_pixel_format = surface.pixel_format;
            rgb565_offscreen_requested = false;
            system::Logger::Warn(
                "ui.lvgl.display",
                "Sunxi G2D cross-format present is corrupt on this framebuffer; using native "
                "scanout format for LVGL draw buffers");
            display_result = display_backend_->initialize(display_config);
            if (!display_result) {
                return display_result;
            }
        }
    }

    if (config_.prefer_native_render_format) {
        const auto& surface = display_backend_->surface();
        const auto& primary = display_backend_->primaryBuffer();
        if (primary.pixel_format == surface.pixel_format) {
            system::Logger::Info(
                "ui.lvgl.display",
                "LVGL render format is pinned to the framebuffer native scanout format");
        }
    }

    accel_2d_backend_ = MakeAccel2DBackend(resolved_accel_kind);
    if (!accel_2d_backend_) {
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "failed to create 2D acceleration backend: " +
                                        std::string(Accel2DBackendKindName(resolved_accel_kind)));
    }

    auto accel_result = accel_2d_backend_->initialize(display_backend_->surface());
    if (!accel_result &&
        config_.accel_2d_backend == LvglPort::Accel2DBackendKind::kAuto &&
        resolved_accel_kind != LvglPort::Accel2DBackendKind::kNone) {
        system::Logger::Warn("ui.lvgl.accel",
                             "Acceleration backend initialization failed, fallback to software path: " +
                                 accel_result.message());
        accel_2d_backend_ = CreateNullAccel2DBackend();
        accel_result = accel_2d_backend_->initialize(display_backend_->surface());
        resolved_accel_kind = LvglPort::Accel2DBackendKind::kNone;
        if (rgb565_offscreen_requested) {
            display_config.prefer_dma_draw_buffer = false;
            display_config.draw_buffer_pixel_format = PixelFormat::kUnknown;
            auto redisplay_result = display_backend_->initialize(display_config);
            if (!redisplay_result) {
                return redisplay_result;
            }
            system::Logger::Warn("ui.lvgl.display",
                                 "RGB565 offscreen render path disabled because GE2D present "
                                 "backend is unavailable");
        }
    }
    if (!accel_result) {
        return accel_result;
    }

    DisableGe2DStatsReporting(this);
    DisableSunxiG2DStatsReporting(this);

    if (resolved_accel_kind == LvglPort::Accel2DBackendKind::kD211Ge2D) {
        const bool enable_ge2d_draw_unit =
            config_.rotation == device::display::Rotation::k0 ||
            config_.enable_rotated_ge2d_draw_unit;
        if (enable_ge2d_draw_unit) {
            hmi_nexus_d211_lvgl_ge2d_init();
            if (hmi_nexus_d211_lvgl_ge2d_ready() != 0) {
                EnableGe2DStatsReporting(this);
                if (config_.rotation == device::display::Rotation::k0) {
                    system::Logger::Info(
                        "ui.lvgl.ge2d",
                        "D211 GE2D draw unit enabled; periodic stats reporting active");
                } else {
                    system::Logger::Info(
                        "ui.lvgl.ge2d",
                        "D211 GE2D draw unit enabled on rotated display to match the official "
                        "fbdev pipeline; periodic stats reporting active");
                }
            } else {
                system::Logger::Warn(
                    "ui.lvgl.ge2d",
                    "D211 GE2D draw unit initialization failed; periodic stats reporting disabled");
            }
        } else {
            system::Logger::Info(
                "ui.lvgl.ge2d",
                "D211 GE2D draw unit is disabled on rotated displays; keep GE2D framebuffer present "
                "path only");
        }
    } else if (resolved_accel_kind == LvglPort::Accel2DBackendKind::kSunxiG2D) {
        if (config_.enable_sunxi_g2d_draw_unit) {
            hmi_nexus_f133_lvgl_g2d_init();
            if (hmi_nexus_f133_lvgl_g2d_ready() != 0) {
                EnableSunxiG2DStatsReporting(this);
                system::Logger::Info(
                    "ui.lvgl.sunxi_g2d",
                    "Sunxi G2D draw-unit skeleton enabled; fill/image/layer currently execute "
                    "through LVGL software fallback while the F133 hardware submit path is brought up");
            } else {
                system::Logger::Warn(
                    "ui.lvgl.sunxi_g2d",
                    "Sunxi G2D draw-unit skeleton initialization failed; keep framebuffer present "
                    "path only");
            }
        } else {
            system::Logger::Info(
                "ui.lvgl.sunxi_g2d",
                "Sunxi G2D draw-unit skeleton is disabled; keep framebuffer present path only");
        }
    }

    system::Logger::Info("ui.lvgl",
                         "LVGL backend selection: display=" +
                             std::string(display_backend_->name()) + ", accel=" +
                             std::string(accel_2d_backend_->name()));
    return common::Result::Ok();
}

common::Result LvglPort::initializeLvglDisplay() {
#if HMI_NEXUS_HAS_LVGL
    const auto& surface = display_backend_->surface();
    auto* display = lv_display_create(surface.width, surface.height);
    if (display == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "lv_display_create failed");
    }

    const auto& primary = display_backend_->primaryBuffer();
    const auto* secondary = display_backend_->secondaryBuffer();

    lv_display_set_color_format(display, ToLvglColorFormat(primary.pixel_format));
    lv_display_set_rotation(display, ToLvglRotation(config_.rotation));
    lv_display_set_buffers(display,
                           primary.data,
                           secondary != nullptr ? secondary->data : nullptr,
                           static_cast<std::uint32_t>(primary.size),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, FlushCallback);
    lv_display_set_dpi(display, surface.dpi);
    lv_display_set_user_data(display, this);
    lv_display_set_default(display);

    auto d211_buffer_desc = MakeD211BufferDesc(primary);
    hmi_nexus_d211_lvgl_ge2d_register_external_buffer(&d211_buffer_desc);

    auto f133_buffer_desc = MakeF133BufferDesc(primary);
    hmi_nexus_f133_lvgl_g2d_register_external_buffer(&f133_buffer_desc);

    if (secondary != nullptr) {
        d211_buffer_desc = MakeD211BufferDesc(*secondary);
        hmi_nexus_d211_lvgl_ge2d_register_external_buffer(&d211_buffer_desc);

        f133_buffer_desc = MakeF133BufferDesc(*secondary);
        hmi_nexus_f133_lvgl_g2d_register_external_buffer(&f133_buffer_desc);
    }

    display_ = display;
    resetPerformanceWindow(std::chrono::steady_clock::now());
    if (config_.touch_enabled) {
        if (touch_input_ == nullptr) {
            touch_input_ = std::make_unique<detail::LinuxTouchInput>();
        }

        detail::LinuxTouchInput::Config touch_config;
        touch_config.enabled = config_.touch_enabled;
        touch_config.device_path = config_.touch_device;
        touch_config.swap_axes = config_.touch_swap_axes;
        touch_config.invert_x = config_.touch_invert_x;
        touch_config.invert_y = config_.touch_invert_y;
        touch_config.use_calibration = config_.touch_use_calibration;
        touch_config.min_x = config_.touch_min_x;
        touch_config.max_x = config_.touch_max_x;
        touch_config.min_y = config_.touch_min_y;
        touch_config.max_y = config_.touch_max_y;

        const auto touch_result = touch_input_->initialize(display, touch_config);
        if (!touch_result) {
            system::Logger::Warn("ui.touch", "Touch input disabled: " + touch_result.message());
            touch_input_.reset();
        }
    } else {
        touch_input_.reset();
        system::Logger::Info("ui.touch", "Touch input is disabled by configuration");
    }

    system::Logger::Info("ui.lvgl",
                         "LVGL 9.2.0 initialized: " + std::to_string(surface.width) + "x" +
                             std::to_string(surface.height) + ", render=" +
                             PixelFormatName(primary.pixel_format) + ", scanout=" +
                             PixelFormatName(surface.pixel_format) + ", rotation=" +
                             RotationName(config_.rotation));
    return common::Result::Ok();
#else
    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "LVGL support is not available");
#endif
}

common::Result LvglPort::applyTheme(const std::string& theme_name) {
#if HMI_NEXUS_HAS_LVGL
    if (!initialized_ || display_ == nullptr) {
        return common::Result::Fail(common::ErrorCode::kNotReady,
                                    "LVGL must be initialized before applying theme");
    }

    lv_theme_t* theme = nullptr;
    auto* display = static_cast<lv_display_t*>(display_);

    if (theme_name == "mono") {
        theme = lv_theme_mono_init(display, false, LV_FONT_DEFAULT);
    } else if (theme_name == "simple") {
        theme = lv_theme_simple_init(display);
    } else {
        theme = lv_theme_default_init(display,
                                      lv_color_hex(0x0E7490),
                                      lv_color_hex(0xD97706),
                                      false,
                                      LV_FONT_DEFAULT);
    }

    if (theme == nullptr) {
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "failed to initialize LVGL theme: " + theme_name);
    }

    lv_display_set_theme(display, theme);
    system::Logger::Info("ui.lvgl", "Applied LVGL theme: " + theme_name);
    return common::Result::Ok();
#else
    (void)theme_name;
    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "LVGL support is not available");
#endif
}

void LvglPort::pump() {
#if HMI_NEXUS_HAS_LVGL
    if (!initialized_) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_time_).count();
    if (elapsed_ms > 0) {
        lv_tick_inc(static_cast<std::uint32_t>(elapsed_ms));
        last_tick_time_ = now;
    }

    const auto handler_start = std::chrono::steady_clock::now();
    lv_timer_handler();
    const auto handler_end = std::chrono::steady_clock::now();

    if (config_.perf_stats_enabled) {
        const auto handler_time_us =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           handler_end - handler_start)
                                           .count());
        ++perf_pump_count_;
        perf_total_handler_time_us_ += handler_time_us;
        perf_max_handler_time_us_ = std::max(perf_max_handler_time_us_, handler_time_us);
        if (handler_time_us > kFrameBudgetUs) {
            ++perf_over_budget_count_;
        }
        maybeReportPerformance(handler_end);
    }

    MaybeReportGe2DStats(this, handler_end);
    MaybeReportSunxiG2DStats(this, handler_end);
#endif
}

bool LvglPort::initialized() const {
    return initialized_;
}

const BufferDescriptor* LvglPort::resolveActiveBuffer(std::uint8_t* data) const {
    if (display_backend_ == nullptr || data == nullptr) {
        return nullptr;
    }

    const auto& primary = display_backend_->primaryBuffer();
    if (primary.data == data) {
        return &primary;
    }

    const auto* secondary = display_backend_->secondaryBuffer();
    if (secondary != nullptr && secondary->data == data) {
        return secondary;
    }

    return nullptr;
}

#if HMI_NEXUS_HAS_LVGL
void LvglPort::FlushCallback(lv_display_t* display,
                             const lv_area_t* area,
                             std::uint8_t* pixel_map) {
    (void)area;
    (void)pixel_map;

    auto* port = static_cast<LvglPort*>(lv_display_get_user_data(display));
    if (port != nullptr) {
        port->handleFlush();
    }

    lv_display_flush_ready(display);
}
#endif

void LvglPort::resetPerformanceWindow(std::chrono::steady_clock::time_point now) {
    perf_window_start_ = now;
    perf_pump_count_ = 0;
    perf_present_count_ = 0;
    perf_total_handler_time_us_ = 0;
    perf_max_handler_time_us_ = 0;
    perf_total_flush_time_us_ = 0;
    perf_max_flush_time_us_ = 0;
    perf_over_budget_count_ = 0;
}

void LvglPort::recordFlushDuration(std::uint64_t flush_time_us) {
    ++perf_present_count_;
    perf_total_flush_time_us_ += flush_time_us;
    perf_max_flush_time_us_ = std::max(perf_max_flush_time_us_, flush_time_us);
}

void LvglPort::maybeReportPerformance(std::chrono::steady_clock::time_point now) {
    if (!config_.perf_stats_enabled) {
        return;
    }

    if (config_.perf_report_interval_ms == 0U) {
        return;
    }

    if (perf_window_start_.time_since_epoch().count() == 0) {
        resetPerformanceWindow(now);
        return;
    }

    const auto elapsed_us =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       now - perf_window_start_)
                                       .count());
    const auto report_interval_us =
        static_cast<std::uint64_t>(config_.perf_report_interval_ms) * 1000U;
    if (elapsed_us < report_interval_us || perf_pump_count_ == 0U) {
        return;
    }

    const double elapsed_seconds = static_cast<double>(elapsed_us) / 1000000.0;
    const double pump_rate = static_cast<double>(perf_pump_count_) / elapsed_seconds;
    const double present_rate = static_cast<double>(perf_present_count_) / elapsed_seconds;
    const double avg_handler_ms =
        static_cast<double>(perf_total_handler_time_us_) /
        static_cast<double>(perf_pump_count_) / 1000.0;
    const double max_handler_ms = static_cast<double>(perf_max_handler_time_us_) / 1000.0;
    const double avg_flush_ms =
        perf_present_count_ == 0U
            ? 0.0
            : static_cast<double>(perf_total_flush_time_us_) /
                  static_cast<double>(perf_present_count_) / 1000.0;
    const double max_flush_ms = static_cast<double>(perf_max_flush_time_us_) / 1000.0;

    std::ostringstream stream;
    stream << "LVGL perf: pump=" << perf_pump_count_ << " (" << FormatMilliseconds(pump_rate)
           << "/s), present=" << perf_present_count_ << " (" << FormatMilliseconds(present_rate)
           << " fps), handler avg/max=" << FormatMilliseconds(avg_handler_ms) << '/'
           << FormatMilliseconds(max_handler_ms) << " ms, flush avg/max="
           << FormatMilliseconds(avg_flush_ms) << '/' << FormatMilliseconds(max_flush_ms)
           << " ms, over16.67ms=" << perf_over_budget_count_;
    system::Logger::Info("ui.lvgl.perf", stream.str());

    resetPerformanceWindow(now);
}

void LvglPort::handleFlush() {
#if HMI_NEXUS_HAS_LVGL
    auto* display = static_cast<lv_display_t*>(display_);
    if (display == nullptr || display_backend_ == nullptr) {
        return;
    }

    if (!lv_disp_flush_is_last(display)) {
        return;
    }

    lv_draw_buf_t* active_buffer = lv_display_get_buf_active(display);
    if (active_buffer == nullptr || active_buffer->data == nullptr) {
        system::Logger::Warn("ui.lvgl.display", "LVGL flush skipped: active draw buffer is null");
        return;
    }

    const auto* source = resolveActiveBuffer(active_buffer->data);
    if (source == nullptr) {
        system::Logger::Warn("ui.lvgl.display",
                             "LVGL flush skipped: active draw buffer is not managed by current backend");
        return;
    }

    SurfaceInfo render_surface = display_backend_->surface();
    render_surface.width = lv_display_get_horizontal_resolution(display);
    render_surface.height = lv_display_get_vertical_resolution(display);

    BufferDescriptor logical_source = *source;
    logical_source.stride = lv_draw_buf_width_to_stride(
        static_cast<uint32_t>(render_surface.width),
        ToLvglColorFormat(logical_source.pixel_format));

    render_surface.pixel_format = logical_source.pixel_format;
    render_surface.stride = logical_source.stride;

    const auto flush_start = std::chrono::steady_clock::now();
    const auto present_result = display_backend_->present(
        logical_source, render_surface, config_.rotation, accel_2d_backend_.get());
    if (!present_result) {
        system::Logger::Warn("ui.lvgl.display",
                             "LVGL flush fallback failed: " + present_result.message());
    }
    if (config_.perf_stats_enabled) {
        const auto flush_time_us =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now() - flush_start)
                                           .count());
        recordFlushDuration(flush_time_us);
    }
#endif
}

}  // namespace hmi_nexus::ui
