#pragma once
#include <string>

namespace NMC::Server {
// Bound both new connections and writes on a connection whose server has
// disappeared. connect_timeout alone does not bound an established socket.
inline std::string boundedPostgresDsn(const std::string& dsn) {
    if (dsn.empty()) return dsn;
    const bool uri = dsn.rfind("postgres://", 0) == 0 || dsn.rfind("postgresql://", 0) == 0;
    const char* bounds = uri
            ? "connect_timeout=3&tcp_user_timeout=5000&keepalives=1&keepalives_idle=5&keepalives_interval=2&keepalives_count=2"
            : "connect_timeout=3 tcp_user_timeout=5000 keepalives=1 keepalives_idle=5 keepalives_interval=2 keepalives_count=2";
    return dsn + (uri ? (dsn.find('?') == std::string::npos ? "?" : "&") : " ") + bounds;
}
}
