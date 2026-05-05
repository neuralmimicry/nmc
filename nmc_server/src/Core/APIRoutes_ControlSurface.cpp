// APIRoutes_ControlSurface.cpp
// Request logging, auth/session flows, access policy checks, and server metadata handlers.

#include "APIRoutes.h"
#include "Utils.h"
#include "VersionCheck.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>

namespace NMC::Server {

    namespace {
#include "APIRoutes_ExtractedCommon.inl"

        constexpr const char* SERVICE_ACCESS_NONE = "none";
        constexpr const char* SERVICE_ACCESS_REQUEST = "request";
        constexpr const char* SERVICE_ACCESS_OBSERVE = "observe";
        constexpr const char* SERVICE_ACCESS_USE = "use";
        constexpr const char* SERVICE_ACCESS_CONTROL = "control";

        struct ServiceRouteRequirement {
            std::string serviceKey;
            std::string accessLevel;
        };

        std::string normalizeServiceAccessLevel(const std::string& value,
                                                const std::string& fallback = SERVICE_ACCESS_NONE) {
            const std::string cleaned = toLower(trim(value));
            if (cleaned == SERVICE_ACCESS_NONE
                || cleaned == SERVICE_ACCESS_REQUEST
                || cleaned == SERVICE_ACCESS_OBSERVE
                || cleaned == SERVICE_ACCESS_USE
                || cleaned == SERVICE_ACCESS_CONTROL) {
                return cleaned;
            }
            return fallback;
        }

        int serviceAccessRank(const std::string& value) {
            const std::string normalized = normalizeServiceAccessLevel(value);
            if (normalized == SERVICE_ACCESS_REQUEST) {
                return 1;
            }
            if (normalized == SERVICE_ACCESS_OBSERVE) {
                return 2;
            }
            if (normalized == SERVICE_ACCESS_USE) {
                return 3;
            }
            if (normalized == SERVICE_ACCESS_CONTROL) {
                return 4;
            }
            return 0;
        }

        bool serviceAccessAtLeast(const std::string& current, const std::string& required) {
            return serviceAccessRank(current) >= serviceAccessRank(required);
        }

        std::string maxServiceAccessLevel(const std::string& left, const std::string& right) {
            return serviceAccessRank(left) >= serviceAccessRank(right)
                   ? normalizeServiceAccessLevel(left)
                   : normalizeServiceAccessLevel(right);
        }

        std::vector<std::string> normalizeStringArray(const nlohmann::json& value) {
            std::vector<std::string> normalized;
            auto pushValue = [&](const std::string& candidate) {
                const std::string cleaned = toLower(trim(candidate));
                if (cleaned.empty()) {
                    return;
                }
                if (std::find(normalized.begin(), normalized.end(), cleaned) != normalized.end()) {
                    return;
                }
                normalized.push_back(cleaned);
            };

            if (value.is_string()) {
                std::stringstream stream(value.get<std::string>());
                std::string item;
                while (std::getline(stream, item, ',')) {
                    pushValue(item);
                }
                return normalized;
            }
            if (value.is_array()) {
                for (const auto& item : value) {
                    if (item.is_string()) {
                        pushValue(item.get<std::string>());
                    }
                }
            }
            return normalized;
        }

        nlohmann::json normalizeStringArrayJson(const nlohmann::json& value) {
            nlohmann::json normalized = nlohmann::json::array();
            for (const auto& item : normalizeStringArray(value)) {
                normalized.push_back(item);
            }
            return normalized;
        }

        nlohmann::json normalizeActiveTeamClaim(const nlohmann::json& value) {
            if (value.is_null()) {
                return nlohmann::json(nullptr);
            }
            if (value.is_string()) {
                const std::string teamId = trim(value.get<std::string>());
                if (teamId.empty()) {
                    return nlohmann::json(nullptr);
                }
                return nlohmann::json{{"team_id", teamId}};
            }
            if (!value.is_object()) {
                return nlohmann::json(nullptr);
            }
            nlohmann::json normalized = value;
            std::string teamId = firstStringValue(normalized, {"team_id", "id"}, "");
            if (teamId.empty()) {
                return nlohmann::json(nullptr);
            }
            normalized["team_id"] = teamId;
            return normalized;
        }

