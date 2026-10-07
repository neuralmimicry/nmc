// HTTP handlers for the scoped AWS, GCP and Azure compute adapter.
#include "APIRoutes.h"
#include "ProviderCompute.h"
#include <chrono>

namespace NMC::Server {
namespace {

int64_t providerAuditTimestampMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

void APIRoutes::handleProviderComputeStatus(const httplib::Request&, httplib::Response& res) {
    const ProviderComputeResult result = ProviderCompute::status();
    Models::CloudResponse body;
    body.success = result.success;
    body.message = result.message;
    body.data = result.data;
    sendJsonResponse(res, body);
}

void APIRoutes::handleProviderComputeList(const httplib::Request& req, httplib::Response& res) {
    if (!req.has_param("provider") || !req.has_param("scope")) {
        return sendErrorResponse(res, 400, "provider and scope query parameters are required.");
    }
    const std::string region = req.has_param("region") ? req.get_param_value("region") : "";
    const ProviderComputeResult result = ProviderCompute::list(
            req.get_param_value("provider"), req.get_param_value("scope"), region);
    if (!result.success) return sendErrorResponse(res, result.httpStatus, result.message);
    Models::CloudResponse body;
    body.success = true;
    body.message = result.message;
    body.data = result.data;
    sendJsonResponse(res, body);
}

void APIRoutes::handleProviderComputeCreate(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error&) {
        return sendErrorResponse(res, 400, "Request body must be valid JSON.");
    }
    if (!request.is_object()) return sendErrorResponse(res, 400, "Request body must be a JSON object.");

    const std::string provider = request.value("provider", std::string{});
    const std::string scope = request.value("scope", std::string{});
    const std::string providerRequestId = request.value("idempotency_key", std::string{});
    std::string name;
    if (request.contains("spec") && request["spec"].is_object()) {
        name = request["spec"].value("name", std::string{});
    }
    ProviderComputeResult result = ProviderCompute::create(request);
    const int64_t timestamp = providerAuditTimestampMs();
    recordServerStateEvent("provider_compute", provider + ":" + scope + ":" + name,
                           result.success ? "created" : "create_failed", timestamp,
                           {{"provider", provider}, {"scope", scope}, {"name", name},
                            {"success", result.success}, {"http_status", result.httpStatus},
                            {"message", result.message}, {"provider_request_id", providerRequestId},
                            {"request_id", req.get_header_value("X-Request-ID")}});
    if (!result.success) return sendErrorResponse(res, result.httpStatus, result.message);
    Models::CloudResponse body;
    body.success = true;
    body.message = result.message;
    body.data = result.data;
    sendJsonResponse(res, body);
}

void APIRoutes::handleProviderComputeAction(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error&) {
        return sendErrorResponse(res, 400, "Request body must be valid JSON.");
    }
    if (!request.is_object()) return sendErrorResponse(res, 400, "Request body must be a JSON object.");

    const std::string provider = request.value("provider", std::string{});
    const std::string scope = request.value("scope", std::string{});
    const std::string instanceId = request.value("instance_id", std::string{});
    const std::string action = request.value("action", std::string{});
    const std::string providerRequestId = request.value("request_id", std::string{});
    ProviderComputeResult result = ProviderCompute::action(request);
    const int64_t timestamp = providerAuditTimestampMs();
    recordServerStateEvent("provider_compute", provider + ":" + scope + ":" + instanceId,
                           result.success ? action : action + "_failed", timestamp,
                           {{"provider", provider}, {"scope", scope}, {"instance_id", instanceId},
                            {"action", action}, {"success", result.success},
                            {"http_status", result.httpStatus}, {"message", result.message},
                            {"provider_request_id", providerRequestId},
                            {"request_id", req.get_header_value("X-Request-ID")}});
    if (!result.success) return sendErrorResponse(res, result.httpStatus, result.message);
    Models::CloudResponse body;
    body.success = true;
    body.message = result.message;
    body.data = result.data;
    sendJsonResponse(res, body);
}

// These endpoints once changed process-local records without calling a provider.
// They now fail explicitly so clients cannot mistake simulated state for a VM.
void APIRoutes::handleCreateVM(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API has no provider-backed lifecycle. Use POST /providers/compute/instances/create with an explicit provider scope.");
}

void APIRoutes::handleDeleteVM(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API has no provider-backed lifecycle. Use POST /providers/compute/instances/action with action=delete and an explicit provider scope.");
}

void APIRoutes::handleGetVM(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API does not read provider inventory. Use GET /providers/compute/instances with an explicit provider scope.");
}

void APIRoutes::handleListVMs(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API does not read provider inventory. Use GET /providers/compute/instances with an explicit provider scope.");
}

void APIRoutes::handleListVMLocations(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API does not list provider regions. Supply an explicit region to the provider compute API.");
}

void APIRoutes::handleListVMOSImages(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API does not list provider images. Use the selected provider's image catalogue.");
}

void APIRoutes::handleListVMSKUs(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API does not list provider machine types. Use the selected provider's machine catalogue.");
}

void APIRoutes::handleRestartVM(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API cannot restart provider instances. Use POST /providers/compute/instances/action with action=restart.");
}

void APIRoutes::handleResumeVM(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API cannot start provider instances. Use POST /providers/compute/instances/action with action=start.");
}

void APIRoutes::handleSuspendVM(const httplib::Request&, httplib::Response& res) {
    sendErrorResponse(res, 410, "The legacy /vm API cannot stop provider instances. Use POST /providers/compute/instances/action with action=stop.");
}

} // namespace NMC::Server
