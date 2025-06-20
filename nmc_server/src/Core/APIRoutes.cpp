// server/APIRoutes.cpp
#include "APIRoutes.h"
#include "Utils.h"
#include <iostream>
#include <algorithm> // For std::remove_if, std::find_if
#include <set>       // For unique locations/types

namespace NMC {
    namespace Server {

        APIRoutes::APIRoutes(httplib::Server& svr) {
            // Enable logging of all requests
            svr.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
                logRequest(req);
            });

            // --- Bucket Routes ---
            svr.Post("/bucket/create", [this](const httplib::Request& req, httplib::Response& res) {
                handleCreateBucket(req, res);
            });
            svr.Delete(R"(/bucket/delete/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleDeleteBucket(req, res);
            });
            svr.Get(R"(/bucket/get/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleGetBucket(req, res);
            });
            svr.Get("/bucket/list", [this](const httplib::Request& req, httplib::Response& res) {
                handleListBuckets(req, res);
            });
            svr.Get("/bucket/list-locations", [this](const httplib::Request& req, httplib::Response& res) {
                handleListBucketLocations(req, res);
            });
            svr.Get("/bucket/list-types", [this](const httplib::Request& req, httplib::Response& res) {
                handleListBucketTypes(req, res);
            });
            svr.Post(R"(/bucket/reset-key/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleResetBucketKey(req, res);
            });

            // --- K8s Routes ---
            svr.Post("/k8s/create", [this](const httplib::Request& req, httplib::Response& res) {
                handleCreateK8sCluster(req, res);
            });
            svr.Delete(R"(/k8s/delete/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleDeleteK8sCluster(req, res);
            });
            svr.Get(R"(/k8s/get/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleGetK8sCluster(req, res);
            });
            svr.Get(R"(/k8s/get-config/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleGetKubeConfig(req, res);
            });
            svr.Get("/k8s/list", [this](const httplib::Request& req, httplib::Response& res) {
                handleListK8sClusters(req, res);
            });
            svr.Get("/k8s/list-locations", [this](const httplib::Request& req, httplib::Response& res) {
                handleListK8sLocations(req, res);
            });
            svr.Post(R"(/k8s/resume/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleResumeK8sCluster(req, res);
            });
            svr.Post(R"(/k8s/suspend/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleSuspendK8sCluster(req, res);
            });

            // --- Model Routes ---
            svr.Post("/model/upload", [this](const httplib::Request& req, httplib::Response& res) {
                handleUploadModel(req, res);
            });

            // --- SSH Routes ---
            svr.Post("/ssh/create", [this](const httplib::Request& req, httplib::Response& res) {
                handleCreateSSHKey(req, res);
            });
            svr.Delete(R"(/ssh/delete/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleDeleteSSHKey(req, res);
            });
            svr.Get("/ssh/list", [this](const httplib::Request& req, httplib::Response& res) {
                handleListSSHKeys(req, res);
            });

            // --- VM Routes ---
            svr.Post("/vm/create", [this](const httplib::Request& req, httplib::Response& res) {
                handleCreateVM(req, res);
            });
            svr.Delete(R"(/vm/delete/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleDeleteVM(req, res);
            });
            svr.Get(R"(/vm/get/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleGetVM(req, res);
            });
            svr.Get("/vm/list", [this](const httplib::Request& req, httplib::Response& res) {
                handleListVMs(req, res);
            });
            svr.Get("/vm/list-locations", [this](const httplib::Request& req, httplib::Response& res) {
                handleListVMLocations(req, res);
            });
            svr.Get("/vm/list-os", [this](const httplib::Request& req, httplib::Response& res) {
                handleListVMOSImages(req, res);
            });
            svr.Get("/vm/list-sku", [this](const httplib::Request& req, httplib::Response& res) {
                handleListVMSKUs(req, res);
            });
            svr.Post(R"(/vm/restart/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleRestartVM(req, res);
            });
            svr.Post(R"(/vm/resume/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleResumeVM(req, res);
            });
            svr.Post(R"(/vm/suspend/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
                handleSuspendVM(req, res);
            });
        }

        void APIRoutes::logRequest(const httplib::Request& req) const {
            std::cout << "[" << req.method << "] " << req.path;
            if (!req.body.empty()) {
                std::cout << " Body: " << req.body;
            }
            std::cout << std::endl;
        }

        void APIRoutes::sendJsonResponse(httplib::Response& res, const Models::CloudResponse& apiResponse) const {
            res.set_header("Content-Type", "application/json");
            res.status = apiResponse.success ? 200 : 400; // 200 for success, 400 for client error
            res.body = apiResponse.toJson().dump(4); // Pretty print JSON
        }

        void APIRoutes::sendErrorResponse(httplib::Response& res, int status, const std::string& message) const {
            Models::CloudResponse apiResponse;
            apiResponse.success = false;
            apiResponse.message = message;
            apiResponse.data = nlohmann::json({});
            sendJsonResponse(res, apiResponse);
            res.status = status; // Set HTTP status code for error
        }

