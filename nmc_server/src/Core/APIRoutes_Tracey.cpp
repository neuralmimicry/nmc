// APIRoutes_Tracey.cpp
// Tracey route registration for fleet telemetry, adaptive planning, assessment, and agent control surfaces.

#include "APIRoutes.h"

namespace NMC::Server {

    void APIRoutes::registerTraceyRoutes(httplib::Server& svr, const APIRoutes::RouteGuard& guard) {
        svr.Post("/tracey/agents/heartbeat", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyHeartbeat(req, res);
        });
        svr.Get("/tracey/agents", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListTraceyAgents(req, res);
        });
        svr.Get("/tracey/analytics", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAnalytics(req, res);
        });
        svr.Get("/tracey/fleet", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyFleet(req, res);
        });
        svr.Get("/tracey/adaptive", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAdaptive(req, res);
        });
        svr.Get("/tracey/cve/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyCveStatus(req, res);
        });
        svr.Get("/tracey/assessment/fleet", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAssessmentFleet(req, res);
        });
        svr.Get("/tracey/ai-lab", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAiLabStatus(req, res);
        });
        svr.Post("/tracey/ai-lab/report", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleAiLabReport(req, res);
        });
        svr.Get("/tracey/racks", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListTraceyRacks(req, res);
        });
        svr.Get(R"(/tracey/racks/([^/]+))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyRackDetails(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/analysis)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentAnalysis(req, res);
        });
        svr.Get(R"(/tracey/agents/([^/]+)/server)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentServer(req, res);
        });
        svr.Get(R"(/tracey/agents/([^/]+)/gpus/([^/]+)/telemetry)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentGpu(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/assessment/plan)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAssessmentPlan(req, res);
        });
        svr.Post(R"(/tracey/agents/(.*)/assessment/report)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAssessmentReport(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/compromise)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentCompromise(req, res);
        });
        svr.Post(R"(/tracey/agents/(.*)/control)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentControl(req, res);
        });
        svr.Get(R"(/tracey/agents/(.*)/deepdive)", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleTraceyAgentDeepDive(req, res);
        });
    }

} // namespace NMC::Server
