// HTTP handlers for the scoped AWS, GCP and Azure compute adapter.
#include "APIRoutes.h"
#include "ProviderCompute.h"

#include <cstddef>

namespace NMC::Server {
namespace {

constexpr std::size_t MaximumProviderRequestBytes = 256 * 1024;

} // namespace

void APIRoutes::handleProviderComputeStatus(const httplib::Request&, httplib::Response& res) {
    ProviderComputeResult result = ProviderCompute::status();
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
    submitProviderComputeJob("list", {{"provider", req.get_param_value("provider")},
                                       {"scope", req.get_param_value("scope")},
                                       {"region", region}}, req, res);
}

void APIRoutes::handleProviderComputeCreate(const httplib::Request& req, httplib::Response& res) {
    if (req.body.size() > MaximumProviderRequestBytes) {
        return sendErrorResponse(res, 413, "Provider operation request exceeds the 256 KiB limit.");
    }
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error&) {
        return sendErrorResponse(res, 400, "Request body must be valid JSON.");
    }
    if (!request.is_object()) return sendErrorResponse(res, 400, "Request body must be a JSON object.");

    submitProviderComputeJob("create", request, req, res);
}

void APIRoutes::handleProviderComputeAction(const httplib::Request& req, httplib::Response& res) {
    if (req.body.size() > MaximumProviderRequestBytes) {
        return sendErrorResponse(res, 413, "Provider operation request exceeds the 256 KiB limit.");
    }
    nlohmann::json request;
    try {
        request = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error&) {
        return sendErrorResponse(res, 400, "Request body must be valid JSON.");
    }
    if (!request.is_object()) return sendErrorResponse(res, 400, "Request body must be a JSON object.");

    submitProviderComputeJob("action", request, req, res);
}

void APIRoutes::submitProviderComputeJob(const std::string& operation,
                                         const nlohmann::json& request,
                                         const httplib::Request&,
                                         httplib::Response& res) {
    if (!providerComputeJobs || !providerComputeJobs->available()) {
        return sendErrorResponse(res, 503, "Provider job execution is unavailable; no provider operation was launched.");
    }
    const auto submission = providerComputeJobs->submit(operation, request);
    if (!submission.accepted) return sendErrorResponse(res, submission.httpStatus, submission.message);

    Models::CloudResponse body;
    body.success = true;
    body.message = submission.message;
    body.data = submission.job;
    sendJsonResponse(res, body);
    res.status = 202;
    res.set_header("Location", "/providers/compute/jobs/" + submission.job.value("job_id", std::string{}));
}

void APIRoutes::handleProviderComputeJob(const httplib::Request& req, httplib::Response& res) {
    if (!providerComputeJobs || req.matches.size() < 2) {
        return sendErrorResponse(res, 503, "Provider job lookup is unavailable.");
    }
    const auto job = providerComputeJobs->get(req.matches[1]);
    if (!job) return sendErrorResponse(res, 404, "Provider job was not found or has expired.");
    Models::CloudResponse body;
    body.success = true;
    body.message = "Provider job status retrieved.";
    body.data = *job;
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