        std::string defaultPublicServiceAccessLevel(const std::string& serviceKey) {
            const std::string normalized = toLower(trim(serviceKey));
            if (normalized == "continuum" || normalized == "tracey") {
                return SERVICE_ACCESS_OBSERVE;
            }
            if (normalized == "aarnn") {
                return SERVICE_ACCESS_REQUEST;
            }
            return SERVICE_ACCESS_NONE;
        }

        std::string defaultAuthenticatedServiceAccessLevel(const std::string& serviceKey) {
            (void)serviceKey;
            return SERVICE_ACCESS_NONE;
        }

        nlohmann::json normalizeServiceAccessEntry(const std::string& serviceKey,
                                                   const nlohmann::json& rawEntry,
                                                   const std::string& defaultPublicLevel,
                                                   const std::string& defaultAccessLevel) {
            const std::string cleanedServiceKey = toLower(trim(serviceKey));
            nlohmann::json entry = rawEntry.is_object() ? rawEntry : nlohmann::json::object();
            if (!rawEntry.is_object() && !rawEntry.is_null()) {
                entry["access_level"] = rawEntry;
            }
            const std::string publicAccessLevel = normalizeServiceAccessLevel(
                    firstStringValue(entry, {"public_access_level"}, defaultPublicLevel),
                    defaultPublicLevel
            );
            const std::string accessLevel = normalizeServiceAccessLevel(
                    firstStringValue(entry, {"access_level", "level"}, defaultAccessLevel),
                    defaultAccessLevel
            );
            const std::string visibleAccessLevel = normalizeServiceAccessLevel(
                    firstStringValue(
                            entry,
                            {"visible_access_level"},
                            maxServiceAccessLevel(accessLevel, publicAccessLevel)
                    ),
                    maxServiceAccessLevel(accessLevel, publicAccessLevel)
            );
            entry["service_key"] = cleanedServiceKey;
            entry["access_level"] = accessLevel;
            entry["public_access_level"] = publicAccessLevel;
            entry["visible_access_level"] = visibleAccessLevel;
            entry["visible"] = entry.contains("visible")
                               ? jsonBoolValue(entry["visible"], visibleAccessLevel != SERVICE_ACCESS_NONE)
                               : visibleAccessLevel != SERVICE_ACCESS_NONE;
            entry["can_request"] = entry.contains("can_request")
                                   ? jsonBoolValue(entry["can_request"], serviceAccessAtLeast(visibleAccessLevel, SERVICE_ACCESS_REQUEST))
                                   : serviceAccessAtLeast(visibleAccessLevel, SERVICE_ACCESS_REQUEST);
            entry["can_observe"] = entry.contains("can_observe")
                                   ? jsonBoolValue(entry["can_observe"], serviceAccessAtLeast(visibleAccessLevel, SERVICE_ACCESS_OBSERVE))
                                   : serviceAccessAtLeast(visibleAccessLevel, SERVICE_ACCESS_OBSERVE);
            entry["can_use"] = entry.contains("can_use")
                               ? jsonBoolValue(entry["can_use"], serviceAccessAtLeast(accessLevel, SERVICE_ACCESS_USE))
                               : serviceAccessAtLeast(accessLevel, SERVICE_ACCESS_USE);
            entry["can_control"] = entry.contains("can_control")
                                   ? jsonBoolValue(entry["can_control"], serviceAccessAtLeast(accessLevel, SERVICE_ACCESS_CONTROL))
                                   : serviceAccessAtLeast(accessLevel, SERVICE_ACCESS_CONTROL);
            return entry;
        }

        nlohmann::json buildFallbackServiceAccess(bool authenticated,
                                                  const std::string& role,
                                                  const std::vector<std::string>& groups) {
            nlohmann::json resolved = nlohmann::json::object();
            const bool isAdmin = role == "admin"
                                 || std::find(groups.begin(), groups.end(), "admin") != groups.end();
            static constexpr std::array<const char*, 5> defaultServices = {
                    "continuum",
                    "tracey",
                    "gail",
                    "gail_trading",
                    "aarnn"
            };
            for (const char* serviceKey : defaultServices) {
                std::string accessLevel = SERVICE_ACCESS_NONE;
                if (isAdmin) {
                    accessLevel = SERVICE_ACCESS_CONTROL;
                } else if (authenticated) {
                    accessLevel = defaultAuthenticatedServiceAccessLevel(serviceKey);
                }
                resolved[serviceKey] = normalizeServiceAccessEntry(
                        serviceKey,
                        nlohmann::json::object(),
                        defaultPublicServiceAccessLevel(serviceKey),
                        accessLevel
                );
            }
            return resolved;
        }

