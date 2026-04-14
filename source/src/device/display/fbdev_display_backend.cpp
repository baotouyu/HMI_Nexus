#include "hmi_nexus/device/display/backend_factory.h"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/device/display/accel_2d_backend.h"
#include "hmi_nexus/device/display/display_backend.h"
#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_D211_GE2D
#include <dma_allocator.h>
#endif

#if HMI_NEXUS_HAS_SUNXI_G2D
#include <ion_mem_alloc.h>
#endif

#if HMI_NEXUS_HAS_LVGL
#include "lvgl.h"
#endif

namespace hmi_nexus::device::display {
namespace {

constexpr std::size_t kMaxFramebufferPages = 2;
constexpr std::size_t kInvalidPageIndex = static_cast<std::size_t>(-1);
constexpr unsigned long kFramebufferIoCacheSync = 0x4630UL;
constexpr unsigned long kFramebufferIoEnableCache = 0x4631UL;

enum class DrawBufferAllocationKind {
    kNone = 0,
    kD211DmaBuf,
    kSunxiIon,
};

#if HMI_NEXUS_HAS_LVGL
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

PixelFormat PixelFormatFromFramebufferBits(int bits_per_pixel) {
    switch (bits_per_pixel) {
    case 16:
        return PixelFormat::kRgb565;
    case 24:
        return PixelFormat::kRgb888;
    case 32:
        return PixelFormat::kArgb8888;
    default:
        return PixelFormat::kUnknown;
    }
}

std::string IoErrorMessage(const std::string& action, const std::string& path) {
    return action + " failed for " + path + ": " + std::strerror(errno);
}

void ConvertRgb565ToArgb8888(const std::uint8_t* source_row,
                             std::uint8_t* destination_row,
                             int width,
                             PixelFormat destination_pixel_format) {
    for (int x = 0; x < width; ++x) {
        const std::uint16_t packed =
            static_cast<std::uint16_t>(source_row[x * 2]) |
            static_cast<std::uint16_t>(source_row[x * 2 + 1] << 8);
        const std::uint8_t red = static_cast<std::uint8_t>(((packed >> 11) & 0x1F) * 255 / 31);
        const std::uint8_t green = static_cast<std::uint8_t>(((packed >> 5) & 0x3F) * 255 / 63);
        const std::uint8_t blue = static_cast<std::uint8_t>((packed & 0x1F) * 255 / 31);
        destination_row[x * 4] = blue;
        destination_row[x * 4 + 1] = green;
        destination_row[x * 4 + 2] = red;
        destination_row[x * 4 + 3] =
            destination_pixel_format == PixelFormat::kXrgb8888 ? 0x00 : 0xFF;
    }
}

class LinuxFbdevDisplayBackend final : public DisplayBackend {
public:
    ~LinuxFbdevDisplayBackend() override {
        ReleaseResources();
    }

    const char* name() const override {
        return "linux-fbdev";
    }

    common::Result initialize(const DisplayConfig& config) override {
        PixelFormat draw_buffer_pixel_format;
        const bool dma_requested = config.prefer_dma_draw_buffer;

        ReleaseResources();

        framebuffer_fd_ = open(config.device_path.c_str(), O_RDWR | O_CLOEXEC);
        if (framebuffer_fd_ < 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        IoErrorMessage("open framebuffer", config.device_path));
        }

        if (ioctl(framebuffer_fd_, FBIOGET_FSCREENINFO, &fix_info_) != 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        IoErrorMessage("FBIOGET_FSCREENINFO", config.device_path));
        }
        if (ioctl(framebuffer_fd_, FBIOGET_VSCREENINFO, &var_info_) != 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        IoErrorMessage("FBIOGET_VSCREENINFO", config.device_path));
        }

        surface_.width = static_cast<int>(var_info_.xres);
        surface_.height = static_cast<int>(var_info_.yres);
        surface_.dpi = config.dpi;
        surface_.pixel_format = PixelFormatFromFramebufferBits(var_info_.bits_per_pixel);
        surface_.stride = static_cast<std::size_t>(fix_info_.line_length);
        if (!surface_.valid()) {
            return common::Result::Fail(common::ErrorCode::kUnsupported,
                                        "unsupported framebuffer pixel format: " +
                                            std::to_string(var_info_.bits_per_pixel) + " bpp");
        }

