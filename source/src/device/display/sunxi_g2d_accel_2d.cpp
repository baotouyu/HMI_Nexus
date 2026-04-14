#include "hmi_nexus/device/display/backend_factory.h"

#include <cstring>
#include <memory>
#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/device/display/accel_2d_backend.h"
#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_SUNXI_G2D
#include <ion_mem_alloc.h>
#include <g2d_driver_enh.h>
#endif

namespace hmi_nexus::device::display {
namespace {

#if HMI_NEXUS_HAS_SUNXI_G2D

g2d_fmt_enh ToSunxiPixelFormat(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::kRgb565:
        return G2D_FORMAT_RGB565;
    case PixelFormat::kRgb888:
        return G2D_FORMAT_RGB888;
    case PixelFormat::kArgb8888:
        return G2D_FORMAT_ARGB8888;
    case PixelFormat::kXrgb8888:
        return G2D_FORMAT_XRGB8888;
    case PixelFormat::kUnknown:
        break;
    }
    return G2D_FORMAT_MAX;
}

g2d_blt_flags_h ToSunxiRotationFlags(Rotation rotation) {
    switch (rotation) {
    case Rotation::k0:
        return G2D_ROT_0;
    case Rotation::k90:
        return G2D_ROT_270;
    case Rotation::k180:
        return G2D_ROT_180;
    case Rotation::k270:
        return G2D_ROT_90;
    }
    return G2D_ROT_0;
}

class SunxiG2DAccel2DBackend final : public Accel2DBackend {
public:
    ~SunxiG2DAccel2DBackend() override {
        if (g2d_fd_ >= 0) {
            close(g2d_fd_);
            g2d_fd_ = -1;
        }
        if (memops_ != nullptr) {
            SunxiMemClose(memops_);
            memops_ = nullptr;
        }
    }

    const char* name() const override {
        return "sunxi-g2d";
    }

    common::Result initialize(const SurfaceInfo& surface) override {
        surface_ = surface;
        if (!surface_.valid()) {
            return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                        "invalid framebuffer surface for Sunxi G2D");
        }

        if (memops_ == nullptr) {
            memops_ = GetMemAdapterOpsS();
            if (memops_ == nullptr || SunxiMemOpen(memops_) < 0) {
                memops_ = nullptr;
                return common::Result::Fail(common::ErrorCode::kInternalError,
                                            "failed to open Sunxi ION memory adapter");
            }
        }

        if (g2d_fd_ < 0) {
            g2d_fd_ = open("/dev/g2d", O_RDWR | O_CLOEXEC);
        }
        if (g2d_fd_ < 0) {
            return common::Result::Fail(common::ErrorCode::kInternalError,
                                        "failed to open /dev/g2d");
        }

        dst_format_ = ToSunxiPixelFormat(surface_.pixel_format);
        if (dst_format_ == G2D_FORMAT_MAX) {
            return common::Result::Fail(common::ErrorCode::kUnsupported,
                                        "unsupported framebuffer format for Sunxi G2D");
        }

        return common::Result::Ok();
    }

    bool canBlit(const BufferDescriptor& source,
                 const BufferDescriptor& destination,
                 const SurfaceInfo& render_surface,
                 Rotation /*rotation*/) const override {
        return g2d_fd_ >= 0 &&
               memops_ != nullptr &&
               render_surface.valid() &&
               source.data != nullptr &&
               source.physical_address != 0 &&
               destination.physical_address != 0 &&
               source.pixel_format == destination.pixel_format &&
               ToSunxiPixelFormat(source.pixel_format) != G2D_FORMAT_MAX &&
               ToSunxiPixelFormat(destination.pixel_format) != G2D_FORMAT_MAX;
    }

