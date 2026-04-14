#include "hmi_nexus/device/display/backend_factory.h"

#include <memory>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/device/display/accel_2d_backend.h"

namespace hmi_nexus::device::display {
namespace {

class NullAccel2DBackend final : public Accel2DBackend {
public:
    const char* name() const override {
        return "none";
    }

    common::Result initialize(const SurfaceInfo& /*surface*/) override {
        return common::Result::Ok();
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
                                    "2D acceleration backend is disabled");
    }
};

}  // namespace

std::unique_ptr<Accel2DBackend> CreateNullAccel2DBackend() {
    return std::make_unique<NullAccel2DBackend>();
}

bool HasD211Ge2DBackend() {
#if HMI_NEXUS_HAS_D211_GE2D
    return true;
#else
    return false;
#endif
}

bool HasSunxiG2DBackend() {
#if HMI_NEXUS_HAS_SUNXI_G2D
    return true;
#else
    return false;
#endif
}

}  // namespace hmi_nexus::device::display
