#pragma once

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::device {

class Runtime {
public:
    common::Result initialize();
    const char* name() const;
};

}  // namespace hmi_nexus::device