    common::Result blit(const BufferDescriptor& source,
                        const BufferDescriptor& destination,
                        const SurfaceInfo& render_surface,
                        Rotation rotation) override {
        if (!canBlit(source, destination, render_surface, rotation)) {
            return common::Result::Fail(common::ErrorCode::kUnsupported,
                                        "Sunxi G2D cannot blit the current buffers");
        }

        const std::size_t source_stride =
            render_surface.stride != 0 ? render_surface.stride : source.stride;
        const std::size_t source_size =
            source_stride * static_cast<std::size_t>(render_surface.height);
        if (source.data != nullptr && source_size != 0U) {
            SunxiMemFlushCache(memops_, source.data, static_cast<int>(source_size));
        }

        g2d_blt_h blit;
        std::memset(&blit, 0, sizeof(blit));
        blit.flag_h = ToSunxiRotationFlags(rotation);

        blit.src_image_h.width = render_surface.width;
        blit.src_image_h.height = render_surface.height;
        blit.src_image_h.clip_rect.x = 0;
        blit.src_image_h.clip_rect.y = 0;
        blit.src_image_h.clip_rect.w = render_surface.width;
        blit.src_image_h.clip_rect.h = render_surface.height;
        blit.src_image_h.laddr[0] = static_cast<unsigned long>(source.physical_address);
        blit.src_image_h.format = ToSunxiPixelFormat(source.pixel_format);
        blit.src_image_h.mode = G2D_GLOBAL_ALPHA;
        blit.src_image_h.alpha = 255;
        blit.src_image_h.align[0] = 0;
        blit.src_image_h.align[1] = 0;
        blit.src_image_h.align[2] = 0;
        blit.src_image_h.use_phy_addr = 1;

        blit.dst_image_h.format = dst_format_;
        if (rotation == Rotation::k90 || rotation == Rotation::k270) {
            blit.dst_image_h.width = render_surface.height;
            blit.dst_image_h.height = render_surface.width;
        } else {
            blit.dst_image_h.width = render_surface.width;
            blit.dst_image_h.height = render_surface.height;
        }
        blit.dst_image_h.clip_rect.x = 0;
        blit.dst_image_h.clip_rect.y = 0;
        blit.dst_image_h.clip_rect.w = blit.dst_image_h.width;
        blit.dst_image_h.clip_rect.h = blit.dst_image_h.height;
        blit.dst_image_h.mode = G2D_GLOBAL_ALPHA;
        blit.dst_image_h.alpha = 255;
        blit.dst_image_h.align[0] = 0;
        blit.dst_image_h.align[1] = 0;
        blit.dst_image_h.align[2] = 0;
        blit.dst_image_h.laddr[0] = static_cast<unsigned long>(destination.physical_address);
        blit.dst_image_h.use_phy_addr = 1;

        if (ioctl(g2d_fd_, G2D_CMD_BITBLT_H, reinterpret_cast<unsigned long>(&blit)) < 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "G2D_CMD_BITBLT_H failed");
        }

        return common::Result::Ok();
    }

private:
    SurfaceInfo surface_{};
    struct SunxiMemOpsS* memops_ = nullptr;
    int g2d_fd_ = -1;
    g2d_fmt_enh dst_format_ = G2D_FORMAT_MAX;
};

#else

class SunxiG2DAccel2DBackend final : public Accel2DBackend {
public:
    const char* name() const override {
        return "sunxi-g2d";
    }

    common::Result initialize(const SurfaceInfo& /*surface*/) override {
        return common::Result::Fail(common::ErrorCode::kUnsupported,
                                    "Sunxi G2D support is not built");
    }

    bool canBlit(const BufferDescriptor& /*source*/,
                 const BufferDescriptor& /*destination*/,
                 const SurfaceInfo& /*render_surface*/,
                 Rotation /*rotation*/) const override {
        return false;
    }

    common::Result blit(const BufferDescriptor& /*source*/,
                        const BufferDescriptor& /*destination*/,
                        const SurfaceInfo& /*render_surface*/,
                        Rotation /*rotation*/) override {
        return common::Result::Fail(common::ErrorCode::kUnsupported,
                                    "Sunxi G2D support is not built");
    }
};

#endif

}  // namespace

std::unique_ptr<Accel2DBackend> CreateSunxiG2DAccel2DBackend() {
    return std::make_unique<SunxiG2DAccel2DBackend>();
}

}  // namespace hmi_nexus::device::display
