#include <iostream>

#include "hmi_nexus/common/types.h"
#include "hmi_nexus/system/logger.h"

int main() {
    using Logger = hmi_nexus::system::Logger;

    Logger::Config config;
    config.enable_console = true;
    config.enable_file = true;
    config.enable_color = true;
    config.level = Logger::Level::kDebug;
    config.file_path = "logs/logger_example.log";
    config.max_file_size = 512 * 1024;
    config.max_backup_files = 3;
    config.tag_levels["net"] = Logger::Level::kWarn;
    config.tag_levels["net.http"] = Logger::Level::kError;

    Logger::Configure(config);

    std::cout << "Logger example started.\n";
    std::cout << "Configured rules: global=DEBUG, net*=WARN, net.http=ERROR.\n";

    Logger::Debug("app.example", "debug log from logger example");
    Logger::Info("ui.home", "ui info log is visible");

    std::cout << "The next INFO log uses tag net.http and should be filtered.\n";
    Logger::Info("net.http", "this info log is filtered by tag rule");
    Logger::Warn("net.mqtt", "mqtt warn log is visible because net*=WARN");
    Logger::Error("net.http", "http error log is visible");

    hmi_nexus::common::ByteBuffer frame = {
        0x48, 0x4d, 0x49, 0x5f, 0x4e, 0x45, 0x58, 0x55,
        0x53, 0x00, 0x10, 0x20, 0x7e, 0x7f, 0x80, 0xff,
    };
    Logger::HexDump("app.example",
                    "sample.frame",
                    frame,
                    Logger::Level::kDebug,
                    8);

    std::cout << "Raise global level to WARN at runtime.\n";
    Logger::SetLevel(Logger::Level::kWarn);
    Logger::Info("app.example", "this info log is filtered by global WARN");
    Logger::Warn("app.example", "warn log is still visible");

    std::cout << "Clear tag rules and force ui.home to ERROR only.\n";
    Logger::ClearTagLevels();
    Logger::SetTagLevel("ui.home", Logger::Level::kError);
    Logger::Warn("ui.home", "this warn log is filtered by ui.home=ERROR");
    Logger::Error("ui.home", "ui.home error log is visible");

    std::cout << "Logs are also written to logs/logger_example.log\n";
    return 0;
}
