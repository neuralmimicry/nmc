// APIRoutes_ReleaseOperate.cpp
// Release/operate route registration for Refiner runtime control, VM lifecycle, and node recruitment.

#include "APIRoutes.h"
#include "K8sHandlers.h"

namespace NMC::Server {

    void APIRoutes::registerReleaseOperateRoutes(httplib::Server& svr, const APIRoutes::RouteGuard& guard) {
        svr.Get("/k8s/refiner/status", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleGetRefinerStatus(req, res);
        });
        svr.Post("/k8s/refiner/scale", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            k8sHandlers->handleScaleRefiner(req, res);
        });

        svr.Post("/vm/create", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleCreateVM(req, res);
        });
        svr.Delete(R"(/vm/delete/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleDeleteVM(req, res);
        });
        svr.Get(R"(/vm/get/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleGetVM(req, res);
        });
        svr.Get("/vm/list", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMs(req, res);
        });
        svr.Get("/vm/list-locations", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMLocations(req, res);
        });
        svr.Get("/vm/list-os", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMOSImages(req, res);
        });
        svr.Get("/vm/list-sku", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleListVMSKUs(req, res);
        });
        svr.Post(R"(/vm/restart/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleRestartVM(req, res);
        });
        svr.Post(R"(/vm/resume/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleResumeVM(req, res);
        });
        svr.Post(R"(/vm/suspend/(.*))", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleSuspendVM(req, res);
        });

        svr.Post("/node/recruit", [this, guard](const httplib::Request& req, httplib::Response& res) {
            if (!guard(req, res)) return;
            handleRecruitNode(req, res);
        });
    }

} // namespace NMC::Server
