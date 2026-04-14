#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/device/display/types.h"

namespace hmi_nexus::device::display {

class Accel2DBackend {
public:
    virtual ~Accel2DBackend() = default;

    virtual const char* name() const = 0;
    virtual common::Result initialize(const SurfaceInfo& surface) = 0;
    virtual bool canBlit(const BufferDescriptor& source,
                         const BufferDescriptor& destination,
                         const SurfaceInfo& render_surface,
                         Rotation rotation) const = 0;
    virtual common::Result blit(const BufferDescriptor& source,
                                const BufferDescriptor& destination,
                                const SurfaceInfo& render_surface,
                                Rotation rotation) = 0;
};

}  // namespace hmi_nexus::device::display
