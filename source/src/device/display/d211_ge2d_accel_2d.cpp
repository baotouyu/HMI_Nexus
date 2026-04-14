#include "hmi_nexus/device/display/backend_factory.h"

#include <cstring>
#include <memory>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/device/display/accel_2d_backend.h"
#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_D211_GE2D
#include <dma_allocator.h>
#include <mpp_ge.h>
#endif

namespace hmi_nexus::device::display {
namespace {

#if HMI_NEXUS_HAS_D211_GE2D

enum mpp_pixel_format ToMppPixelFormat(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::kRgb565:
        return MPP_FMT_RGB_565;
    case PixelFormat::kRgb888:
        return MPP_FMT_RGB_888;
    case PixelFormat::kArgb8888:
        return MPP_FMT_ARGB_8888;
    case PixelFormat::kXrgb8888:
        return MPP_FMT_XRGB_8888;
    case PixelFormat::kUnknown:
        break;
    }
    return MPP_FMT_MAX;
}

unsigned int ToMppRotationFlags(Rotation rotation) {
    switch (rotation) {
    case Rotation::k0:
        return MPP_ROTATION_0;
    case Rotation::k90:
        return MPP_ROTATION_270;
    case Rotation::k180:
        return MPP_ROTATION_180;
    case Rotation::k270:
        return MPP_ROTATION_90;
    }
    return MPP_ROTATION_0;
}

class ScopedDmaBufRegistration {
public:
    ScopedDmaBufRegistration(struct mpp_ge* ge, int dma_fd)
        : ge_(ge),
          dma_fd_(dma_fd) {
        if (ge_ != nullptr && dma_fd_ >= 0) {
            registered_ = mpp_ge_add_dmabuf(ge_, dma_fd_) == 0;
        }
    }

    ~ScopedDmaBufRegistration() {
        if (registered_ && ge_ != nullptr && dma_fd_ >= 0) {
            mpp_ge_rm_dmabuf(ge_, dma_fd_);
        }
    }

    bool ok() const {
        return dma_fd_ < 0 || registered_;
    }

private:
    struct mpp_ge* ge_ = nullptr;
    int dma_fd_ = -1;
    bool registered_ = false;
};

class D211Ge2DAccel2DBackend final : public Accel2DBackend {
public:
    ~D211Ge2DAccel2DBackend() override {
        if (ge_ != nullptr) {
            mpp_ge_close(ge_);
            ge_ = nullptr;
        }
    }

    const char* name() const override {
        return "d211-ge2d";
    }

    common::Result initialize(const SurfaceInfo& surface) override {
        surface_ = surface;
        if (ge_ == nullptr) {
            ge_ = mpp_ge_open();
        }
        if (ge_ == nullptr) {
            return common::Result::Fail(common::ErrorCode::kInternalError,
                                        "failed to open D211 GE2D device");
        }
        return common::Result::Ok();
    }

    bool canBlit(const BufferDescriptor& source,
                 const BufferDescriptor& destination,
                 const SurfaceInfo& render_surface,
                 Rotation /*rotation*/) const override {
        return ge_ != nullptr &&
               source.memory_type == BufferMemoryType::kDmaBuf &&
               source.dma_fd >= 0 &&
               destination.physical_address != 0 &&
               destination.stride != 0 &&
               render_surface.valid() &&
               ToMppPixelFormat(source.pixel_format) != MPP_FMT_MAX &&
               ToMppPixelFormat(destination.pixel_format) != MPP_FMT_MAX;
    }

    common::Result blit(const BufferDescriptor& source,
                        const BufferDescriptor& destination,
                        const SurfaceInfo& render_surface,
                        Rotation rotation) override {
        if (!canBlit(source, destination, render_surface, rotation)) {
            return common::Result::Fail(common::ErrorCode::kUnsupported,
                                        "D211 GE2D cannot blit the current buffers");
        }

        if (dmabuf_sync(source.dma_fd, CACHE_CLEAN) < 0) {
            system::Logger::Warn("ui.lvgl.accel",
                                 "dmabuf cache clean failed before GE2D blit");
        }

        ScopedDmaBufRegistration source_registration(ge_, source.dma_fd);
        if (!source_registration.ok()) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "mpp_ge_add_dmabuf failed for source buffer");
        }

        struct ge_bitblt blt;
        std::memset(&blt, 0, sizeof(blt));
        blt.ctrl.flags = ToMppRotationFlags(rotation);

        const std::size_t source_stride =
            render_surface.stride != 0 ? render_surface.stride : source.stride;

        blt.src_buf.buf_type = MPP_DMA_BUF_FD;
        blt.src_buf.fd[0] = source.dma_fd;
        blt.src_buf.stride[0] = static_cast<unsigned int>(source_stride);
        blt.src_buf.size.width = render_surface.width;
        blt.src_buf.size.height = render_surface.height;
        blt.src_buf.format = ToMppPixelFormat(source.pixel_format);

        blt.dst_buf.buf_type = MPP_PHY_ADDR;
        blt.dst_buf.phy_addr[0] = static_cast<unsigned int>(destination.physical_address);
        blt.dst_buf.stride[0] = static_cast<unsigned int>(destination.stride);
        blt.dst_buf.format = ToMppPixelFormat(destination.pixel_format);
        if (rotation == Rotation::k90 || rotation == Rotation::k270) {
            blt.dst_buf.size.width = render_surface.height;
            blt.dst_buf.size.height = render_surface.width;
        } else {
            blt.dst_buf.size.width = render_surface.width;
            blt.dst_buf.size.height = render_surface.height;
        }

        if (mpp_ge_bitblt(ge_, &blt) < 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "mpp_ge_bitblt failed");
        }
        if (mpp_ge_emit(ge_) < 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "mpp_ge_emit failed");
        }
        if (mpp_ge_sync(ge_) < 0) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "mpp_ge_sync failed");
        }

        return common::Result::Ok();
    }

private:
    SurfaceInfo surface_{};
    struct mpp_ge* ge_ = nullptr;
};

#else

class D211Ge2DAccel2DBackend final : public Accel2DBackend {
public:
    const char* name() const override {
        return "d211-ge2d";
    }

    common::Result initialize(const SurfaceInfo& /*surface*/) override {
        return common::Result::Fail(common::ErrorCode::kUnsupported,
                                    "D211 GE2D support is not built");
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
                                    "D211 GE2D support is not built");
    }
};

#endif

}  // namespace

std::unique_ptr<Accel2DBackend> CreateD211Ge2DAccel2DBackend() {
    return std::make_unique<D211Ge2DAccel2DBackend>();
}

}  // namespace hmi_nexus::device::display