// --- Bucket Handlers ---

        void APIRoutes::handleCreateBucket(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string name = json_body.value("name", "");
                std::string location = json_body.value("location", "");
                std::string type = json_body.value("type", "");

                if (name.empty() || location.empty() || type.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: name, location, or type.");
                }

                std::lock_guard<std::mutex> lock(dataMutex);
                // Check if bucket already exists
                auto it = std::find_if(mockBuckets.begin(), mockBuckets.end(),
                                       [&](const Models::Bucket& b) { return b.name == name; });
                if (it != mockBuckets.end()) {
                    return sendErrorResponse(res, 409, "Bucket '" + name + "' already exists."); // Conflict
                }

                Models::Bucket newBucket;
                newBucket.name = name;
                newBucket.location = location;
                newBucket.type = type;
                newBucket.status = "Active";
                mockBuckets.push_back(newBucket);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Bucket '" + name + "' created successfully.";
                apiResponse.data = newBucket.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void APIRoutes::handleDeleteBucket(const httplib::Request& req, httplib::Response& res) {
            std::string name = req.matches[1]; // Get name from regex capture

            std::lock_guard<std::mutex> lock(dataMutex);
            auto initial_size = mockBuckets.size();
            mockBuckets.erase(std::remove_if(mockBuckets.begin(), mockBuckets.end(),
                                             [&](const Models::Bucket& b) { return b.name == name; }),
                              mockBuckets.end());

            if (mockBuckets.size() < initial_size) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Bucket '" + name + "' deleted successfully.";
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
            }
        }

        void APIRoutes::handleGetBucket(const httplib::Request& req, httplib::Response& res) {
            std::string name = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockBuckets.begin(), mockBuckets.end(),
                                   [&](const Models::Bucket& b) { return b.name == name; });

            if (it != mockBuckets.end()) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Bucket details retrieved.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
            }
        }

        void APIRoutes::handleListBuckets(const httplib::Request& req, httplib::Response& res) {
            std::string filterName = req.get_param_value("filter-name");

            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json bucketList = nlohmann::json::array();
            for (const auto& bucket : mockBuckets) {
                if (filterName.empty() || bucket.name.find(filterName) != std::string::npos) {
                    bucketList.push_back(bucket.toJson());
                }
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Buckets listed successfully.";
            apiResponse.data = bucketList;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleListBucketLocations(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            // In a real system, these would come from a configuration or backend service
            nlohmann::json locations = {
                    {"name", "eu-central"},
                    {"name", "us-east"},
                    {"name", "asia-southeast"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket locations listed successfully.";
            apiResponse.data = locations;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleListBucketTypes(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json types = {
                    {"name", "standard"},
                    {"name", "archive"},
                    {"name", "coldline"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "Bucket types listed successfully.";
            apiResponse.data = types;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleResetBucketKey(const httplib::Request& req, httplib::Response& res) {
            std::string name = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockBuckets.begin(), mockBuckets.end(),
                                   [&](const Models::Bucket& b) { return b.name == name; });

            if (it != mockBuckets.end()) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Bucket '" + name + "' access credentials reset successfully.";
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "Bucket '" + name + "' not found.");
            }
        }

// --- K8s Handlers ---
        void APIRoutes::handleCreateK8sCluster(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string name = json_body.value("name", "");
                std::string region = json_body.value("region", "");

                if (name.empty() || region.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: name or region.");
                }

                std::lock_guard<std::mutex> lock(dataMutex);
                auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                       [&](const Models::K8sCluster& c) { return c.name == name; });
                if (it != mockK8sClusters.end()) {
                    return sendErrorResponse(res, 409, "K8s cluster '" + name + "' already exists.");
                }

                Models::K8sCluster newCluster;
                newCluster.id = Utils::generateUniqueId("k8s");
                newCluster.name = name;
                newCluster.region = region;
                newCluster.status = "Provisioning";
                mockK8sClusters.push_back(newCluster);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + name + "' created successfully.";
                apiResponse.data = newCluster.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void APIRoutes::handleDeleteK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto initial_size = mockK8sClusters.size();
            mockK8sClusters.erase(std::remove_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                                 [&](const Models::K8sCluster& c) { return c.id == id; }),
                                  mockK8sClusters.end());

            if (mockK8sClusters.size() < initial_size) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + id + "' deleted successfully.";
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        void APIRoutes::handleGetK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster details retrieved.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        void APIRoutes::handleGetKubeConfig(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                std::string kubeconfig =
                        "apiVersion: v1\n"
                        "clusters:\n"
                        "- cluster:\n"
                        "    server: https://kube." + id + ".example.com\n"
                                                           "    certificate-authority-data: REDACTED\n"
                                                           "  name: " + it->name + "\n"
                                                                                   "contexts:\n"
                                                                                   "- context:\n"
                                                                                   "    cluster: " + it->name + "\n"
                                                                                                                "    user: " + it->name + "-user\n"
                                                                                                                                          "  name: " + it->name + "-context\n"
                                                                                                                                                                  "current-context: " + it->name + "-context\n"
                                                                                                                                                                                                   "kind: Config\n"
                                                                                                                                                                                                   "preferences: {}\n"
                                                                                                                                                                                                   "users:\n"
                                                                                                                                                                                                   "- name: " + it->name + "-user\n"
                                                                                                                                                                                                                           "  user:\n"
                                                                                                                                                                                                                           "    token: REDACTED-TOKEN-FOR-" + it->name + "\n";

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "KubeConfig retrieved successfully.";
                apiResponse.data = kubeconfig; // Kubeconfig is usually a YAML string
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        void APIRoutes::handleListK8sClusters(const httplib::Request& req, httplib::Response& res) {
            std::string filterName = req.get_param_value("filter-name");

            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json clusterList = nlohmann::json::array();
            for (const auto& cluster : mockK8sClusters) {
                if (filterName.empty() || cluster.name.find(filterName) != std::string::npos) {
                    clusterList.push_back(cluster.toJson());
                }
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "K8s clusters listed successfully.";
            apiResponse.data = clusterList;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleListK8sLocations(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json locations = {
                    {"name", "lille-4"},
                    {"name", "paris-1"},
                    {"name", "frankfurt-3"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "K8s cluster locations listed successfully.";
            apiResponse.data = locations;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleResumeK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                it->status = "Running"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + id + "' resumed successfully.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

        void APIRoutes::handleSuspendK8sCluster(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockK8sClusters.begin(), mockK8sClusters.end(),
                                   [&](const Models::K8sCluster& c) { return c.id == id; });

            if (it != mockK8sClusters.end()) {
                it->status = "Suspended"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "K8s cluster '" + id + "' suspended successfully.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "K8s cluster '" + id + "' not found.");
            }
        }

// --- Model Handlers ---
        void APIRoutes::handleUploadModel(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string filePath = json_body.value("filePath", "");
                std::string sku = json_body.value("sku", "");
                std::string registryName = json_body.value("registryName", "");
                std::string tag = json_body.value("tag", "");

                if (filePath.empty() || sku.empty() || registryName.empty() || tag.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: filePath, sku, registryName, or tag.");
                }

                // In a real scenario, you'd handle file upload logic here
                // For mock, just acknowledge the upload
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "Model '" + sku + "' version '" + tag + "' uploaded to registry '" + registryName + "' successfully.";
                apiResponse.data = {
                        {"filePath", filePath},
                        {"sku", sku},
                        {"registryName", registryName},
                        {"tag", tag}
                };
                sendJsonResponse(res, apiResponse);

            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

// --- SSH Key Handlers ---
        void APIRoutes::handleCreateSSHKey(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string name = json_body.value("name", "");
                std::string publicKey = json_body.value("publicKey", "");
                std::string description = json_body.value("description", "");

                if (name.empty() || publicKey.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: name or publicKey.");
                }

                std::lock_guard<std::mutex> lock(dataMutex);
                auto it = std::find_if(mockSshKeys.begin(), mockSshKeys.end(),
                                       [&](const Models::SSHKey& k) { return k.name == name; });
                if (it != mockSshKeys.end()) {
                    return sendErrorResponse(res, 409, "SSH key '" + name + "' already exists.");
                }

                Models::SSHKey newKey;
                newKey.id = Utils::generateUniqueId("sshkey");
                newKey.name = name;
                newKey.publicKey = publicKey;
                newKey.description = description;
                mockSshKeys.push_back(newKey);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "SSH key '" + name + "' created successfully.";
                apiResponse.data = newKey.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void APIRoutes::handleDeleteSSHKey(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto initial_size = mockSshKeys.size();
            mockSshKeys.erase(std::remove_if(mockSshKeys.begin(), mockSshKeys.end(),
                                             [&](const Models::SSHKey& k) { return k.id == id; }),
                              mockSshKeys.end());

            if (mockSshKeys.size() < initial_size) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "SSH key '" + id + "' deleted successfully.";
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "SSH key '" + id + "' not found.");
            }
        }

        void APIRoutes::handleListSSHKeys(const httplib::Request& req, httplib::Response& res) {
            std::string filterName = req.get_param_value("filter-name");

            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json keyList = nlohmann::json::array();
            for (const auto& key : mockSshKeys) {
                if (filterName.empty() || key.name.find(filterName) != std::string::npos) {
                    keyList.push_back(key.toJson());
                }
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "SSH keys listed successfully.";
            apiResponse.data = keyList;
            sendJsonResponse(res, apiResponse);
        }

// --- VM Handlers ---
        void APIRoutes::handleCreateVM(const httplib::Request& req, httplib::Response& res) {
            try {
                auto json_body = nlohmann::json::parse(req.body);
                std::string name = json_body.value("name", "");
                std::string sku = json_body.value("sku", "");
                std::string region = json_body.value("region", "");
                std::string osImage = json_body.value("osImage", "");
                std::string publicKeyId = json_body.value("publicKeyId", "");
                std::string initScript = json_body.value("initScript", "");

                if (name.empty() || sku.empty() || region.empty() || osImage.empty() || publicKeyId.empty()) {
                    return sendErrorResponse(res, 400, "Missing required parameters: name, sku, region, osImage, or publicKeyId.");
                }

                std::lock_guard<std::mutex> lock(dataMutex);
                auto it = std::find_if(mockVMs.begin(), mockVMs.end(),
                                       [&](const Models::VM& vm) { return vm.name == name; });
                if (it != mockVMs.end()) {
                    return sendErrorResponse(res, 409, "VM '" + name + "' already exists.");
                }

                // Check if publicKeyId exists in mockSshKeys
                auto sshKeyIt = std::find_if(mockSshKeys.begin(), mockSshKeys.end(),
                                             [&](const Models::SSHKey& k) { return k.id == publicKeyId; });
                if (sshKeyIt == mockSshKeys.end()) {
                    return sendErrorResponse(res, 400, "Invalid publicKeyId: '" + publicKeyId + "' not found.");
                }


                Models::VM newVM;
                newVM.id = Utils::generateUniqueId("vm");
                newVM.name = name;
                newVM.sku = sku;
                newVM.region = region;
                newVM.osImage = osImage;
                newVM.publicKeyId = publicKeyId;
                newVM.initScript = initScript;
                newVM.status = "Creating";
                mockVMs.push_back(newVM);

                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VM '" + name + "' created successfully.";
                apiResponse.data = newVM.toJson();
                sendJsonResponse(res, apiResponse);

            } catch (const nlohmann::json::parse_error& e) {
                sendErrorResponse(res, 400, "Invalid JSON body: " + std::string(e.what()));
            } catch (const std::exception& e) {
                sendErrorResponse(res, 500, "Server error: " + std::string(e.what()));
            }
        }

        void APIRoutes::handleDeleteVM(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto initial_size = mockVMs.size();
            mockVMs.erase(std::remove_if(mockVMs.begin(), mockVMs.end(),
                                         [&](const Models::VM& vm) { return vm.id == id; }),
                          mockVMs.end());

            if (mockVMs.size() < initial_size) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VM '" + id + "' deleted successfully.";
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "VM '" + id + "' not found.");
            }
        }

        void APIRoutes::handleGetVM(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockVMs.begin(), mockVMs.end(),
                                   [&](const Models::VM& vm) { return vm.id == id; });

            if (it != mockVMs.end()) {
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VM details retrieved.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "VM '" + id + "' not found.");
            }
        }

        void APIRoutes::handleListVMs(const httplib::Request& req, httplib::Response& res) {
            std::string filterName = req.get_param_value("filter-name");

            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json vmList = nlohmann::json::array();
            for (const auto& vm : mockVMs) {
                if (filterName.empty() || vm.name.find(filterName) != std::string::npos) {
                    vmList.push_back(vm.toJson());
                }
            }

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VMs listed successfully.";
            apiResponse.data = vmList;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleListVMLocations(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json locations = {
                    {"name", "us-east"},
                    {"name", "eu-west"},
                    {"name", "us-central"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM locations listed successfully.";
            apiResponse.data = locations;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleListVMOSImages(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json osImages = {
                    {"sku", "ubuntu-22.04"},
                    {"sku", "windows-server-2022"},
                    {"sku", "debian-11"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM OS images listed successfully.";
            apiResponse.data = osImages;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleListVMSKUs(const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(dataMutex);
            nlohmann::json skus = {
                    {"sku", "ogx-h100-80"},
                    {"sku", "standard-a2"},
                    {"sku", "premium-gpu-p100"}
            };

            Models::CloudResponse apiResponse;
            apiResponse.success = true;
            apiResponse.message = "VM SKUs listed successfully.";
            apiResponse.data = skus;
            sendJsonResponse(res, apiResponse);
        }

        void APIRoutes::handleRestartVM(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockVMs.begin(), mockVMs.end(),
                                   [&](const Models::VM& vm) { return vm.id == id; });

            if (it != mockVMs.end()) {
                it->status = "Restarting"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VM '" + id + "' is restarting.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "VM '" + id + "' not found.");
            }
        }

        void APIRoutes::handleResumeVM(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockVMs.begin(), mockVMs.end(),
                                   [&](const Models::VM& vm) { return vm.id == id; });

            if (it != mockVMs.end()) {
                it->status = "Running"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VM '" + id + "' resumed successfully.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "VM '" + id + "' not found.");
            }
        }

        void APIRoutes::handleSuspendVM(const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];

            std::lock_guard<std::mutex> lock(dataMutex);
            auto it = std::find_if(mockVMs.begin(), mockVMs.end(),
                                   [&](const Models::VM& vm) { return vm.id == id; });

            if (it != mockVMs.end()) {
                it->status = "Suspended"; // Change status
                Models::CloudResponse apiResponse;
                apiResponse.success = true;
                apiResponse.message = "VM '" + id + "' suspended successfully.";
                apiResponse.data = it->toJson();
                sendJsonResponse(res, apiResponse);
            } else {
                sendErrorResponse(res, 404, "VM '" + id + "' not found.");
            }
        }

    } // namespace Server
} // namespace NMC
