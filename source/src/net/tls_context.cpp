#include "hmi_nexus/net/tls_context.h"

#include <utility>

namespace hmi_nexus::net {

TlsContext::TlsContext(TlsOptions options)
    : options_(std::move(options)) {}

const TlsOptions& TlsContext::options() const {
    return options_;
}

bool TlsContext::enabled() const {
    return !options_.ca_file.empty() || !options_.client_cert_file.empty() ||
           !options_.private_key_file.empty();
}

}  // namespace hmi_nexus::net
