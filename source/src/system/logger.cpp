#include "hmi_nexus/system/logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>

#include <unistd.h>

namespace hmi_nexus::system {
namespace {

std::mutex g_log_mutex;
std::atomic<Logger::Level> g_log_level {Logger::Level::kInfo};
Logger::Config g_log_config;
std::ofstream g_log_file_stream;
std::filesystem::path g_open_log_path;

const char* LevelName(Logger::Level level) {
    switch (level) {
    case Logger::Level::kDebug:
        return "DEBUG";
    case Logger::Level::kInfo:
        return "INFO";
    case Logger::Level::kWarn:
        return "WARN";
    case Logger::Level::kError:
        return "ERROR";
    case Logger::Level::kOff:
        return "OFF";
    }
    return "UNKNOWN";
}

const char* LevelColor(Logger::Level level) {
    switch (level) {
    case Logger::Level::kDebug:
        return "\033[36m";
    case Logger::Level::kInfo:
        return "\033[32m";
    case Logger::Level::kWarn:
        return "\033[33m";
    case Logger::Level::kError:
        return "\033[31m";
    case Logger::Level::kOff:
        return "\033[0m";
    }
    return "\033[0m";
}

std::string NormalizeTag(std::string tag) {
    while (!tag.empty() &&
           (std::isspace(static_cast<unsigned char>(tag.front())) != 0 ||
            tag.front() == '.')) {
        tag.erase(tag.begin());
    }

    while (!tag.empty() &&
           (std::isspace(static_cast<unsigned char>(tag.back())) != 0 ||
            tag.back() == '.')) {
        tag.pop_back();
    }

    if (tag.size() >= 2 && tag.compare(tag.size() - 2, 2, ".*") == 0) {
        tag.resize(tag.size() - 2);
        while (!tag.empty() && tag.back() == '.') {
            tag.pop_back();
        }
    }

    return tag;
}

std::optional<Logger::Level> ResolveTagLevelLocked(std::string_view tag) {
    std::string candidate = NormalizeTag(std::string(tag));
    while (!candidate.empty()) {
        const auto it = g_log_config.tag_levels.find(candidate);
        if (it != g_log_config.tag_levels.end()) {
            return it->second;
        }

        const auto split = candidate.rfind('.');
        if (split == std::string::npos) {
            break;
        }
        candidate.resize(split);
    }
    return std::nullopt;
}

bool ShouldLogLocked(Logger::Level level, std::string_view tag) {
    Logger::Level threshold = g_log_config.level;
    const auto tag_level = ResolveTagLevelLocked(tag);
    if (tag_level.has_value() &&
        static_cast<int>(tag_level.value()) > static_cast<int>(threshold)) {
        threshold = tag_level.value();
    }

    return threshold != Logger::Level::kOff &&
           static_cast<int>(level) >= static_cast<int>(threshold);
}

bool SupportsColor(FILE* stream) {
    return g_log_config.enable_color && stream != nullptr && ::isatty(::fileno(stream)) != 0;
}

std::string TimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
        std::chrono::seconds(1);

    std::tm local_tm {};
    localtime_r(&time, &local_tm);

    std::ostringstream stream;
    stream << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << millis.count();
    return stream.str();
}

std::string ComposeLine(Logger::Level level, std::string_view tag, std::string_view message) {
    std::ostringstream stream;
    stream << '[' << TimestampNow() << "] [" << LevelName(level) << ']';
    if (!tag.empty()) {
        stream << " [" << tag << ']';
    }
    stream << ' ' << message;
    return stream.str();
}

std::filesystem::path BackupPath(const std::filesystem::path& path, std::size_t index) {
    return std::filesystem::path(path.string() + "." + std::to_string(index));
}

void RotateLogFileLocked(std::size_t upcoming_size) {
    if (!g_log_config.enable_file || g_log_config.file_path.empty() ||
        g_log_config.max_file_size == 0) {
        return;
    }

    const std::filesystem::path log_path(g_log_config.file_path);
    std::error_code error;
    const bool exists = std::filesystem::exists(log_path, error);
    if (error || !exists) {
        return;
    }

    const auto current_size = std::filesystem::file_size(log_path, error);
    if (error || current_size + upcoming_size <= g_log_config.max_file_size) {
        return;
    }

    if (g_log_file_stream.is_open()) {
        g_log_file_stream.close();
    }

    if (g_log_config.max_backup_files == 0) {
        std::filesystem::remove(log_path, error);
        g_open_log_path.clear();
        return;
    }

    std::filesystem::remove(BackupPath(log_path, g_log_config.max_backup_files), error);
    for (std::size_t index = g_log_config.max_backup_files; index > 1; --index) {
        const auto from = BackupPath(log_path, index - 1);
        const auto to = BackupPath(log_path, index);
        if (std::filesystem::exists(from, error)) {
            std::filesystem::rename(from, to, error);
        }
    }
    std::filesystem::rename(log_path, BackupPath(log_path, 1), error);
    g_open_log_path.clear();
}

void EnsureLogFileOpenLocked() {
    if (!g_log_config.enable_file || g_log_config.file_path.empty()) {
        if (g_log_file_stream.is_open()) {
            g_log_file_stream.close();
        }
        g_open_log_path.clear();
        return;
    }

    const std::filesystem::path log_path(g_log_config.file_path);
    if (g_log_file_stream.is_open() && g_open_log_path == log_path) {
        return;
    }

    if (g_log_file_stream.is_open()) {
        g_log_file_stream.close();
    }

    std::error_code error;
    if (log_path.has_parent_path()) {
        std::filesystem::create_directories(log_path.parent_path(), error);
    }

    g_log_file_stream.open(log_path, std::ios::app);
    if (g_log_file_stream.is_open()) {
        g_open_log_path = log_path;
    } else {
        g_open_log_path.clear();
    }
}

void WriteLineLocked(Logger::Level level, std::string_view tag, std::string_view message) {
    const std::string line = ComposeLine(level, tag, message);

    if (g_log_config.enable_console) {
        std::ostream& stream = level >= Logger::Level::kWarn ? std::cerr : std::cout;
        FILE* stream_handle = level >= Logger::Level::kWarn ? stderr : stdout;
        const bool use_color = SupportsColor(stream_handle);
        if (use_color) {
            stream << LevelColor(level);
        }
        stream << line;
        if (use_color) {
            stream << "\033[0m";
        }
        stream << std::endl;
    }

    if (!g_log_config.enable_file) {
        return;
    }

    RotateLogFileLocked(line.size() + 1);
    EnsureLogFileOpenLocked();
    if (g_log_file_stream.is_open()) {
        g_log_file_stream << line << '\n';
        g_log_file_stream.flush();
    }
}

void HexDumpLocked(Logger::Level level,
                   std::string_view tag,
                   std::string_view label,
                   const void* data,
                   std::size_t size,
                   std::size_t bytes_per_line) {
    if (data == nullptr) {
        WriteLineLocked(level, tag, std::string(label) + " <null>");
        return;
    }

    if (bytes_per_line == 0) {
        bytes_per_line = 16;
    }

    const auto* bytes = static_cast<const unsigned char*>(data);

    {
        std::ostringstream header;
        header << label << " (" << size << " bytes)";
        WriteLineLocked(level, tag, header.str());
    }

    for (std::size_t offset = 0; offset < size; offset += bytes_per_line) {
        std::ostringstream line;
        line << std::hex << std::setfill('0') << std::setw(4) << offset << "  ";

        const std::size_t line_end = std::min(offset + bytes_per_line, size);
        for (std::size_t index = offset; index < offset + bytes_per_line; ++index) {
            if (index < line_end) {
                line << std::setw(2) << static_cast<unsigned>(bytes[index]) << ' ';
            } else {
                line << "   ";
            }
        }

        line << " |";
        for (std::size_t index = offset; index < line_end; ++index) {
            const unsigned char ch = bytes[index];
            line << (std::isprint(ch) ? static_cast<char>(ch) : '.');
        }
        line << '|';

        WriteLineLocked(level, tag, line.str());
    }
}

}  // namespace

