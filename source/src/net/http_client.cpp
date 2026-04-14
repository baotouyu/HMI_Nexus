#include "hmi_nexus/net/http_client.h"

#include <utility>

#include "hmi_nexus/common/error_code.h"
#include "hmi_nexus/net/tls_context.h"
#include "hmi_nexus/system/logger.h"

#if HMI_NEXUS_HAS_CURL
#include <curl/curl.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <cctype>
#endif

namespace hmi_nexus::net {
namespace {

#if HMI_NEXUS_HAS_CURL

struct CurlHandleDeleter {
    void operator()(CURL* handle) const {
        if (handle != nullptr) {
            curl_easy_cleanup(handle);
        }
    }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* list) const {
        if (list != nullptr) {
            curl_slist_free_all(list);
        }
    }
};

std::string Trim(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
            value.back() == '\t')) {
        value.pop_back();
    }

    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' ||
            value[begin] == '\n')) {
        ++begin;
    }

    return value.substr(begin);
}

std::string ToUpper(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

common::Result EnsureCurlGlobalInit() {
    static std::once_flag once;
    static common::Result init_result = common::Result::Ok();

    std::call_once(once, []() {
        const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK) {
            init_result = common::Result::Fail(
                common::ErrorCode::kInternalError,
                "curl_global_init failed: " + std::string(curl_easy_strerror(code)));
        }
    });

    return init_result;
}

size_t WriteStringCallback(char* data, size_t size, size_t count, void* user_data) {
    if (user_data == nullptr) {
        return 0;
    }

    const size_t bytes = size * count;
    auto* output = static_cast<std::string*>(user_data);
    output->append(data, bytes);
    return bytes;
}

size_t HeaderCallback(char* data, size_t size, size_t count, void* user_data) {
    if (user_data == nullptr) {
        return 0;
    }

    const size_t bytes = size * count;
    std::string line(data, bytes);
    line = Trim(line);
    if (line.empty() || line.rfind("HTTP/", 0) == 0) {
        return bytes;
    }

    const auto separator = line.find(':');
    if (separator == std::string::npos) {
        return bytes;
    }

    auto* headers = static_cast<std::map<std::string, std::string>*>(user_data);
    const std::string key = Trim(line.substr(0, separator));
    const std::string value = Trim(line.substr(separator + 1));
    (*headers)[key] = value;
    return bytes;
}

size_t FileWriteCallback(char* data, size_t size, size_t count, void* user_data) {
    if (user_data == nullptr) {
        return 0;
    }

    const size_t bytes = size * count;
    auto* output = static_cast<std::ofstream*>(user_data);
    output->write(data, static_cast<std::streamsize>(bytes));
    return output->good() ? bytes : 0;
}

common::Result ApplyTlsOptions(CURL* handle, const TlsContext* tls_context) {
    if (tls_context == nullptr) {
        return common::Result::Ok();
    }

    const auto& options = tls_context->options();
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);

    if (!options.ca_file.empty()) {
        curl_easy_setopt(handle, CURLOPT_CAINFO, options.ca_file.c_str());
    }
    if (!options.client_cert_file.empty()) {
        curl_easy_setopt(handle, CURLOPT_SSLCERT, options.client_cert_file.c_str());
    }
    if (!options.private_key_file.empty()) {
        curl_easy_setopt(handle, CURLOPT_SSLKEY, options.private_key_file.c_str());
    }
    return common::Result::Ok();
}

common::Result ApplySharedOptions(CURL* handle,
                                  const std::string& url,
                                  const std::map<std::string, std::string>& headers,
                                  long timeout_ms,
                                  bool follow_redirects,
                                  const TlsContext* tls_context,
                                  std::unique_ptr<curl_slist, CurlSlistDeleter>* header_list) {
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, follow_redirects ? 1L : 0L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "HMI_Nexus/0.1");
    if (timeout_ms > 0) {
        curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
    }

    auto tls_result = ApplyTlsOptions(handle, tls_context);
    if (!tls_result) {
        return tls_result;
    }

    curl_slist* raw_header_list = nullptr;
    for (const auto& [key, value] : headers) {
        const std::string header_line = key + ": " + value;
        raw_header_list = curl_slist_append(raw_header_list, header_line.c_str());
    }
    header_list->reset(raw_header_list);
    if (raw_header_list != nullptr) {
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, raw_header_list);
    }

    return common::Result::Ok();
}

void ApplyMethodOptions(CURL* handle, const HttpRequest& request) {
    const std::string method = ToUpper(request.method.empty() ? "GET" : request.method);
    curl_easy_setopt(handle, CURLOPT_NOBODY, 0L);

    if (method == "GET") {
        curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
        return;
    }

    if (method == "HEAD") {
        curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "HEAD");
        return;
    }

    if (method == "POST") {
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
    } else {
        curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    }

    if (!request.body.empty() || method == "POST" || method == "PUT" || method == "PATCH") {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(request.body.size()));
    }
}

#endif

}  // namespace

HttpClient::HttpClient(TlsContext* tls_context)
    : tls_context_(tls_context) {}

common::Result HttpClient::ensureInitialized() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (initialized_) {
        return common::Result::Ok();
    }
    if (init_attempted_) {
        return init_result_;
    }

    init_attempted_ = true;
    init_result_ = initializeImpl();
    return init_result_;
}