        nlohmann::json resolveServiceAccessClaims(const nlohmann::json& payload,
                                                  bool authenticated,
                                                  const std::string& role,
                                                  const std::vector<std::string>& groups) {
            nlohmann::json resolved = buildFallbackServiceAccess(authenticated, role, groups);
            if (!payload.is_object() || !payload.contains("service_access")) {
                return resolved;
            }

            auto applyEntry = [&](const std::string& rawServiceKey, const nlohmann::json& rawEntry) {
                const std::string serviceKey = toLower(trim(rawServiceKey));
                if (serviceKey.empty()) {
                    return;
                }
                std::string defaultPublicLevel = defaultPublicServiceAccessLevel(serviceKey);
                std::string defaultAccessLevel = SERVICE_ACCESS_NONE;
                auto existingIt = resolved.find(serviceKey);
                if (existingIt != resolved.end() && existingIt->is_object()) {
                    defaultPublicLevel = firstStringValue(
                            *existingIt,
                            {"public_access_level"},
                            defaultPublicLevel
                    );
                    defaultAccessLevel = firstStringValue(
                            *existingIt,
                            {"access_level"},
                            defaultAccessLevel
                    );
                }
                resolved[serviceKey] = normalizeServiceAccessEntry(
                        serviceKey,
                        rawEntry,
                        defaultPublicLevel,
                        defaultAccessLevel
                );
            };

            const nlohmann::json& rawServiceAccess = payload["service_access"];
            if (rawServiceAccess.is_object()) {
                for (const auto& item : rawServiceAccess.items()) {
                    applyEntry(item.key(), item.value());
                }
            } else if (rawServiceAccess.is_array()) {
                for (const auto& item : rawServiceAccess) {
                    if (!item.is_object()) {
                        continue;
                    }
                    applyEntry(firstStringValue(item, {"service_key", "key"}, ""), item);
                }
            }
            return resolved;
        }

        nlohmann::json visibleServiceKeys(const nlohmann::json& payload,
                                          const nlohmann::json& serviceAccess) {
            nlohmann::json visible = nlohmann::json::array();
            std::set<std::string> seen;
            auto pushVisible = [&](const std::string& rawValue) {
                const std::string cleaned = toLower(trim(rawValue));
                if (cleaned.empty() || seen.find(cleaned) != seen.end()) {
                    return;
                }
                seen.insert(cleaned);
                visible.push_back(cleaned);
            };

            if (payload.is_object() && payload.contains("visible_services")) {
                for (const auto& item : normalizeStringArray(payload["visible_services"])) {
                    pushVisible(item);
                }
            }
            if (serviceAccess.is_object()) {
                for (const auto& item : serviceAccess.items()) {
                    if (!item.value().is_object()) {
                        continue;
                    }
                    if (jsonBoolValue(item.value().value("visible", nlohmann::json(false)), false)) {
                        pushVisible(item.key());
                    }
                }
            }
            return visible;
        }

