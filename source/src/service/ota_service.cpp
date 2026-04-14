#include "hmi_nexus/service/ota_service.h"

#include "hmi_nexus/common/error_code.h"

namespace hmi_nexus::service {

OtaService::OtaService(net::HttpClient& http_client)
    : http_client_(http_client) {}

common::Result OtaService::checkForUpdates() {
    const auto response = http_client_.perform({"GET", "https://updates.placeholder.local/manifest.json", {}, ""});
    if (response.status_code != 200) {
        return common::Result::Fail(common::ErrorCode::kNetworkError,
                                    "OTA manifest request failed");
    }
    return common::Result::Ok();
}

}  // namespace hmi_nexus::service
