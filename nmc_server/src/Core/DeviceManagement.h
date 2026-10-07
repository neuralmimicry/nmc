#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>

namespace NMC::Server {

/**
 * Authenticated API boundary for the verified device registry and its
 * read-only discovery and guarded controller operations.
 *
 * The registry is reloaded for every request so operators can publish a new
 * inventory revision without restarting Continuum. Inventory credentials are
 * represented only by environment-variable names; secret values are never
 * returned by these handlers. Controller mutations are serialised through the
 * durable audit log and fail closed on stale or ambiguous host mappings.
 */
class DeviceManagement {
public:
    /** Construct the registry accessor using NMC_DEVICE_INVENTORY_PATH. */
    DeviceManagement();

    /** Return the current non-secret inventory projection and freshness state. */
    void handleGetInventory(const httplib::Request& req, httplib::Response& res) const;
    /** Return only configured, explicitly allowlisted Home Assistant entities. */
    void handleGetHomeAssistant(const httplib::Request& req, httplib::Response& res) const;
    /** Reconcile allowlisted Home Assistant states to exact inventory entity mappings. */
    void handleGetHomeAssistantReconciliation(const httplib::Request& req, httplib::Response& res) const;
    /** Collect bounded DHCP lease evidence from configured, authenticated sources. */
    void handleGetDhcpObservations(const httplib::Request& req, httplib::Response& res) const;
    /** Query one registered controller using its declared protocol and profile. */
    void handleGetControllerDiagnostics(const httplib::Request& req, httplib::Response& res) const;
    /** Execute an explicitly enabled, audited, idempotent controller action. */
    void handleControllerAction(const httplib::Request& req, httplib::Response& res);

private:
    struct Inventory {
        nlohmann::json document{nlohmann::json::object()};
        std::string revision;
        std::string error;
        bool valid{false};
        bool stale{true};
    };

    /** Load, schema-check and freshness-check one bounded registry snapshot. */
    Inventory loadInventory() const;
    /** Resolve a registered controller without exposing its credential values. */
    nlohmann::json getControllerDiagnostics(const Inventory& inventory,
                                            const std::string& controllerId,
                                            int& statusCode) const;
    /** Perform a guarded action and durably record its intent and outcome. */
    nlohmann::json performControllerAction(const Inventory& inventory,
                                          const std::string& controllerId,
                                          const nlohmann::json& request,
                                          int& statusCode);
    /** Send a consistent JSON response with cache storage disabled. */
    void sendJson(httplib::Response& res, int statusCode, const nlohmann::json& body) const;

    std::string inventoryPath_;
    mutable std::mutex actionMutex_;
};

} // namespace NMC::Server