        nlohmann::json normalizeIdentityClaims(const nlohmann::json& payload,
                                               bool defaultAuthenticated = false) {
            const nlohmann::json normalizedPayload = payload.is_object()
                                                    ? payload
                                                    : nlohmann::json::object();
            bool authenticated = defaultAuthenticated;
            if (normalizedPayload.contains("authenticated")) {
                authenticated = jsonBoolValue(normalizedPayload["authenticated"], authenticated);
            } else if (normalizedPayload.contains("active")) {
                authenticated = jsonBoolValue(normalizedPayload["active"], authenticated);
            }

            const std::string user = trim(
                    firstStringValue(
                            normalizedPayload,
                            {"user", "preferred_username", "username", "email", "sub"},
                            ""
                    )
            );
            const std::string identityType = toLower(trim(firstStringValue(normalizedPayload, {"identity_type"}, "")));
            std::string role = toLower(trim(firstStringValue(
                    normalizedPayload,
                    {"role"},
                    identityType == "service_account" ? "service_account" : "user"
            )));
            if (role.empty()) {
                role = identityType == "service_account" ? "service_account" : "user";
            }
            std::vector<std::string> groups = normalizeStringArray(
                    normalizedPayload.contains("groups")
                    ? normalizedPayload["groups"]
                    : nlohmann::json()
            );
            if (role != "service_account"
                && std::find(groups.begin(), groups.end(), role) == groups.end()) {
                groups.insert(groups.begin(), role);
            }
            const bool isAdmin = (normalizedPayload.contains("is_admin")
                                  && jsonBoolValue(normalizedPayload["is_admin"], false))
                                 || role == "admin"
                                 || std::find(groups.begin(), groups.end(), "admin") != groups.end();
            const nlohmann::json activeTeam = normalizedPayload.contains("active_team")
                                              ? normalizeActiveTeamClaim(normalizedPayload["active_team"])
                                              : nlohmann::json(nullptr);
            const int64_t teamCount = normalizedPayload.contains("team_count")
                                      ? std::max<int64_t>(
                                              0,
                                              firstInt64Value(
                                                      normalizedPayload,
                                                      {"team_count"},
                                                      activeTeam.is_null() ? 0 : 1
                                              )
                                      )
                                      : (activeTeam.is_null() ? 0 : 1);
            const int64_t pendingInvitationCount = normalizedPayload.contains("pending_invitation_count")
                                                   ? std::max<int64_t>(
                                                           0,
                                                           firstInt64Value(
                                                                   normalizedPayload,
                                                                   {"pending_invitation_count"},
                                                                   0
                                                           )
                                                   )
                                                   : 0;
            const nlohmann::json groupMemberships =
                    normalizedPayload.contains("group_memberships") && normalizedPayload["group_memberships"].is_array()
                    ? normalizedPayload["group_memberships"]
                    : nlohmann::json::array();
            const nlohmann::json manageableGroups = normalizeStringArrayJson(
                    normalizedPayload.contains("manageable_groups")
                    ? normalizedPayload["manageable_groups"]
                    : nlohmann::json()
            );
            nlohmann::json visibleGroups = normalizeStringArrayJson(
                    normalizedPayload.contains("visible_groups")
                    ? normalizedPayload["visible_groups"]
                    : nlohmann::json()
            );
            if (visibleGroups.empty()) {
                visibleGroups = nlohmann::json::array();
                for (const auto& group : groups) {
                    visibleGroups.push_back(group);
                }
            }
            const bool canManageAccess = normalizedPayload.contains("can_manage_access")
                                         ? jsonBoolValue(normalizedPayload["can_manage_access"], !manageableGroups.empty())
                                         : !manageableGroups.empty();
            const nlohmann::json serviceAccess = resolveServiceAccessClaims(
                    normalizedPayload,
                    authenticated,
                    role,
                    groups
            );

            nlohmann::json claims = {
                    {"authenticated", authenticated && !user.empty()},
                    {"user", user},
                    {"identity_type", identityType.empty() ? nlohmann::json(nullptr) : nlohmann::json(identityType)},
                    {"role", role},
                    {"groups", groups},
                    {"is_admin", isAdmin},
                    {"active_team", activeTeam},
                    {"team_count", teamCount},
                    {"pending_invitation_count", pendingInvitationCount},
                    {"group_memberships", groupMemberships},
                    {"manageable_groups", manageableGroups},
                    {"visible_groups", visibleGroups},
                    {"can_manage_access", canManageAccess},
                    {"service_access", serviceAccess},
                    {"visible_services", visibleServiceKeys(normalizedPayload, serviceAccess)}
            };
            return claims;
        }

