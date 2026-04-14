#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include "hmi_nexus/net/http_async_client.h"
#include "hmi_nexus/net/tls_context.h"
#include "hmi_nexus/system/ui_dispatcher.h"

namespace {

using hmi_nexus::net::HttpAsyncCallbackContext;
using hmi_nexus::net::HttpAsyncClient;
using hmi_nexus::net::HttpClient;
using hmi_nexus::net::HttpDownloadRequest;
using hmi_nexus::net::HttpRequest;
using hmi_nexus::net::TlsContext;
using hmi_nexus::net::TlsOptions;
using hmi_nexus::system::UiDispatcher;

struct DemoOptions {
    bool show_help = false;
    bool download_mode = false;
    HttpRequest request;
    HttpDownloadRequest download;
    TlsOptions tls;
    std::size_t worker_count = 1;
    std::size_t queue_size = 8;
    int poll_interval_ms = 50;
    HttpAsyncCallbackContext callback_context = HttpAsyncCallbackContext::kUiThread;
};

bool IsHelpFlag(const std::string& value) {
    return value == "-h" || value == "--help";
}

bool ReadNextValue(int argc,
                   char* argv[],
                   int* index,
                   std::string* value,
                   const char* option_name) {
    if (*index + 1 >= argc) {
        std::cerr << "Missing value for option: " << option_name << '\n';
        return false;
    }

    *value = argv[*index + 1];
    *index += 1;
    return true;
}

bool ParseLong(const std::string& text, long* value) {
    try {
        *value = std::stol(text);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseSize(const std::string& text, std::size_t* value) {
    long parsed_value = 0;
    if (!ParseLong(text, &parsed_value) || parsed_value <= 0) {
        return false;
    }

    *value = static_cast<std::size_t>(parsed_value);
    return true;
}

void PrintUsage(const char* program) {
    std::cout
        << "Usage:\n"
        << "  " << program << " -h|--help\n"
        << "  " << program << " request METHOD URL [BODY] [options]\n"
        << "  " << program << " download URL OUTPUT [options]\n"
        << "\n"
        << "Options:\n"
        << "  --worker N            Worker 线程数量，默认 1\n"
        << "  --queue N             队列最大挂起任务数，默认 8\n"
        << "  --poll-ms MS          主循环轮询 UiDispatcher 的间隔，默认 50\n"
        << "  --callback worker|ui  回调执行线程，默认 ui\n"
        << "  --timeout MS          请求超时，默认 30000，下载默认 60000\n"
        << "  --ca FILE             CA 证书路径\n"
        << "  --cert FILE           客户端证书路径\n"
        << "  --key FILE            客户端私钥路径\n"
        << "\n"
        << "Examples:\n"
        << "  " << program << " request GET https://example.com\n"
        << "  " << program << " request POST https://example.com/api/login '{\"user\":\"demo\"}' --callback worker\n"
        << "  " << program << " download https://example.com/fw.bin /tmp/fw.bin --worker 2\n";
}

bool ParseArgs(int argc, char* argv[], DemoOptions* options) {
    if (argc <= 1) {
        options->show_help = true;
        return true;
    }

    const std::string command = argv[1];
    if (IsHelpFlag(command)) {
        options->show_help = true;
        return true;
    }

    int index = 2;
    if (command == "request") {
        if (index + 1 >= argc) {
            std::cerr << "request requires METHOD and URL\n";
            return false;
        }

        options->request.method = argv[index++];
        options->request.url = argv[index++];
        if (index < argc) {
            const std::string maybe_body = argv[index];
            if (!maybe_body.empty() && maybe_body.front() != '-') {
                options->request.body = maybe_body;
                ++index;
            }
        }
    } else if (command == "download") {
        if (index + 1 >= argc) {
            std::cerr << "download requires URL and OUTPUT\n";
            return false;
        }

        options->download_mode = true;
        options->download.url = argv[index++];
        options->download.output_path = argv[index++];
    } else {
        std::cerr << "Unknown command: " << command << '\n';
        return false;
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--worker") {
            std::string value;
            if (!ReadNextValue(argc, argv, &index, &value, "--worker") ||
                !ParseSize(value, &options->worker_count)) {
                std::cerr << "Invalid worker count\n";
                return false;
            }
            continue;
        }

        if (arg == "--queue") {
            std::string value;
            if (!ReadNextValue(argc, argv, &index, &value, "--queue") ||
                !ParseSize(value, &options->queue_size)) {
                std::cerr << "Invalid queue size\n";
                return false;
            }
            continue;
        }

        if (arg == "--poll-ms") {
            std::string value;
            long parsed_value = 0;
            if (!ReadNextValue(argc, argv, &index, &value, "--poll-ms") ||
                !ParseLong(value, &parsed_value) || parsed_value <= 0) {
                std::cerr << "Invalid poll interval\n";
                return false;
            }
            options->poll_interval_ms = static_cast<int>(parsed_value);
            continue;
        }

        if (arg == "--callback") {
            std::string value;
            if (!ReadNextValue(argc, argv, &index, &value, "--callback")) {
                return false;
            }
            if (value == "worker") {
                options->callback_context = HttpAsyncCallbackContext::kWorkerThread;
            } else if (value == "ui") {
                options->callback_context = HttpAsyncCallbackContext::kUiThread;
            } else {
                std::cerr << "Invalid callback context: " << value << '\n';
                return false;
            }
            continue;
        }

        if (arg == "--timeout") {
            std::string value;
            long timeout_ms = 0;
            if (!ReadNextValue(argc, argv, &index, &value, "--timeout") ||
                !ParseLong(value, &timeout_ms) || timeout_ms <= 0) {
                std::cerr << "Invalid timeout value\n";
                return false;
            }
            options->request.timeout_ms = timeout_ms;
            options->download.timeout_ms = timeout_ms;
            continue;
        }

        if (arg == "--ca") {
            if (!ReadNextValue(argc, argv, &index, &options->tls.ca_file, "--ca")) {
                return false;
            }
            continue;
        }

        if (arg == "--cert") {
            if (!ReadNextValue(argc, argv, &index, &options->tls.client_cert_file, "--cert")) {
                return false;
            }
            continue;
        }

        if (arg == "--key") {
            if (!ReadNextValue(argc, argv, &index, &options->tls.private_key_file, "--key")) {
                return false;
            }
            continue;
        }

        std::cerr << "Unknown option: " << arg << '\n';
        return false;
    }

    return true;
}

void PrintResponse(const hmi_nexus::net::HttpResponse& response) {
    std::cout << "HTTP status : " << response.status_code << '\n';
    if (!response.error_message.empty()) {
        std::cout << "Error       : " << response.error_message << '\n';
    }
    if (!response.headers.empty()) {
        std::cout << "Headers:\n";
        for (const auto& [key, value] : response.headers) {
            std::cout << "  " << key << ": " << value << '\n';
        }
    }
    std::cout << "Body:\n" << response.body << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    DemoOptions options;
    if (!ParseArgs(argc, argv, &options)) {
        PrintUsage(argv[0]);
        return argc <= 1 ? 0 : 1;
    }

    if (options.show_help) {
        PrintUsage(argv[0]);
        return 0;
    }

    UiDispatcher ui_dispatcher;
    TlsContext tls_context(options.tls);
    HttpClient http_client(&tls_context);
    HttpAsyncClient async_client(&http_client, &ui_dispatcher);

    const auto start_result = async_client.start(options.worker_count, options.queue_size);
    if (!start_result) {
        std::cerr << "HTTP async init failed: " << start_result.message() << '\n';
        return 1;
    }

    std::atomic<bool> finished{false};
    std::atomic<int> exit_code{0};

    hmi_nexus::common::Result schedule_result = hmi_nexus::common::Result::Ok();
    if (options.download_mode) {
        schedule_result = async_client.downloadAsync(
            options.download,
            [&](hmi_nexus::common::Result result) {
                if (!result) {
                    std::cout << "Download failed: " << result.message() << '\n';
                    exit_code = 1;
                } else {
                    std::cout << "Download saved to: " << options.download.output_path << '\n';
                }
                finished = true;
            },
            options.callback_context);
    } else {
        schedule_result = async_client.performAsync(
            options.request,
            [&](hmi_nexus::net::HttpResponse response) {
                PrintResponse(response);
                if (!response.error_message.empty() || response.status_code < 200 ||
                    response.status_code >= 400) {
                    exit_code = 1;
                }
                finished = true;
            },
            options.callback_context);
    }

    if (!schedule_result) {
        std::cerr << "Failed to schedule async HTTP job: " << schedule_result.message() << '\n';
        return 1;
    }

    std::cout << "Async HTTP job submitted. callback="
              << hmi_nexus::net::ToString(options.callback_context)
              << ", workers=" << options.worker_count
              << ", queue_limit=" << options.queue_size << '\n';

    using clock = std::chrono::steady_clock;
    auto last_progress = clock::now();

    while (!finished.load()) {
        ui_dispatcher.drain();

        const auto now = clock::now();
        if (now - last_progress >= std::chrono::seconds(1)) {
            std::cout << "Waiting... pending_jobs=" << async_client.pendingJobs() << '\n';
            last_progress = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.poll_interval_ms));
    }

    ui_dispatcher.drain();
    async_client.stop();
    return exit_code.load();
}
