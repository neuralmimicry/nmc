// Private helper subset shared by extracted APIRoutes implementation units.

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t");
    return value.substr(start, end - start + 1);
}

std::string toLower(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string toUpper(std::string value) {
    for (auto& ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string normalizeTraceyStatus(const std::string& rawStatus) {
    const std::string status = toLower(rawStatus);
    if (status == "ok" || status == "ready" || status == "running" || status == "healthy") {
        return "healthy";
    }
    if (status == "degraded" || status == "warning" || status == "warn") {
        return "degraded";
    }
    if (status == "offline" || status == "down" || status == "failed" || status == "error") {
        return "offline";
    }
    if (status.empty()) {
        return "unknown";
    }
    return status;
}

int64_t nowEpochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string firstStringValue(const nlohmann::json& objectNode,
                             std::initializer_list<const char*> keys,
                             const std::string& fallback = "") {
    if (!objectNode.is_object()) {
        return fallback;
    }
    for (const char* key : keys) {
        const auto it = objectNode.find(key);
        if (it == objectNode.end() || !it->is_string()) {
            continue;
        }
        const std::string value = trim(it->get<std::string>());
        if (!value.empty()) {
            return value;
        }
    }
    return fallback;
}

int64_t firstInt64Value(const nlohmann::json& objectNode,
                        std::initializer_list<const char*> keys,
                        int64_t fallback) {
    if (!objectNode.is_object()) {
        return fallback;
    }
    for (const char* key : keys) {
        const auto it = objectNode.find(key);
        if (it == objectNode.end() || it->is_null()) {
            continue;
        }
        if (it->is_number_integer()) {
            return it->get<int64_t>();
        }
        if (it->is_number_unsigned()) {
            return static_cast<int64_t>(it->get<uint64_t>());
        }
        if (it->is_string()) {
            const std::string value = trim(it->get<std::string>());
            if (value.empty()) {
                continue;
            }
            try {
                return std::stoll(value);
            } catch (const std::exception&) {
                continue;
            }
        }
    }
    return fallback;
}

const nlohmann::json* firstValue(const nlohmann::json& objectNode,
                                 std::initializer_list<const char*> keys) {
    if (!objectNode.is_object()) {
        return nullptr;
    }
    for (const char* key : keys) {
        const auto it = objectNode.find(key);
        if (it != objectNode.end() && !it->is_null()) {
            return &(*it);
        }
    }
    return nullptr;
}

const nlohmann::json* firstObjectValue(const nlohmann::json& objectNode,
                                       std::initializer_list<const char*> keys) {
    const auto* value = firstValue(objectNode, keys);
    return value != nullptr && value->is_object() ? value : nullptr;
}

const nlohmann::json* firstArrayValue(const nlohmann::json& objectNode,
                                      std::initializer_list<const char*> keys) {
    const auto* value = firstValue(objectNode, keys);
    return value != nullptr && value->is_array() ? value : nullptr;
}

double jsonDoubleValue(const nlohmann::json& node, double fallback) {
    if (node.is_number_float()) {
        return node.get<double>();
    }
    if (node.is_number_integer()) {
        return static_cast<double>(node.get<int64_t>());
    }
    if (node.is_number_unsigned()) {
        return static_cast<double>(node.get<uint64_t>());
    }
    if (node.is_string()) {
        const std::string value = trim(node.get<std::string>());
        if (value.empty()) {
            return fallback;
        }
        try {
            return std::stod(value);
        } catch (const std::exception&) {
            return fallback;
        }
    }
    if (node.is_boolean()) {
        return node.get<bool>() ? 1.0 : 0.0;
    }
    return fallback;
}

bool jsonBoolValue(const nlohmann::json& node, bool fallback) {
    if (node.is_boolean()) {
        return node.get<bool>();
    }
    if (node.is_number_integer()) {
        return node.get<int64_t>() != 0;
    }
    if (node.is_number_unsigned()) {
        return node.get<uint64_t>() != 0;
    }
    if (node.is_string()) {
        const std::string value = toLower(trim(node.get<std::string>()));
        if (value == "true" || value == "yes" || value == "1" || value == "on") {
            return true;
        }
        if (value == "false" || value == "no" || value == "0" || value == "off") {
            return false;
        }
    }
    return fallback;
}

double firstDoubleValue(const nlohmann::json& objectNode,
                        std::initializer_list<const char*> keys,
                        double fallback) {
    const auto* value = firstValue(objectNode, keys);
    return value != nullptr ? jsonDoubleValue(*value, fallback) : fallback;
}

struct TraceyEndpoint {
    std::string host;
    int port{80};
    bool https{false};
    std::string basePath;
    std::string normalized;
};

bool parseTraceyEndpoint(const std::string& rawValue, TraceyEndpoint& endpoint) {
    std::string value = trim(rawValue);
    if (value.empty()) {
        return false;
    }

    if (value.rfind("http://", 0) != 0 && value.rfind("https://", 0) != 0) {
        value = "http://" + value;
    }

    std::string rest;
    if (value.rfind("https://", 0) == 0) {
        endpoint.https = true;
        endpoint.port = 443;
        rest = value.substr(8);
    } else if (value.rfind("http://", 0) == 0) {
        endpoint.https = false;
        endpoint.port = 80;
        rest = value.substr(7);
    } else {
        return false;
    }

    const auto slashPos = rest.find('/');
    std::string hostPort = slashPos == std::string::npos ? rest : rest.substr(0, slashPos);
    endpoint.basePath = slashPos == std::string::npos ? "" : rest.substr(slashPos);
    if (hostPort.empty()) {
        return false;
    }
    while (!endpoint.basePath.empty() && endpoint.basePath.back() == '/') {
        endpoint.basePath.pop_back();
    }
    if (endpoint.basePath == "/") {
        endpoint.basePath.clear();
    }

    std::string host;
    std::string portPart;
    if (!hostPort.empty() && hostPort.front() == '[') {
        const auto close = hostPort.find(']');
        if (close == std::string::npos) {
            return false;
        }
        host = hostPort.substr(1, close - 1);
        if (close + 1 < hostPort.size()) {
            if (hostPort[close + 1] != ':') {
                return false;
            }
            portPart = hostPort.substr(close + 2);
        }
    } else {
        const auto colonPos = hostPort.rfind(':');
        if (colonPos != std::string::npos && hostPort.find(':') == colonPos) {
            host = hostPort.substr(0, colonPos);
            portPart = hostPort.substr(colonPos + 1);
        } else {
            host = hostPort;
        }
    }

    if (host.empty()) {
        return false;
    }

    if (!portPart.empty()) {
        try {
            endpoint.port = std::stoi(portPart);
        } catch (const std::exception&) {
            return false;
        }
    }

    if (endpoint.port <= 0 || endpoint.port > 65535) {
        return false;
    }

    endpoint.host = host;
    const bool hostIsIpv6 = host.find(':') != std::string::npos;
    endpoint.normalized = endpoint.https ? "https://" : "http://";
    endpoint.normalized += hostIsIpv6 ? "[" + host + "]" : host;
    endpoint.normalized += ":" + std::to_string(endpoint.port);
    endpoint.normalized += endpoint.basePath;
    return true;
}

nlohmann::json limitedJsonArray(nlohmann::json values, std::size_t limit) {
    if (!values.is_array()) {
        return nlohmann::json::array();
    }
    if (values.size() > limit) {
        values.erase(values.begin() + static_cast<nlohmann::json::difference_type>(limit), values.end());
    }
    return values;
}
