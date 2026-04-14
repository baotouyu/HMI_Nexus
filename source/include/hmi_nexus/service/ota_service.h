#pragma once

#include "hmi_nexus/common/result.h"
#include "hmi_nexus/net/http_client.h"

namespace hmi_nexus::service {

class OtaService {
public:
    explicit OtaService(net::HttpClient& http_client);

    common::Result checkForUpdates();

private:
    net::HttpClient& http_client_;
};

}  // namespace hmi_nexus::service
