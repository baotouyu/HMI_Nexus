#pragma once

#include <string>

namespace hmi_nexus::net {

struct TlsOptions {
    std::string ca_file;
    std::string client_cert_file;
    std::string private_key_file;
};

class TlsContext {
public:
    explicit TlsContext(TlsOptions options = {});

    const TlsOptions& options() const;
    bool enabled() const;

private:
    TlsOptions options_;
};

}  // namespace hmi_nexus::net
