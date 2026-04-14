#pragma once

#include <memory>

namespace hmi_nexus::device::display {

class Accel2DBackend;
class DisplayBackend;

std::unique_ptr<DisplayBackend> CreateHeadlessDisplayBackend();
std::unique_ptr<DisplayBackend> CreateLinuxFbdevDisplayBackend();

std::unique_ptr<Accel2DBackend> CreateNullAccel2DBackend();
std::unique_ptr<Accel2DBackend> CreateD211Ge2DAccel2DBackend();
std::unique_ptr<Accel2DBackend> CreateSunxiG2DAccel2DBackend();

bool HasD211Ge2DBackend();
bool HasSunxiG2DBackend();

}  // namespace hmi_nexus::device::display