        if (config.requested_width > 0 && config.requested_height > 0 &&
            (config.requested_width != surface_.width ||
             config.requested_height != surface_.height)) {
            system::Logger::Warn(
                "ui.lvgl.display",
                "Ignoring ui.display.width/height for framebuffer backend; using /dev/fb* mode " +
                    std::to_string(surface_.width) + "x" + std::to_string(surface_.height));
        }
        if (config.draw_buffer_lines > 0 &&
            config.draw_buffer_lines < static_cast<std::size_t>(surface_.height)) {
            system::Logger::Info(
                "ui.lvgl.display",
                "ui.display.draw_buffer_lines is ignored by the current framebuffer direct/full-frame backend");
        }

        framebuffer_size_ = surface_.stride * static_cast<std::size_t>(surface_.height);
        framebuffer_map_size_ = static_cast<std::size_t>(fix_info_.smem_len);
        if (framebuffer_map_size_ < framebuffer_size_) {
            return common::Result::Fail(common::ErrorCode::kUnsupported,
                                        "framebuffer memory is smaller than one visible page");
        }

        framebuffer_map_ = static_cast<std::uint8_t*>(
            mmap(nullptr,
                 framebuffer_map_size_,
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 framebuffer_fd_,
                 0));
        if (framebuffer_map_ == MAP_FAILED) {
            framebuffer_map_ = nullptr;
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        IoErrorMessage("mmap framebuffer", config.device_path));
        }

        framebuffer_page_count_ =
            (framebuffer_map_size_ >= framebuffer_size_ * kMaxFramebufferPages &&
             static_cast<std::size_t>(var_info_.yres_virtual) >=
                 static_cast<std::size_t>(var_info_.yres) * kMaxFramebufferPages)
                ? kMaxFramebufferPages
                : 1U;
        for (std::size_t index = 0; index < framebuffer_page_count_; ++index) {
            framebuffer_pages_[index] = MakeFramebufferPage(index);
        }
        visible_page_index_ =
            (framebuffer_page_count_ > 1 && var_info_.yoffset >= var_info_.yres) ? 1U : 0U;
        if (visible_page_index_ >= framebuffer_page_count_) {
            visible_page_index_ = 0;
        }
        MaybeEnableFramebufferCache();

        draw_buffer_pixel_format =
            config.draw_buffer_pixel_format == PixelFormat::kUnknown
                ? surface_.pixel_format
                : config.draw_buffer_pixel_format;
        if (draw_buffer_pixel_format != surface_.pixel_format && !dma_requested) {
            system::Logger::Warn("ui.lvgl.display",
                                 "Offscreen format conversion requires DMA-backed draw buffers; "
                                 "falling back to framebuffer native format");
            draw_buffer_pixel_format = surface_.pixel_format;
        }

        direct_scanout_enabled_ =
            config.rotation == Rotation::k0 && draw_buffer_pixel_format == surface_.pixel_format;
        if (direct_scanout_enabled_) {
            if (framebuffer_page_count_ > 1) {
                draw_buffer_ = framebuffer_pages_[visible_page_index_ == 0 ? 1 : 0];
                secondary_draw_buffer_ = framebuffer_pages_[visible_page_index_];
            } else {
                draw_buffer_ = framebuffer_pages_[0];
                secondary_draw_buffer_ = {};
            }
        } else {
            const std::size_t buffer_stride =
                ComputeStride(surface_.width, draw_buffer_pixel_format);
            const std::size_t buffer_size =
                buffer_stride * static_cast<std::size_t>(surface_.height);
            const bool allow_secondary_offscreen_draw_buffer =
                config.rotation == Rotation::k0;

            if (dma_requested && TryAllocateDmaDrawBuffer(buffer_size,
                                                          buffer_stride,
                                                          draw_buffer_pixel_format,
                                                          &draw_buffer_,
                                                          &dma_draw_buffer_fd_,
                                                          &dma_draw_buffer_map_,
                                                          &draw_buffer_allocation_kind_)) {
                system::Logger::Info("ui.lvgl.display",
                                     "Allocated DMA-backed LVGL draw buffer for framebuffer backend");
                if (allow_secondary_offscreen_draw_buffer &&
                    framebuffer_page_count_ > 1 &&
                    TryAllocateDmaDrawBuffer(buffer_size,
                                             buffer_stride,
                                             draw_buffer_pixel_format,
                                             &secondary_draw_buffer_,
                                             &secondary_dma_draw_buffer_fd_,
                                             &secondary_dma_draw_buffer_map_,
                                             &secondary_draw_buffer_allocation_kind_)) {
                    system::Logger::Info("ui.lvgl.display",
                                         "Allocated secondary DMA-backed LVGL draw buffer");
                } else if (!allow_secondary_offscreen_draw_buffer) {
                    system::Logger::Info(
                        "ui.lvgl.display",
                        "Rotation path keeps a single DMA offscreen draw buffer to match the "
                        "official fbdev pipeline");
                }
            } else {
                if (draw_buffer_pixel_format != surface_.pixel_format) {
                    system::Logger::Warn(
                        "ui.lvgl.display",
                        "RGB565 offscreen draw buffer allocation failed; falling back to "
                        "framebuffer native format");
                    draw_buffer_pixel_format = surface_.pixel_format;
                }

                const std::size_t fallback_stride =
                    ComputeStride(surface_.width, draw_buffer_pixel_format);
                const std::size_t fallback_size =
                    fallback_stride * static_cast<std::size_t>(surface_.height);
                host_draw_buffer_.assign(fallback_size, 0);
                draw_buffer_.data = host_draw_buffer_.data();
                draw_buffer_.size = host_draw_buffer_.size();
                draw_buffer_.stride = fallback_stride;
                draw_buffer_.pixel_format = draw_buffer_pixel_format;
                draw_buffer_.memory_type = BufferMemoryType::kHost;
                secondary_draw_buffer_ = {};
            }
        }

        if (config.rotation != Rotation::k0) {
            if (draw_buffer_.memory_type == BufferMemoryType::kDmaBuf) {
                system::Logger::Info("ui.lvgl.display",
                                     "Framebuffer rotation will use the GE2D DMA offscreen path");
            } else {
                system::Logger::Warn("ui.lvgl.display",
                                     "Framebuffer rotation will fall back to software because a DMA "
                                     "offscreen draw buffer is unavailable");
            }
        }

        async_page_flip_enabled_ = !direct_scanout_enabled_ && framebuffer_page_count_ > 1;
        if (async_page_flip_enabled_) {
            try {
                StartAsyncPageFlipWaiter();
            } catch (const std::system_error& error) {
                async_page_flip_enabled_ = false;
                return common::Result::Fail(common::ErrorCode::kInternalError,
                                            "failed to start async page-flip waiter: " +
                                                std::string(error.what()));
            }
        }

        system::Logger::Info("ui.lvgl.display",
                             "Using framebuffer backend on " + config.device_path + ": " +
                                 std::to_string(surface_.width) + "x" +
                                 std::to_string(surface_.height) + " " +
                                 PixelFormatName(surface_.pixel_format) + ", render=" +
                                 PixelFormatName(draw_buffer_.pixel_format) + ", mode=" +
                                 (direct_scanout_enabled_
                                      ? (framebuffer_page_count_ > 1 ? "direct-page-flip"
                                                                     : "direct-single-buffer")
                                      : (framebuffer_page_count_ > 1 ? "offscreen-page-flip-async"
                                                                     : "offscreen-blit-single-buffer")));
        return common::Result::Ok();
    }

    const SurfaceInfo& surface() const override {
        return surface_;
    }

    const BufferDescriptor& primaryBuffer() const override {
        return draw_buffer_;
    }

    const BufferDescriptor* secondaryBuffer() const override {
        return secondary_draw_buffer_.valid() ? &secondary_draw_buffer_ : nullptr;
    }

    common::Result present(const BufferDescriptor& source,
                           const SurfaceInfo& render_surface,
                           Rotation rotation,
                           Accel2DBackend* accel_2d) override {
        if (!source.valid()) {
            return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                        "framebuffer present source buffer is invalid");
        }

        if (direct_scanout_enabled_) {
            return PresentDirectFramebuffer(source);
        }

        const std::size_t target_page_index =
            framebuffer_page_count_ > 1 ? (visible_page_index_ == 0 ? 1 : 0) : 0;
        const BufferDescriptor& destination = framebuffer_pages_[target_page_index];
        bool cpu_wrote_destination = false;

        if (async_page_flip_enabled_) {
            auto wait_result = WaitForAsyncPageFlipReady();
            if (!wait_result) {
                return wait_result;
            }
        } else if (framebuffer_page_count_ <= 1) {
            auto sync_result = WaitForVSync();
            if (!sync_result) {
                return sync_result;
            }
        }

        if (accel_2d != nullptr &&
            accel_2d->canBlit(source, destination, render_surface, rotation)) {
            auto blit_result = accel_2d->blit(source, destination, render_surface, rotation);
            if (!blit_result) {
                return blit_result;
            }
        } else {
#if HMI_NEXUS_HAS_LVGL
            if (rotation != Rotation::k0 &&
                source.pixel_format == destination.pixel_format) {
                lv_draw_sw_rotate(source.data,
                                  destination.data,
                                  render_surface.width,
                                  render_surface.height,
                                  static_cast<int32_t>(source.stride),
                                  static_cast<int32_t>(destination.stride),
                                  ToLvglRotation(rotation),
                                  ToLvglColorFormat(source.pixel_format));
            } else {
#else
            if (rotation != Rotation::k0) {
                return common::Result::Fail(common::ErrorCode::kUnsupported,
                                            "software rotation requires LVGL support");
            } else {
#endif

                const std::size_t src_row_bytes =
                    ComputeStride(render_surface.width, source.pixel_format);
                const std::size_t dst_row_bytes =
                    ComputeStride(render_surface.width, destination.pixel_format);
                const std::size_t src_stride = render_surface.stride != 0
                                                   ? render_surface.stride
                                                   : (source.stride != 0 ? source.stride
                                                                         : src_row_bytes);
                const std::size_t dst_stride =
                    destination.stride != 0 ? destination.stride : dst_row_bytes;

                if (rotation != Rotation::k0) {
                    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                                "software rotation does not support format conversion");
                }

                if (source.pixel_format == destination.pixel_format) {
                    const std::size_t copy_bytes =
                        std::min(src_row_bytes, std::min(src_stride, dst_stride));
                    for (int y = 0; y < render_surface.height; ++y) {
                        std::memcpy(destination.data + static_cast<std::size_t>(y) * dst_stride,
                                    source.data + static_cast<std::size_t>(y) * src_stride,
                                    copy_bytes);
                    }
                } else if (source.pixel_format == PixelFormat::kRgb565 &&
                           (destination.pixel_format == PixelFormat::kArgb8888 ||
                            destination.pixel_format == PixelFormat::kXrgb8888)) {
                    for (int y = 0; y < render_surface.height; ++y) {
                        ConvertRgb565ToArgb8888(
                            source.data + static_cast<std::size_t>(y) * src_stride,
                            destination.data + static_cast<std::size_t>(y) * dst_stride,
                            render_surface.width,
                            destination.pixel_format);
                    }
                } else {
                    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                                "software present does not support this format conversion");
                }
            }

            cpu_wrote_destination = true;
        }

        if (framebuffer_page_count_ > 1) {
            return PresentFramebufferPage(
                target_page_index, !async_page_flip_enabled_, cpu_wrote_destination);
        }

        if (cpu_wrote_destination) {
            SyncFramebufferPageCache(target_page_index);
        }

        return common::Result::Ok();
    }