common::Result HttpClient::initialize() {
    std::lock_guard<std::mutex> lock(init_mutex_);
    if (initialized_) {
        return common::Result::Ok();
    }
    if (init_attempted_) {
        return init_result_;
    }

    init_attempted_ = true;
    init_result_ = initializeImpl();
    return init_result_;
}

common::Result HttpClient::initializeImpl() {
#if HMI_NEXUS_HAS_CURL
    auto init_result = EnsureCurlGlobalInit();
    if (!init_result) {
        return init_result;
    }

    initialized_ = true;
    if (tls_context_ != nullptr && tls_context_->enabled()) {
        system::Logger::Info("net.http",
                             "HTTP client configured with libcurl and TLS context");
    } else {
        system::Logger::Info("net.http",
                             "HTTP client configured with libcurl and default TLS store");
    }
    return common::Result::Ok();
#else
    initialized_ = false;
    system::Logger::Warn("net.http",
                         "HTTP client running in stub mode because libcurl is unavailable");
    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "libcurl support is not available");
#endif
}

HttpResponse HttpClient::perform(const HttpRequest& request) {
    HttpResponse response;
    if (request.url.empty()) {
        response.status_code = 400;
        response.error_message = "missing request url";
        return response;
    }

    auto init_result = ensureInitialized();
    if (!init_result) {
        response.error_message = init_result.message();
        return response;
    }

#if HMI_NEXUS_HAS_CURL
    std::unique_ptr<CURL, CurlHandleDeleter> handle(curl_easy_init());
    if (!handle) {
        response.error_message = "curl_easy_init failed";
        return response;
    }

    std::unique_ptr<curl_slist, CurlSlistDeleter> header_list;
    auto option_result =
        ApplySharedOptions(handle.get(), request.url, request.headers, request.timeout_ms,
                           request.follow_redirects, tls_context_, &header_list);
    if (!option_result) {
        response.error_message = option_result.message();
        return response;
    }

    ApplyMethodOptions(handle.get(), request);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &WriteStringCallback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &HeaderCallback);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &response.headers);

    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, error_buffer);

    const CURLcode code = curl_easy_perform(handle.get());
    if (code != CURLE_OK) {
        response.error_message =
            error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
        system::Logger::Error("net.http",
                              "HTTP request failed: " + request.method + " " + request.url +
                                  " -> " + response.error_message);
        return response;
    }

    long status_code = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status_code);
    response.status_code = static_cast<int>(status_code);

    system::Logger::Info("net.http",
                         "HTTP request completed: " + request.method + " " + request.url +
                             " -> " + std::to_string(response.status_code));
    return response;
#else
    response.status_code = 501;
    response.error_message = "libcurl support is not available";
    return response;
#endif
}

common::Result HttpClient::download(const HttpDownloadRequest& request) {
    if (request.url.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "missing download url");
    }
    if (request.output_path.empty()) {
        return common::Result::Fail(common::ErrorCode::kInvalidArgument,
                                    "missing download output path");
    }

    auto init_result = ensureInitialized();
    if (!init_result) {
        return init_result;
    }

#if HMI_NEXUS_HAS_CURL
    namespace fs = std::filesystem;

    const fs::path output_path(request.output_path);
    if (output_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(output_path.parent_path(), ec);
        if (ec) {
            return common::Result::Fail(common::ErrorCode::kIoError,
                                        "failed to create download directory: " +
                                            output_path.parent_path().string());
        }
    }

    std::ofstream output_file(request.output_path, std::ios::binary | std::ios::trunc);
    if (!output_file.is_open()) {
        return common::Result::Fail(common::ErrorCode::kIoError,
                                    "failed to open download file: " + request.output_path);
    }

    std::unique_ptr<CURL, CurlHandleDeleter> handle(curl_easy_init());
    if (!handle) {
        return common::Result::Fail(common::ErrorCode::kInternalError,
                                    "curl_easy_init failed");
    }

    std::unique_ptr<curl_slist, CurlSlistDeleter> header_list;
    auto option_result =
        ApplySharedOptions(handle.get(), request.url, request.headers, request.timeout_ms,
                           request.follow_redirects, tls_context_, &header_list);
    if (!option_result) {
        return option_result;
    }

    curl_easy_setopt(handle.get(), CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &FileWriteCallback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &output_file);

    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, error_buffer);

    const CURLcode code = curl_easy_perform(handle.get());
    output_file.flush();
    output_file.close();

    if (code != CURLE_OK) {
        fs::remove(output_path);
        const std::string message =
            error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
        return common::Result::Fail(common::ErrorCode::kNetworkError,
                                    "HTTP download failed: " + message);
    }

    long status_code = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status_code);
    if (status_code < 200 || status_code >= 300) {
        fs::remove(output_path);
        return common::Result::Fail(common::ErrorCode::kNetworkError,
                                    "HTTP download returned status " +
                                        std::to_string(status_code));
    }

    system::Logger::Info("net.http",
                         "HTTP download completed: " + request.url + " -> " +
                             request.output_path);
    return common::Result::Ok();
#else
    (void)request;
    return common::Result::Fail(common::ErrorCode::kUnsupported,
                                "libcurl support is not available");
#endif
}

}  // namespace hmi_nexus::net
