#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/device/display/types.h"

namespace hmi_nexus::device::display {

class Accel2DBackend;

class DisplayBackend {
public:
    virtual ~DisplayBackend() = default;

    virtual const char* name() const = 0;
    virtual common::Result initialize(const DisplayConfig& config) = 0;
    virtual const SurfaceInfo& surface() const = 0;
    virtual const BufferDescriptor& primaryBuffer() const = 0;
    virtual const BufferDescriptor* secondaryBuffer() const = 0;
    virtual common::Result present(const BufferDescriptor& source,
                                   const SurfaceInfo& render_surface,
                                   Rotation rotation,
                                   Accel2DBackend* accel_2d) = 0;
};

}  // namespace hmi_nexus::device::display
