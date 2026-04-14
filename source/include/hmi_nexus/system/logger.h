#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

#include "hmi_nexus/common/types.h"

namespace hmi_nexus::system {

class Logger {
public:
    enum class Level {
        kDebug = 0,
        kInfo,
        kWarn,
        kError,
        kOff,
    };

    struct Config {
        bool enable_console = true;
        bool enable_file = true;
        bool enable_color = true;
        Level level = Level::kInfo;
        std::string file_path = "logs/hmi.log";
        std::size_t max_file_size = 2 * 1024 * 1024;
        std::size_t max_backup_files = 5;
        std::unordered_map<std::string, Level> tag_levels;
    };

    static void Configure(Config config);
    static Config GetConfig();
    static void SetLevel(Level level);
    static Level GetLevel();
    static void SetTagLevel(const std::string& tag, Level level);
    static void ClearTagLevels();

    static void Debug(const std::string& message);
    static void Debug(const std::string& tag, const std::string& message);
    static void Info(const std::string& message);
    static void Info(const std::string& tag, const std::string& message);
    static void Warn(const std::string& message);
    static void Warn(const std::string& tag, const std::string& message);
    static void Error(const std::string& message);
    static void Error(const std::string& tag, const std::string& message);

    static void HexDump(const std::string& label,
                        const void* data,
                        std::size_t size,
                        Level level = Level::kDebug,
                        std::size_t bytes_per_line = 16);
    static void HexDump(const std::string& tag,
                        const std::string& label,
                        const void* data,
                        std::size_t size,
                        Level level = Level::kDebug,
                        std::size_t bytes_per_line = 16);
    static void HexDump(const std::string& label,
                        const common::ByteBuffer& data,
                        Level level = Level::kDebug,
                        std::size_t bytes_per_line = 16);
    static void HexDump(const std::string& tag,
                        const std::string& label,
                        const common::ByteBuffer& data,
                        Level level = Level::kDebug,
                        std::size_t bytes_per_line = 16);

private:
    static void Log(Level level, std::string_view tag, std::string_view message);
};

}  // namespace hmi_nexus::system
