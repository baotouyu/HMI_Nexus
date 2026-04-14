#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hmi_nexus::device::display {

enum class PixelFormat {
    kUnknown = 0,
    kRgb565,
    kRgb888,
    kArgb8888,
    kXrgb8888,
};

enum class Rotation {
    k0 = 0,
    k90,
    k180,
    k270,
};

enum class BufferMemoryType {
    kHost = 0,
    kDmaBuf,
    kPhysical,
};

struct BufferDescriptor {
    std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t stride = 0;
    PixelFormat pixel_format = PixelFormat::kUnknown;
    int dma_fd = -1;
    std::uintptr_t physical_address = 0;
    BufferMemoryType memory_type = BufferMemoryType::kHost;

    bool valid() const {
        return data != nullptr && size != 0 && pixel_format != PixelFormat::kUnknown;
    }
};

struct SurfaceInfo {
    int width = 0;
    int height = 0;
    int dpi = 160;
    PixelFormat pixel_format = PixelFormat::kUnknown;
    std::size_t stride = 0;

    bool valid() const {
        return width > 0 && height > 0 && pixel_format != PixelFormat::kUnknown && stride != 0;
    }
};

struct DisplayConfig {
    int requested_width = 800;
    int requested_height = 480;
    int dpi = 160;
    std::size_t draw_buffer_lines = 40;
    std::string device_path = "/dev/fb0";
    Rotation rotation = Rotation::k0;
    bool prefer_dma_draw_buffer = false;
    PixelFormat draw_buffer_pixel_format = PixelFormat::kUnknown;
};

inline std::size_t BytesPerPixel(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::kRgb565:
        return 2;
    case PixelFormat::kRgb888:
        return 3;
    case PixelFormat::kArgb8888:
    case PixelFormat::kXrgb8888:
        return 4;
    case PixelFormat::kUnknown:
        break;
    }
    return 0;
}

inline std::size_t ComputeStride(int width, PixelFormat pixel_format) {
    return static_cast<std::size_t>(width) * BytesPerPixel(pixel_format);
}

inline const char* PixelFormatName(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::kRgb565:
        return "rgb565";
    case PixelFormat::kRgb888:
        return "rgb888";
    case PixelFormat::kArgb8888:
        return "argb8888";
    case PixelFormat::kXrgb8888:
        return "xrgb8888";
    case PixelFormat::kUnknown:
        break;
    }
    return "unknown";
}

inline const char* RotationName(Rotation rotation) {
    switch (rotation) {
    case Rotation::k0:
        return "0";
    case Rotation::k90:
        return "90";
    case Rotation::k180:
        return "180";
    case Rotation::k270:
        return "270";
    }
    return "0";
}

}  // namespace hmi_nexus::device::display
