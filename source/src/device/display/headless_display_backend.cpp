#include "hmi_nexus/device/display/backend_factory.h"

#include <memory>
#include <vector>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/device/display/accel_2d_backend.h"
#include "hmi_nexus/device/display/display_backend.h"
#include "hmi_nexus/system/logger.h"

namespace hmi_nexus::device::display {
namespace {

class HeadlessDisplayBackend final : public DisplayBackend {
public:
    const char* name() const override {
        return "headless";
    }

    common::Result initialize(const DisplayConfig& config) override {
        if (config.requested_width <= 0 || config.requested_height <= 0) {
            return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                        "headless display resolution must be greater than zero");
        }

        surface_.width = config.requested_width;
        surface_.height = config.requested_height;
        surface_.dpi = config.dpi;
        surface_.pixel_format = PixelFormat::kRgb565;
        surface_.stride = ComputeStride(surface_.width, surface_.pixel_format);

        buffer_.assign(surface_.stride * static_cast<std::size_t>(surface_.height), 0);
        draw_buffer_.data = buffer_.data();
        draw_buffer_.size = buffer_.size();
        draw_buffer_.stride = surface_.stride;
        draw_buffer_.pixel_format = surface_.pixel_format;
        draw_buffer_.memory_type = BufferMemoryType::kHost;

        system::Logger::Info("ui.lvgl.display",
                             "Using headless LVGL display buffer: " +
                                 std::to_string(surface_.width) + "x" +
                                 std::to_string(surface_.height));
        return common::Result::Ok();
    }

    const SurfaceInfo& surface() const override {
        return surface_;
    }

    const BufferDescriptor& primaryBuffer() const override {
        return draw_buffer_;
    }

    const BufferDescriptor* secondaryBuffer() const override {
        return nullptr;
    }

    common::Result present(const BufferDescriptor& source,
                           const SurfaceInfo& /*render_surface*/,
                           Rotation rotation,
                           Accel2DBackend* /*accel_2d*/) override {
        if (!source.valid()) {
            return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                        "headless present source buffer is invalid");
        }

        if (rotation != Rotation::k0) {
            system::Logger::Debug("ui.lvgl.display",
                                  "Headless backend ignores non-zero rotation request");
        }

        return common::Result::Ok();
    }

private:
    SurfaceInfo surface_{};
    BufferDescriptor draw_buffer_{};
    std::vector<std::uint8_t> buffer_;
};

}  // namespace

std::unique_ptr<DisplayBackend> CreateHeadlessDisplayBackend() {
    return std::make_unique<HeadlessDisplayBackend>();
}

}  // namespace hmi_nexus::device::display