        bool serviceEntryAllows(const nlohmann::json& entry,
                                const std::string& requiredLevel) {
            if (!entry.is_object()) {
                return false;
            }
            const std::string normalizedRequired = normalizeServiceAccessLevel(requiredLevel);
            if (normalizedRequired == SERVICE_ACCESS_REQUEST) {
                return entry.contains("can_request")
                       ? jsonBoolValue(entry["can_request"], false)
                       : serviceAccessAtLeast(
                               firstStringValue(entry, {"visible_access_level"}, SERVICE_ACCESS_NONE),
                               SERVICE_ACCESS_REQUEST
                       );
            }
            if (normalizedRequired == SERVICE_ACCESS_OBSERVE) {
                return entry.contains("can_observe")
                       ? jsonBoolValue(entry["can_observe"], false)
                       : serviceAccessAtLeast(
                               firstStringValue(entry, {"visible_access_level"}, SERVICE_ACCESS_NONE),
                               SERVICE_ACCESS_OBSERVE
                       );
            }
            if (normalizedRequired == SERVICE_ACCESS_USE) {
                return entry.contains("can_use")
                       ? jsonBoolValue(entry["can_use"], false)
                       : serviceAccessAtLeast(
                               firstStringValue(entry, {"access_level"}, SERVICE_ACCESS_NONE),
                               SERVICE_ACCESS_USE
                       );
            }
            if (normalizedRequired == SERVICE_ACCESS_CONTROL) {
                return entry.contains("can_control")
                       ? jsonBoolValue(entry["can_control"], false)
                       : serviceAccessAtLeast(
                               firstStringValue(entry, {"access_level"}, SERVICE_ACCESS_NONE),
                               SERVICE_ACCESS_CONTROL
                       );
            }
            return false;
        }

        bool claimsAllowServiceAccess(const nlohmann::json& claims,
                                      const std::string& serviceKey,
                                      const std::string& requiredLevel) {
            if (serviceKey.empty()) {
                return true;
            }
            if (!claims.is_object() || !claims.contains("service_access") || !claims["service_access"].is_object()) {
                return false;
            }
            const std::string cleanedServiceKey = toLower(trim(serviceKey));
            auto it = claims["service_access"].find(cleanedServiceKey);
            if (it == claims["service_access"].end()) {
                return false;
            }
            return serviceEntryAllows(*it, requiredLevel);
        }

        ServiceRouteRequirement routeAccessRequirement(const httplib::Request& req) {
            const std::string path = req.path;
            const std::string method = toUpper(req.method);
            if (path == "/auth/session"
                || path == "/auth/session/"
                || path == "/services/health/monitoring/auth/session"
                || path == "/services/health/monitoring/auth/session/") {
                return {};
            }
            if (path.rfind("/tracey/", 0) == 0) {
                if (method == "POST") {
                    if (path == "/tracey/agents/heartbeat" || path.find("/assessment/report") != std::string::npos) {
                        return {"tracey", SERVICE_ACCESS_USE};
                    }
                    return {"tracey", SERVICE_ACCESS_CONTROL};
                }
                return {"tracey", SERVICE_ACCESS_OBSERVE};
            }
            if (path.rfind("/gail/trading/", 0) == 0) {
                if (method == "GET") {
                    return {"gail_trading", SERVICE_ACCESS_OBSERVE};
                }
                return {"gail_trading", SERVICE_ACCESS_CONTROL};
            }
            if (path.rfind("/aarnn/", 0) == 0) {
                if (path == "/aarnn/proxy/runtime") {
                    return {"aarnn", SERVICE_ACCESS_USE};
                }
                if (path == "/aarnn/proxy/control") {
                    return {"aarnn", SERVICE_ACCESS_CONTROL};
                }
                return {"aarnn", SERVICE_ACCESS_OBSERVE};
            }
            if (path.rfind("/connections", 0) == 0 || path == "/node/recruit" || path == "/k8s/refiner/scale") {
                return {"continuum", SERVICE_ACCESS_CONTROL};
            }
            if (method == "GET") {
                return {"continuum", SERVICE_ACCESS_OBSERVE};
            }
            return {"continuum", SERVICE_ACCESS_USE};
        }

        bool claimsAllowRoute(const nlohmann::json& claims, const httplib::Request& req) {
            const ServiceRouteRequirement requirement = routeAccessRequirement(req);
            if (requirement.serviceKey.empty()) {
                return true;
            }
            if (requirement.serviceKey == "gail_trading"
                && claimsAllowServiceAccess(claims, "gail", requirement.accessLevel)) {
                return true;
            }
            return claimsAllowServiceAccess(claims, requirement.serviceKey, requirement.accessLevel);
        }