private:
    BufferDescriptor MakeFramebufferPage(std::size_t page_index) const {
        BufferDescriptor page{};
        if (framebuffer_map_ == nullptr || framebuffer_size_ == 0 ||
            page_index >= framebuffer_page_count_) {
            return page;
        }

        const std::size_t offset = framebuffer_size_ * page_index;
        page.data = framebuffer_map_ + offset;
        page.size = framebuffer_size_;
        page.stride = surface_.stride;
        page.pixel_format = surface_.pixel_format;
        page.physical_address = static_cast<std::uintptr_t>(fix_info_.smem_start) + offset;
        page.memory_type = BufferMemoryType::kPhysical;
        return page;
    }

    std::size_t ResolveFramebufferPageIndex(const std::uint8_t* data) const {
        for (std::size_t index = 0; index < framebuffer_page_count_; ++index) {
            if (framebuffer_pages_[index].data == data) {
                return index;
            }
        }
        return kInvalidPageIndex;
    }

    common::Result WaitForVSync() {
        if (!vsync_wait_enabled_) {
            return common::Result::Ok();
        }

        std::uint32_t zero = 0;
        if (ioctl(framebuffer_fd_, FBIO_WAITFORVSYNC, &zero) == 0) {
            return common::Result::Ok();
        }

        vsync_wait_enabled_ = false;
        system::Logger::Warn("ui.lvgl.display",
                             "Framebuffer VSYNC wait is unavailable, continue without explicit sync: " +
                                 std::string(std::strerror(errno)));
        return common::Result::Ok();
    }

    void StartAsyncPageFlipWaiter() {
        async_page_flip_shutdown_ = false;
        async_page_flip_pending_ = false;
        async_page_flip_ready_ = true;
        async_page_flip_thread_ = std::thread([this]() { AsyncPageFlipThreadMain(); });
    }

    void StopAsyncPageFlipWaiter() {
        {
            std::lock_guard<std::mutex> lock(async_page_flip_mutex_);
            async_page_flip_shutdown_ = true;
            async_page_flip_pending_ = false;
            async_page_flip_ready_ = true;
        }
        async_page_flip_cv_.notify_all();
        async_page_flip_ready_cv_.notify_all();
        if (async_page_flip_thread_.joinable()) {
            async_page_flip_thread_.join();
        }
        async_page_flip_shutdown_ = false;
        async_page_flip_pending_ = false;
        async_page_flip_ready_ = true;
    }

    void AsyncPageFlipThreadMain() {
        std::unique_lock<std::mutex> lock(async_page_flip_mutex_);
        while (true) {
            async_page_flip_cv_.wait(lock, [this]() {
                return async_page_flip_pending_ || async_page_flip_shutdown_;
            });
            if (async_page_flip_shutdown_) {
                break;
            }

            async_page_flip_pending_ = false;
            lock.unlock();
            WaitForVSync();
            lock.lock();

            async_page_flip_ready_ = true;
            async_page_flip_ready_cv_.notify_all();
        }
    }

    common::Result WaitForAsyncPageFlipReady() {
        std::unique_lock<std::mutex> lock(async_page_flip_mutex_);
        async_page_flip_ready_cv_.wait(lock, [this]() {
            return async_page_flip_ready_ || async_page_flip_shutdown_;
        });
        if (async_page_flip_shutdown_) {
            return common::Result::Fail(common::ErrorCode::kInternalError,
                                        "async page-flip waiter stopped unexpectedly");
        }
        return common::Result::Ok();
    }

    common::Result PresentFramebufferPage(std::size_t page_index,
                                          bool wait_for_vsync,
                                          bool cpu_wrote_page) {
        if (page_index >= framebuffer_page_count_) {
            return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                        "framebuffer page index is out of range");
        }

        if (cpu_wrote_page) {
            SyncFramebufferPageCache(page_index);
        }

        fb_var_screeninfo var = var_info_;
        var.xoffset = 0;
        var.yoffset = static_cast<__u32>(page_index * static_cast<std::size_t>(var_info_.yres));
        if (ioctl(framebuffer_fd_, FBIOPAN_DISPLAY, &var) != 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "FBIOPAN_DISPLAY failed: " +
                                            std::string(std::strerror(errno)));
        }

        visible_page_index_ = page_index;
        if (async_page_flip_enabled_ && !wait_for_vsync) {
            {
                std::lock_guard<std::mutex> lock(async_page_flip_mutex_);
                async_page_flip_ready_ = false;
                async_page_flip_pending_ = true;
            }
            async_page_flip_cv_.notify_one();
            return common::Result::Ok();
        }
        return WaitForVSync();
    }

    common::Result PresentDirectFramebuffer(const BufferDescriptor& source) {
        const std::size_t source_page_index = ResolveFramebufferPageIndex(source.data);
        if (source_page_index == kInvalidPageIndex) {
            return common::Result::Fail(
                common::ErrorCode::kInvalidArgument,
                "direct framebuffer present source is not one of the mapped scanout pages");
        }

        return PresentFramebufferPage(source_page_index, true, true);
    }

    void MaybeEnableFramebufferCache() {
        if (framebuffer_fd_ < 0 || framebuffer_cache_enabled_ ||
            !framebuffer_cache_control_supported_) {
            return;
        }

        std::uintptr_t args[2] = {1U, 0U};
        if (ioctl(framebuffer_fd_, kFramebufferIoEnableCache, args) == 0) {
            framebuffer_cache_enabled_ = true;
            system::Logger::Info("ui.lvgl.display",
                                 "Framebuffer vendor cache control is enabled");
            return;
        }

        framebuffer_cache_control_supported_ = false;
    }

    void DisableFramebufferCache() {
        if (framebuffer_fd_ < 0 || !framebuffer_cache_enabled_) {
            return;
        }

        std::uintptr_t args[2] = {0U, 0U};
        ioctl(framebuffer_fd_, kFramebufferIoEnableCache, args);
        framebuffer_cache_enabled_ = false;
    }

    void SyncFramebufferPageCache(std::size_t page_index) {
        if (!framebuffer_cache_enabled_ || !framebuffer_cache_sync_supported_ ||
            page_index >= framebuffer_page_count_) {
            return;
        }

        const auto& page = framebuffer_pages_[page_index];
        std::uintptr_t args[2] = {
            reinterpret_cast<std::uintptr_t>(page.data),
            static_cast<std::uintptr_t>(framebuffer_size_),
        };
        if (ioctl(framebuffer_fd_, kFramebufferIoCacheSync, args) == 0) {
            return;
        }

        framebuffer_cache_sync_supported_ = false;
        system::Logger::Warn(
            "ui.lvgl.display",
            "Framebuffer cache sync ioctl is unavailable, continue without vendor cache flush: " +
                std::string(std::strerror(errno)));
    }

    bool TryAllocateDmaDrawBuffer(std::size_t buffer_size,
                                  std::size_t buffer_stride,
                                  PixelFormat pixel_format,
                                  BufferDescriptor* draw_buffer,
                                  int* dma_fd,
                                  std::uint8_t** dma_map,
                                  DrawBufferAllocationKind* allocation_kind) {
        if (draw_buffer == nullptr || dma_fd == nullptr || dma_map == nullptr ||
            allocation_kind == nullptr) {
            return false;
        }

        *allocation_kind = DrawBufferAllocationKind::kNone;
#if HMI_NEXUS_HAS_D211_GE2D
        dma_heap_fd_ = dmabuf_device_open();
        if (dma_heap_fd_ < 0) {
        } else {
            *dma_fd = dmabuf_alloc(dma_heap_fd_, static_cast<int>(buffer_size));
            if (*dma_fd >= 0) {
                *dma_map = dmabuf_mmap(*dma_fd, static_cast<int>(buffer_size));
                if (*dma_map != nullptr && *dma_map != MAP_FAILED) {
                    dmabuf_device_close(dma_heap_fd_);
                    dma_heap_fd_ = -1;

                    draw_buffer->data = *dma_map;
                    draw_buffer->size = buffer_size;
                    draw_buffer->stride = buffer_stride;
                    draw_buffer->pixel_format = pixel_format;
                    draw_buffer->dma_fd = *dma_fd;
                    draw_buffer->memory_type = BufferMemoryType::kDmaBuf;
                    *allocation_kind = DrawBufferAllocationKind::kD211DmaBuf;
                    return true;
                }

                *dma_map = nullptr;
                dmabuf_free(*dma_fd);
                *dma_fd = -1;
            }

            dmabuf_device_close(dma_heap_fd_);
            dma_heap_fd_ = -1;
        }
#endif

#if HMI_NEXUS_HAS_SUNXI_G2D
        if (!EnsureSunxiMemReady()) {
            return false;
        }

        void* allocation = SunxiMemPalloc(sunxi_memops_, static_cast<int>(buffer_size));
        if (allocation == nullptr) {
            return false;
        }

        const auto physical_address =
            reinterpret_cast<std::uintptr_t>(SunxiMemGetPhysicAddressCpu(sunxi_memops_, allocation));
        if (physical_address == 0U) {
            SunxiMemPfree(sunxi_memops_, allocation);
            return false;
        }

        const int share_fd = SunxiMemGetBufferFd(sunxi_memops_, allocation);
        *dma_map = static_cast<std::uint8_t*>(allocation);
        *dma_fd = share_fd;
        draw_buffer->data = *dma_map;
        draw_buffer->size = buffer_size;
        draw_buffer->stride = buffer_stride;
        draw_buffer->pixel_format = pixel_format;
        draw_buffer->dma_fd = share_fd;
        draw_buffer->physical_address = physical_address;
        draw_buffer->memory_type =
            share_fd >= 0 ? BufferMemoryType::kDmaBuf : BufferMemoryType::kPhysical;
        *allocation_kind = DrawBufferAllocationKind::kSunxiIon;
        return true;
#endif

        (void)buffer_size;
        (void)buffer_stride;
        (void)pixel_format;
        return false;
    }