void Logger::Configure(Config config) {
    if (config.max_file_size == 0) {
        config.max_file_size = 2 * 1024 * 1024;
    }

    std::unordered_map<std::string, Level> normalized_tag_levels;
    for (const auto& entry : config.tag_levels) {
        const std::string normalized_tag = NormalizeTag(entry.first);
        if (normalized_tag.empty()) {
            continue;
        }
        normalized_tag_levels[normalized_tag] = entry.second;
    }
    config.tag_levels = std::move(normalized_tag_levels);

    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_config = std::move(config);
    g_log_level.store(g_log_config.level);

    if (g_log_file_stream.is_open()) {
        g_log_file_stream.close();
    }
    g_open_log_path.clear();
    EnsureLogFileOpenLocked();
}

Logger::Config Logger::GetConfig() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return g_log_config;
}

void Logger::SetLevel(Level level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_config.level = level;
    g_log_level.store(level);
}

Logger::Level Logger::GetLevel() {
    return g_log_level.load();
}

void Logger::SetTagLevel(const std::string& tag, Level level) {
    const std::string normalized_tag = NormalizeTag(tag);
    if (normalized_tag.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_config.tag_levels[normalized_tag] = level;
}

void Logger::ClearTagLevels() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_config.tag_levels.clear();
}

void Logger::Debug(const std::string& message) {
    Log(Level::kDebug, {}, message);
}

void Logger::Debug(const std::string& tag, const std::string& message) {
    Log(Level::kDebug, tag, message);
}

void Logger::Info(const std::string& message) {
    Log(Level::kInfo, {}, message);
}

void Logger::Info(const std::string& tag, const std::string& message) {
    Log(Level::kInfo, tag, message);
}

void Logger::Warn(const std::string& message) {
    Log(Level::kWarn, {}, message);
}

void Logger::Warn(const std::string& tag, const std::string& message) {
    Log(Level::kWarn, tag, message);
}

void Logger::Error(const std::string& message) {
    Log(Level::kError, {}, message);
}

void Logger::Error(const std::string& tag, const std::string& message) {
    Log(Level::kError, tag, message);
}

void Logger::HexDump(const std::string& label,
                     const void* data,
                     std::size_t size,
                     Level level,
                     std::size_t bytes_per_line) {
    HexDump({}, label, data, size, level, bytes_per_line);
}

void Logger::HexDump(const std::string& tag,
                     const std::string& label,
                     const void* data,
                     std::size_t size,
                     Level level,
                     std::size_t bytes_per_line) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!ShouldLogLocked(level, tag)) {
        return;
    }
    HexDumpLocked(level, tag, label, data, size, bytes_per_line);
}

void Logger::HexDump(const std::string& label,
                     const common::ByteBuffer& data,
                     Level level,
                     std::size_t bytes_per_line) {
    HexDump({}, label, data.data(), data.size(), level, bytes_per_line);
}

void Logger::HexDump(const std::string& tag,
                     const std::string& label,
                     const common::ByteBuffer& data,
                     Level level,
                     std::size_t bytes_per_line) {
    HexDump(tag, label, data.data(), data.size(), level, bytes_per_line);
}

void Logger::Log(Level level, std::string_view tag, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!ShouldLogLocked(level, tag)) {
        return;
    }
    WriteLineLocked(level, tag, message);
}

}  // namespace hmi_nexus::system
