// APIRoutes_AiLab.cpp
// Lab-only AI-assisted adversary-emulation reporting routes.

#include "APIRoutes.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace NMC::Server {

    namespace {
#include "APIRoutes_InternalHelpers.inl"

        std::string labStringField(const nlohmann::json& payload,
                                   std::initializer_list<const char*> keys,
                                   const std::string& fallback = "") {
            for (const char* key : keys) {
                auto it = payload.find(key);
                if (it != payload.end() && it->is_string()) {
                    return trim(it->get<std::string>());
                }
            }
            return fallback;
        }

        int labIntField(const nlohmann::json& payload,
                        std::initializer_list<const char*> keys,
                        int fallback = 0) {
            for (const char* key : keys) {
                auto it = payload.find(key);
                if (it != payload.end() && it->is_number_integer()) {
                    return std::max(0, it->get<int>());
                }
            }
            return fallback;
        }

        int64_t labInt64Field(const nlohmann::json& payload,
                              std::initializer_list<const char*> keys,
                              int64_t fallback = 0) {
            for (const char* key : keys) {
                auto it = payload.find(key);
                if (it != payload.end() && it->is_number_integer()) {
                    return std::max<int64_t>(0, it->get<int64_t>());
                }
            }
            return fallback;
        }
    }

    void APIRoutes::handleAiLabStatus(const httplib::Request& req, httplib::Response& res) {
        (void) req;
        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "AI lab reports listed successfully.";

        nlohmann::json reports = nlohmann::json::array();
        int completed = 0;
        int policyStopped = 0;
        int totalActions = 0;
        int totalRejections = 0;
        int totalEvents = 0;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            for (auto it = aiLabReports.rbegin(); it != aiLabReports.rend(); ++it) {
                reports.push_back(aiLabReportToJson(*it));
                if (it->status == "completed") {
                    completed += 1;
                }
                if (it->status.find("policy") != std::string::npos) {
                    policyStopped += 1;
                }
                totalActions += std::max(0, it->actionsProposed);
                totalRejections += std::max(0, it->policyRejections);
                totalEvents += std::max(0, it->eventsEmitted);
            }
        }

        apiResponse.data = {
                {"summary", {
                        {"reports", static_cast<int>(reports.size())},
                        {"completed", completed},
                        {"policy_stopped", policyStopped},
                        {"actions_proposed", totalActions},
                        {"policy_rejections", totalRejections},
                        {"events_emitted", totalEvents},
                        {"safety_boundary", "authorised isolated lab only; typed adapters only; no model shell access"}
                }},
                {"reports", std::move(reports)}
        };
        sendJsonResponse(res, apiResponse);
    }

    void APIRoutes::handleAiLabReport(const httplib::Request& req, httplib::Response& res) {
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(req.body);
        } catch (const std::exception& error) {
            return sendErrorResponse(res, 400, std::string("Invalid JSON report: ") + error.what());
        }
        if (!payload.is_object()) {
            return sendErrorResponse(res, 400, "AI lab report must be a JSON object.");
        }

        AiLabReport report = aiLabReportFromJson(payload);
        if (report.scenarioId.empty()) {
            return sendErrorResponse(res, 400, "AI lab report is missing scenario_id.");
        }
        if (report.receivedEpochMs <= 0) {
            report.receivedEpochMs = nowEpochMs();
        }
        report.payload = payload;

        {
            std::lock_guard<std::mutex> lock(dataMutex);
            aiLabReports.push_back(report);
            while (aiLabReports.size() > aiLabReportMaxEntries) {
                aiLabReports.pop_front();
            }
            scheduleTraceyStateSnapshotLocked(report.receivedEpochMs);
        }

        Models::CloudResponse apiResponse;
        apiResponse.success = true;
        apiResponse.message = "AI lab report accepted.";
        apiResponse.data = aiLabReportToJson(report);
        sendJsonResponse(res, apiResponse);
    }

    nlohmann::json APIRoutes::aiLabReportToJson(const AiLabReport& report) const {
        return {
                {"scenario_id", report.scenarioId},
                {"scenario_name", report.scenarioName},
                {"status", report.status},
                {"evidence_root", report.evidenceRoot},
                {"received_epoch_ms", report.receivedEpochMs},
                {"started_at_ms", report.startedEpochMs},
                {"finished_at_ms", report.finishedEpochMs},
                {"actions_proposed", report.actionsProposed},
                {"actions_executed", report.actionsExecuted},
                {"policy_rejections", report.policyRejections},
                {"approvals_required", report.approvalsRequired},
                {"events_emitted", report.eventsEmitted},
                {"payload", report.payload.is_null() ? nlohmann::json::object() : report.payload}
        };
    }

    APIRoutes::AiLabReport APIRoutes::aiLabReportFromJson(const nlohmann::json& payload) const {
        AiLabReport report;
        if (!payload.is_object()) {
            return report;
        }
        report.scenarioId = labStringField(payload, {"scenario_id", "scenarioId"});
        report.scenarioName = labStringField(payload, {"scenario_name", "scenarioName", "name"});
        report.status = labStringField(payload, {"status"}, "unknown");
        report.evidenceRoot = labStringField(payload, {"evidence_root", "evidenceRoot"});
        report.receivedEpochMs = labInt64Field(payload, {"received_epoch_ms", "receivedEpochMs"}, nowEpochMs());
        report.startedEpochMs = labInt64Field(payload, {"started_at_ms", "startedAtMs"});
        report.finishedEpochMs = labInt64Field(payload, {"finished_at_ms", "finishedAtMs"});
        report.actionsProposed = labIntField(payload, {"actions_proposed", "actionsProposed"});
        report.actionsExecuted = labIntField(payload, {"actions_executed", "actionsExecuted"});
        report.policyRejections = labIntField(payload, {"policy_rejections", "policyRejections"});
        report.approvalsRequired = labIntField(payload, {"approvals_required", "approvalsRequired"});
        report.eventsEmitted = labIntField(payload, {"events_emitted", "eventsEmitted"});
        report.payload = payload;
        return report;
    }

} // namespace NMC::Server