        nlohmann::json serviceTokenClaims() {
            return normalizeIdentityClaims(
                    nlohmann::json{
                            {"authenticated", true},
                            {"user", "service-token"},
                            {"role", "admin"},
                            {"groups", nlohmann::json::array({"admin"})}
                    },
                    true
            );
        }
    }

    void APIRoutes::logRequest(const httplib::Request& req, const httplib::Response& res) const {
        const std::string requestId = res.get_header_value("X-Request-ID").empty()
                ? req.get_header_value("X-Request-ID")
                : res.get_header_value("X-Request-ID");

        std::cout << "[" << req.method << "] " << req.path;
        if (!requestId.empty()) {
            std::cout << " request_id=" << requestId;
        }
        if (!req.body.empty() && shouldLogBody(req.path)) {
            std::cout << " Body: " << redactBody(req.body);
        } else if (!req.body.empty()) {
            std::cout << " Body: [redacted]";
        }
        std::cout << std::endl;
    }

    void APIRoutes::ensureRequestId(const httplib::Request& req, httplib::Response& res) const {
        const std::string existing = req.get_header_value("X-Request-ID");
        if (!existing.empty()) {
            res.set_header("X-Request-ID", existing);
            return;
        }
        res.set_header("X-Request-ID", Utils::generateUniqueId("req"));
    }

    bool APIRoutes::enforceBodyLimit(const httplib::Request& req, httplib::Response& res) const {
        if (req.body.size() > maxBodyBytes) {
            sendErrorResponse(res, 413, "Request body exceeds maximum allowed size.");
            return false;
        }
        return true;
    }

    std::string APIRoutes::extractAuthToken(const httplib::Request& req) const {
        const std::string bearer = trim(req.get_header_value("Authorization"));
        if (!bearer.empty()) {
            const std::string prefix = "Bearer ";
            if (bearer.rfind(prefix, 0) == 0) {
                return trim(bearer.substr(prefix.size()));
            }
        }
        return trim(req.get_header_value("X-NMC-Token"));
    }

    nlohmann::json APIRoutes::centralAuthClaimsJson(const CentralAuthCacheEntry& entry) const {
        if (entry.claims.is_object()) {
            return entry.claims;
        }
        return {
                {"authenticated", entry.authenticated},
                {"user", entry.user}
        };
    }