#if HMI_NEXUS_HAS_SUNXI_G2D
    bool EnsureSunxiMemReady() {
        if (sunxi_memops_ != nullptr) {
            return true;
        }

        sunxi_memops_ = GetMemAdapterOpsS();
        if (sunxi_memops_ == nullptr) {
            return false;
        }

        if (SunxiMemOpen(sunxi_memops_) < 0) {
            sunxi_memops_ = nullptr;
            return false;
        }

        return true;
    }
#endif

    void ReleaseResources() {
        if (async_page_flip_enabled_ || async_page_flip_thread_.joinable()) {
            StopAsyncPageFlipWaiter();
        }
        DisableFramebufferCache();
#if HMI_NEXUS_HAS_D211_GE2D
        if (draw_buffer_allocation_kind_ == DrawBufferAllocationKind::kD211DmaBuf &&
            dma_draw_buffer_map_ != nullptr) {
            dmabuf_munmap(dma_draw_buffer_map_, static_cast<int>(draw_buffer_.size));
            dma_draw_buffer_map_ = nullptr;
        }
        if (draw_buffer_allocation_kind_ == DrawBufferAllocationKind::kD211DmaBuf &&
            dma_draw_buffer_fd_ >= 0) {
            dmabuf_free(dma_draw_buffer_fd_);
            dma_draw_buffer_fd_ = -1;
        }
        if (dma_heap_fd_ >= 0) {
            dmabuf_device_close(dma_heap_fd_);
            dma_heap_fd_ = -1;
        }
        if (secondary_draw_buffer_allocation_kind_ == DrawBufferAllocationKind::kD211DmaBuf &&
            secondary_dma_draw_buffer_map_ != nullptr) {
            dmabuf_munmap(secondary_dma_draw_buffer_map_,
                          static_cast<int>(secondary_draw_buffer_.size));
            secondary_dma_draw_buffer_map_ = nullptr;
        }
        if (secondary_draw_buffer_allocation_kind_ == DrawBufferAllocationKind::kD211DmaBuf &&
            secondary_dma_draw_buffer_fd_ >= 0) {
            dmabuf_free(secondary_dma_draw_buffer_fd_);
            secondary_dma_draw_buffer_fd_ = -1;
        }
#endif
#if HMI_NEXUS_HAS_SUNXI_G2D
        if (sunxi_memops_ != nullptr) {
            if (draw_buffer_allocation_kind_ == DrawBufferAllocationKind::kSunxiIon &&
                dma_draw_buffer_map_ != nullptr) {
                SunxiMemPfree(sunxi_memops_, dma_draw_buffer_map_);
                dma_draw_buffer_map_ = nullptr;
                dma_draw_buffer_fd_ = -1;
            }
            if (secondary_draw_buffer_allocation_kind_ == DrawBufferAllocationKind::kSunxiIon &&
                secondary_dma_draw_buffer_map_ != nullptr) {
                SunxiMemPfree(sunxi_memops_, secondary_dma_draw_buffer_map_);
                secondary_dma_draw_buffer_map_ = nullptr;
                secondary_dma_draw_buffer_fd_ = -1;
            }
            SunxiMemClose(sunxi_memops_);
            sunxi_memops_ = nullptr;
        }
#endif
        host_draw_buffer_.clear();
        secondary_host_draw_buffer_.clear();
        draw_buffer_ = {};
        secondary_draw_buffer_ = {};
        draw_buffer_allocation_kind_ = DrawBufferAllocationKind::kNone;
        secondary_draw_buffer_allocation_kind_ = DrawBufferAllocationKind::kNone;
        direct_scanout_enabled_ = false;
        async_page_flip_enabled_ = false;
        framebuffer_cache_enabled_ = false;
        framebuffer_cache_control_supported_ = true;
        framebuffer_cache_sync_supported_ = true;
        visible_page_index_ = 0;
        framebuffer_page_count_ = 0;
        vsync_wait_enabled_ = true;
        for (auto& page : framebuffer_pages_) {
            page = {};
        }
        surface_ = {};

        if (framebuffer_map_ != nullptr) {
            munmap(framebuffer_map_, framebuffer_map_size_);
            framebuffer_map_ = nullptr;
        }
        framebuffer_size_ = 0;
        framebuffer_map_size_ = 0;

        if (framebuffer_fd_ >= 0) {
            close(framebuffer_fd_);
            framebuffer_fd_ = -1;
        }

        std::memset(&fix_info_, 0, sizeof(fix_info_));
        std::memset(&var_info_, 0, sizeof(var_info_));
    }

    int framebuffer_fd_ = -1;
    std::size_t framebuffer_size_ = 0;
    std::size_t framebuffer_map_size_ = 0;
    std::size_t framebuffer_page_count_ = 0;
    std::size_t visible_page_index_ = 0;
    std::uint8_t* framebuffer_map_ = nullptr;
    fb_fix_screeninfo fix_info_{};
    fb_var_screeninfo var_info_{};
    SurfaceInfo surface_{};
    BufferDescriptor framebuffer_pages_[kMaxFramebufferPages]{};
    BufferDescriptor draw_buffer_{};
    BufferDescriptor secondary_draw_buffer_{};
    bool direct_scanout_enabled_ = false;
    bool async_page_flip_enabled_ = false;
    bool framebuffer_cache_enabled_ = false;
    bool framebuffer_cache_control_supported_ = true;
    bool framebuffer_cache_sync_supported_ = true;
    bool vsync_wait_enabled_ = true;
    std::thread async_page_flip_thread_{};
    std::mutex async_page_flip_mutex_{};
    std::condition_variable async_page_flip_cv_{};
    std::condition_variable async_page_flip_ready_cv_{};
    bool async_page_flip_shutdown_ = false;
    bool async_page_flip_pending_ = false;
    bool async_page_flip_ready_ = true;
    std::vector<std::uint8_t> host_draw_buffer_;
    std::vector<std::uint8_t> secondary_host_draw_buffer_;
    int dma_heap_fd_ = -1;
    int dma_draw_buffer_fd_ = -1;
    std::uint8_t* dma_draw_buffer_map_ = nullptr;
    int secondary_dma_draw_buffer_fd_ = -1;
    std::uint8_t* secondary_dma_draw_buffer_map_ = nullptr;
    DrawBufferAllocationKind draw_buffer_allocation_kind_ = DrawBufferAllocationKind::kNone;
    DrawBufferAllocationKind secondary_draw_buffer_allocation_kind_ =
        DrawBufferAllocationKind::kNone;
#if HMI_NEXUS_HAS_SUNXI_G2D
    struct SunxiMemOpsS* sunxi_memops_ = nullptr;
#endif
};

}  // namespace

std::unique_ptr<DisplayBackend> CreateLinuxFbdevDisplayBackend() {
    return std::make_unique<LinuxFbdevDisplayBackend>();
}

}  // namespace hmi_nexus::device::display
