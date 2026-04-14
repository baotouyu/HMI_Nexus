#pragma once

#include <map>
#include <mutex>
#include <string>

#include "hmi_nexus/common/result.h"

namespace hmi_nexus::net {

class TlsContext;

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    long timeout_ms = 30000;
    bool follow_redirects = true;
};

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error_message;
};

struct HttpDownloadRequest {
    std::string url;
    std::string output_path;
    std::map<std::string, std::string> headers;
    long timeout_ms = 60000;
    bool follow_redirects = true;
};

class HttpClient {
public:
    explicit HttpClient(TlsContext* tls_context = nullptr);
    ~HttpClient() = default;

    common::Result initialize();
    HttpResponse perform(const HttpRequest& request);
    common::Result download(const HttpDownloadRequest& request);

private:
    common::Result ensureInitialized();
    common::Result initializeImpl();

    TlsContext* tls_context_ = nullptr;
    mutable std::mutex init_mutex_;
    common::Result init_result_ = common::Result::Ok();
    bool init_attempted_ = false;
    bool initialized_ = false;
};

}  // namespace hmi_nexus::net
