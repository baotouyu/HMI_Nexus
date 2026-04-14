#include <cstdint>
#include <iostream>
#include <string>

#include "hmi_nexus/common/json/json_parser.h"

int main() {
    using hmi_nexus::common::json::JsonDocument;
    using hmi_nexus::common::json::JsonParser;

    const std::string payload =
        R"({
            "device": {
                "id": "panel-01",
                "serial": 1234567890123
            },
            "cloud": {
                "connected": true,
                "retry": 3
            },
            "sensor": {
                "temperature": 23.5
            },
            "alarms": ["low_battery", "over_temp"]
        })";

    JsonDocument document;
    const auto result = JsonParser::parseObject(payload, document);
    if (!result) {
        std::cerr << "parse failed: " << result.message() << '\n';
        return 1;
    }

    std::cout << "JSON backend: " << JsonParser::backendName() << '\n';
    std::cout << "Document valid: " << document.valid() << '\n';
    std::cout << "device.id: " << document.getString("device.id") << '\n';
    std::cout << "device.serial: " << document.getInt64("device.serial") << '\n';
    std::cout << "cloud.connected: " << document.getBool("cloud.connected") << '\n';
    std::cout << "cloud.retry: " << document.getInt("cloud.retry") << '\n';
    std::cout << "sensor.temperature: " << document.getDouble("sensor.temperature") << '\n';
    std::cout << "alarms size: " << document.getArraySize("alarms") << '\n';
    std::cout << "has ota.version: " << document.has("ota.version") << '\n';
    std::cout << "fallback string: " << document.getString("ota.version", "not_set") << '\n';

    JsonDocument copied = document;
    std::cout << "copied device.id: " << copied.getString("device.id") << '\n';

    JsonDocument moved = std::move(copied);
    std::cout << "moved cloud.retry: " << moved.getInt("cloud.retry") << '\n';

    return 0;
}
