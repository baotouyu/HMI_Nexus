#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace hmi_nexus::common {

using ByteBuffer = std::vector<std::uint8_t>;
using Milliseconds = std::chrono::milliseconds;

}  // namespace hmi_nexus::common
