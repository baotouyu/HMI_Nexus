#include <iostream>
#include <string>
#include <utility>

#include "hmi_nexus/net/http_client.h"
#include "hmi_nexus/net/tls_context.h"

namespace {

using hmi_nexus::net::HttpClient;
using hmi_nexus::net::HttpDownloadRequest;
using hmi_nexus::net::HttpRequest;
using hmi_nexus::net::TlsContext;
using hmi_nexus::net::TlsOptions;

struct DemoOptions {
    std::string command;
    HttpRequest request;
    HttpDownloadRequest download;
    TlsOptions tls;
};

bool IsHelpFlag(const std::string& value) {
    return value == "-h" || value == "--help";
}

bool IsOption(const std::string& value) {
    return !value.empty() && value.front() == '-';
}

void PrintUsage(const char* program) {
    std::cout
        << "Usage:\n"
        << "  " << program << " -h|--help\n"
        << "  " << program << " get URL [options]\n"
        << "  " << program << " post URL BODY [options]\n"
        << "  " << program << " put URL BODY [options]\n"
        << "  " << program << " patch URL BODY [options]\n"
        << "  " << program << " delete URL [BODY] [options]\n"
        << "  " << program << " head URL [options]\n"
        << "  " << program << " options URL [options]\n"
        << "  " << program << " request METHOD URL [BODY] [options]\n"
        << "  " << program << " download URL OUTPUT [options]\n"
        << "\n"
        << "Options:\n"
        << "  -H, --header KEY:VALUE  Add request header, can be repeated\n"
        << "  --timeout MS            Request timeout in milliseconds\n"
        << "  --ca FILE               CA certificate path for HTTPS\n"
        << "  --cert FILE             Client certificate path for HTTPS\n"
        << "  --key FILE              Private key path for HTTPS\n"
        << "\n"
        << "Examples:\n"
        << "  " << program << " get https://example.com/api/ping\n"
        << "  " << program << " post https://example.com/api/login '{\"user\":\"demo\"}'\n"
        << "  " << program << " put https://example.com/api/item/1 '{\"name\":\"panel\"}'\n"
        << "  " << program << " patch https://example.com/api/item/1 '{\"name\":\"delta\"}'\n"
        << "  " << program << " delete https://example.com/api/item/1\n"
        << "  " << program << " head https://example.com\n"
        << "  " << program << " options https://example.com\n"
        << "  " << program << " request PUT https://example.com/api/item/1 '{\"name\":\"hmi\"}'\n"
        << "  " << program << " download https://example.com/fw.bin /tmp/fw.bin --ca config/certs/ca.pem\n";
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

bool ParseHeaderLine(const std::string& text, std::pair<std::string, std::string>* header) {
    const auto separator = text.find(':');
    if (separator == std::string::npos) {
        return false;
    }

    header->first = text.substr(0, separator);
    header->second = text.substr(separator + 1);
    if (!header->second.empty() && header->second.front() == ' ') {
        header->second.erase(0, 1);
    }
    return !header->first.empty();
}

bool ParseArgs(int argc, char* argv[], DemoOptions* options) {
    if (argc <= 1) {
        return false;
    }

    options->command = argv[1];
    int index = 2;

    if (options->command == "get") {
        if (index >= argc) {
            std::cerr << "get requires URL\n";
            return false;
        }
        options->request.method = "GET";
        options->request.url = argv[index++];
    } else if (options->command == "post") {
        if (index + 1 >= argc) {
            std::cerr << "post requires URL and BODY\n";
            return false;
        }
        options->request.method = "POST";
        options->request.url = argv[index++];
        options->request.body = argv[index++];
    } else if (options->command == "put") {
        if (index + 1 >= argc) {
            std::cerr << "put requires URL and BODY\n";
            return false;
        }
        options->request.method = "PUT";
        options->request.url = argv[index++];
        options->request.body = argv[index++];
    } else if (options->command == "patch") {
        if (index + 1 >= argc) {
            std::cerr << "patch requires URL and BODY\n";
            return false;
        }
        options->request.method = "PATCH";
        options->request.url = argv[index++];
        options->request.body = argv[index++];
    } else if (options->command == "delete") {
        if (index >= argc) {
            std::cerr << "delete requires URL\n";
            return false;
        }
        options->request.method = "DELETE";
        options->request.url = argv[index++];
        if (index < argc && !IsOption(argv[index])) {
            options->request.body = argv[index++];
        }
    } else if (options->command == "head") {
        if (index >= argc) {
            std::cerr << "head requires URL\n";
            return false;
        }
        options->request.method = "HEAD";
        options->request.url = argv[index++];
    } else if (options->command == "options") {
        if (index >= argc) {
            std::cerr << "options requires URL\n";
            return false;
        }
        options->request.method = "OPTIONS";
        options->request.url = argv[index++];
    } else if (options->command == "request") {
        if (index + 1 >= argc) {
            std::cerr << "request requires METHOD and URL\n";
            return false;
        }
        options->request.method = argv[index++];
        options->request.url = argv[index++];
        if (index < argc && !IsOption(argv[index])) {
            options->request.body = argv[index++];
        }
    } else if (options->command == "download") {
        if (index + 1 >= argc) {
            std::cerr << "download requires URL and OUTPUT\n";
            return false;
        }
        options->download.url = argv[index++];
        options->download.output_path = argv[index++];
    } else {
        std::cerr << "Unknown command: " << options->command << '\n';
        return false;
    }

    for (; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "-H" || arg == "--header") {
            std::string header_text;
            if (!ReadNextValue(argc, argv, &index, &header_text, arg.c_str())) {
                return false;
            }

            std::pair<std::string, std::string> header;
            if (!ParseHeaderLine(header_text, &header)) {
                std::cerr << "Invalid header format, expected KEY:VALUE\n";
                return false;
            }
            options->request.headers[header.first] = header.second;
            options->download.headers[header.first] = header.second;
            continue;
        }

        if (arg == "--timeout") {
            std::string timeout_text;
            if (!ReadNextValue(argc, argv, &index, &timeout_text, "--timeout")) {
                return false;
            }
            long timeout_ms = 0;
            if (!ParseLong(timeout_text, &timeout_ms) || timeout_ms <= 0) {
                std::cerr << "Invalid timeout value: " << timeout_text << '\n';
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
    for (int index = 1; index < argc; ++index) {
        if (IsHelpFlag(argv[index])) {
            PrintUsage(argv[0]);
            return 0;
        }
    }

    DemoOptions options;
    if (!ParseArgs(argc, argv, &options)) {
        PrintUsage(argv[0]);
        return argc <= 1 ? 0 : 1;
    }

    TlsContext tls_context(options.tls);
    HttpClient client(&tls_context);
    const auto init_result = client.initialize();
    if (!init_result) {
        std::cerr << "HTTP init failed: " << init_result.message() << '\n';
        return 1;
    }

    if (options.command == "download") {
        const auto result = client.download(options.download);
        if (!result) {
            std::cerr << "HTTP download failed: " << result.message() << '\n';
            return 1;
        }
        std::cout << "Download saved to: " << options.download.output_path << '\n';
        return 0;
    }

    const auto response = client.perform(options.request);
    PrintResponse(response);
    return response.status_code >= 200 && response.status_code < 400 ? 0 : 1;
}