    bool APIRoutes::validateCentralAuthToken(const std::string& token, nlohmann::json* claimsOut) const {
        const std::string trimmedToken = trim(token);
        if (trimmedToken.empty() || centralAuthSessionUrl.empty()) {
            return false;
        }

        const int64_t nowMs = nowEpochMs();
        {
            std::lock_guard<std::mutex> lock(centralAuthCacheMutex);
            auto it = centralAuthTokenCache.find(trimmedToken);
            if (it != centralAuthTokenCache.end()) {
                if (it->second.expiresAtMs > nowMs) {
                    if (it->second.authenticated && !it->second.user.empty() && claimsOut) {
                        *claimsOut = centralAuthClaimsJson(it->second);
                    }
                    return it->second.authenticated && !it->second.user.empty();
                }
                centralAuthTokenCache.erase(it);
            }
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(centralAuthSessionUrl, endpoint)) {
            return false;
        }
        const std::string sessionPath = endpoint.basePath.empty() ? "/api/session" : endpoint.basePath;
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (centralAuthTimeoutMs + 999) / 1000));
        httplib::Headers headers{
                {"Accept", "application/json"},
                {"Authorization", "Bearer " + trimmedToken}
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(centralAuthTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(sessionPath.c_str(), headers);
#else
            return false;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Get(sessionPath.c_str(), headers);
        }

        nlohmann::json payload = nlohmann::json::object();
        if (result && result->status < 500 && !result->body.empty()) {
            const auto parsed = nlohmann::json::parse(result->body, nullptr, false);
            if (!parsed.is_discarded() && parsed.is_object()) {
                payload = parsed;
            }
        }

        const nlohmann::json normalizedClaims = normalizeIdentityClaims(payload, false);
        const bool authenticated = normalizedClaims.value("authenticated", false);
        const std::string user = trim(normalizedClaims.value("user", ""));
        CentralAuthCacheEntry cacheEntry;
        cacheEntry.authenticated = authenticated && !user.empty();
        cacheEntry.user = user;
        cacheEntry.claims = normalizedClaims;
        cacheEntry.expiresAtMs = nowMs + (cacheEntry.authenticated
                                          ? centralAuthCacheTtlMs
                                          : std::min<int64_t>(centralAuthCacheTtlMs, 2000));
        {
            std::lock_guard<std::mutex> lock(centralAuthCacheMutex);
            if (centralAuthTokenCache.size() > 1024) {
                centralAuthTokenCache.clear();
            }
            centralAuthTokenCache[trimmedToken] = cacheEntry;
        }

        if (cacheEntry.authenticated && claimsOut) {
            *claimsOut = centralAuthClaimsJson(cacheEntry);
        }
        return cacheEntry.authenticated;
    }

    void APIRoutes::handleAuthLogin(const httplib::Request& req, httplib::Response& res) {
        ensureRequestId(req, res);
        if (!enforceBodyLimit(req, res)) {
            return;
        }
        if (centralAuthLoginUrl.empty()) {
            sendErrorResponse(res, 503, "Central username/password login is not configured.");
            return;
        }

        const auto payload = nlohmann::json::parse(req.body, nullptr, false);
        if (payload.is_discarded() || !payload.is_object()) {
            sendErrorResponse(res, 400, "Invalid JSON payload.");
            return;
        }
        const std::string username = trim(payload.value("username", ""));
        const std::string password = trim(payload.value("password", ""));
        if (username.empty() || password.empty()) {
            sendErrorResponse(res, 400, "Username and password are required.");
            return;
        }

        TraceyEndpoint endpoint;
        if (!parseTraceyEndpoint(centralAuthLoginUrl, endpoint)) {
            sendErrorResponse(res, 503, "Central username/password login endpoint is invalid.");
            return;
        }
        const std::string loginPath = endpoint.basePath.empty() ? "/api/login" : endpoint.basePath;
        const int timeoutSec = static_cast<int>(std::max<int64_t>(1, (centralAuthTimeoutMs + 999) / 1000));
        httplib::Headers headers{
                {"Accept", "application/json"},
                {"Content-Type", "application/json"}
        };

        httplib::Result result;
        if (endpoint.https) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient client(endpoint.host, endpoint.port);
            client.enable_server_certificate_verification(centralAuthTlsVerify);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Post(loginPath.c_str(), headers, payload.dump(), "application/json");
#else
            sendErrorResponse(res, 503, "HTTPS central auth login is unavailable: httplib lacks OpenSSL support.");
            return;
#endif
        } else {
            httplib::Client client(endpoint.host, endpoint.port);
            client.set_connection_timeout(timeoutSec);
            client.set_read_timeout(timeoutSec);
            client.set_write_timeout(timeoutSec);
            result = client.Post(loginPath.c_str(), headers, payload.dump(), "application/json");
        }

        if (!result) {
            sendErrorResponse(res, 502, "Central auth login request failed.");
            return;
        }

        res.status = result->status;
        res.set_content(result->body.empty() ? "{}" : result->body, "application/json");
    }

    void APIRoutes::handleAuthSession(const httplib::Request& req, httplib::Response& res) {
        ensureRequestId(req, res);
        const std::string token = extractAuthToken(req);
        nlohmann::json claims;
        if (authMode == "token") {
            if (validateCentralAuthToken(token, &claims)) {
                res.status = 200;
                res.set_content(claims.dump(), "application/json");
                return;
            }
            if (!token.empty() && !authToken.empty() && token == authToken) {
                res.status = 200;
                res.set_content(serviceTokenClaims().dump(), "application/json");
                return;
            }
        } else if (authMode == "oidc") {
            if (!oidcValidator || !oidcValidator->isConfigured()) {
                sendErrorResponse(res, 500, "OIDC authentication is misconfigured.");
                return;
            }
            std::string errorMessage;
            if (oidcValidator->validateToken(token, errorMessage, &claims)) {
                const nlohmann::json normalizedClaims = normalizeIdentityClaims(claims, true);
                res.status = 200;
                res.set_content(normalizedClaims.dump(), "application/json");
                return;
            }
        }
        if (!token.empty() && !authToken.empty() && token == authToken) {
            res.status = 200;
            res.set_content(serviceTokenClaims().dump(), "application/json");
            return;
        }
        res.set_header("WWW-Authenticate", "Bearer");
        sendErrorResponse(res, 401, "Unauthorized.");
    }

    bool APIRoutes::authorizeOrReject(const httplib::Request& req, httplib::Response& res) const {
        if (!authRequired) {
            return true;
        }
        if (authMode == "token") {
            const std::string token = extractAuthToken(req);
            nlohmann::json claims;
            if (validateCentralAuthToken(token, &claims)) {
                if (!claimsAllowRoute(claims, req)) {
                    sendErrorResponse(res, 403, "Forbidden.");
                    return false;
                }
                return true;
            }
            if (!token.empty() && !authToken.empty() && token == authToken) {
                if (!claimsAllowRoute(serviceTokenClaims(), req)) {
                    sendErrorResponse(res, 403, "Forbidden.");
                    return false;
                }
                return true;
            }
            res.set_header("WWW-Authenticate", "Bearer");
            sendErrorResponse(res, 401, "Unauthorized.");
            return false;
        }

        if (authMode == "oidc") {
            if (!oidcValidator || !oidcValidator->isConfigured()) {
                sendErrorResponse(res, 500, "OIDC authentication is misconfigured.");
                return false;
            }
            const std::string bearer = req.get_header_value("Authorization");
            std::string token;
            if (!bearer.empty()) {
                const std::string prefix = "Bearer ";
                if (bearer.rfind(prefix, 0) == 0) {
                    token = bearer.substr(prefix.size());
                }
            }
            if (token.empty()) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }

            std::string errorMessage;
            nlohmann::json claims;
            if (!oidcValidator->validateToken(token, errorMessage, &claims)) {
                res.set_header("WWW-Authenticate", "Bearer");
                sendErrorResponse(res, 401, "Unauthorized.");
                return false;
            }
            const nlohmann::json normalizedClaims = normalizeIdentityClaims(claims, true);
            if (!claimsAllowRoute(normalizedClaims, req)) {
                sendErrorResponse(res, 403, "Forbidden.");
                return false;
            }
            return true;
        }

        sendErrorResponse(res, 500, "Unsupported authentication mode.");
        return false;
    }

    bool APIRoutes::shouldLogBody(const std::string& path) const {
        // Redact known sensitive endpoints (keys, scripts, credentials, provisioning).
        const std::vector<std::string> redactedPrefixes = {
                "/ssh/create",
                "/vm/create",
                "/model/upload",
                "/connections/make",
                "/auth/login",
                "/openshift/clusters/request",
                "/openstack/clusters/request",
                "/proxmox/clusters/request",
                "/node/recruit",
                "/services/health/monitoring/auth/login"
        };
        for (const auto& prefix : redactedPrefixes) {
            if (path.rfind(prefix, 0) == 0) {
                return false;
            }
        }
        return true;
    }

    std::string APIRoutes::redactBody(const std::string& body) const {
        if (body.size() <= maxLogBodyBytes) {
            return body;
        }
        return body.substr(0, maxLogBodyBytes) + "...(truncated)";
    }

    void APIRoutes::handleServerVersion(const httplib::Request& req, httplib::Response& res) {
        (void)req;

        const auto releaseInfo = VersionCheck::checkLatestRelease();

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "Server version information retrieved successfully.";
        apiResponse.data = {
                {"component", "nmc_server"},
                {"current_version", releaseInfo.currentVersion},
                {"repository", releaseInfo.repository},
                {"release_check", {
                        {"attempted", true},
                        {"succeeded", releaseInfo.checkSucceeded},
                        {"latest_version", releaseInfo.latestVersion},
                        {"update_available", releaseInfo.updateAvailable},
                        {"message", releaseInfo.message}
                }}
        };
        sendJsonResponse(res, apiResponse);
    }

} // namespace NMC::Server
