// APIRoutes_InternalHelpers.inl
// Internal helper layer shared by APIRoutes translation units.

        std::string trim(const std::string& value) {
            const auto start = value.find_first_not_of(" \t");
            if (start == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t");
            return value.substr(start, end - start + 1);
        }

        std::string trimLineEnd(std::string value) {
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
                value.pop_back();
            }
            return value;
        }

        std::string toLower(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        std::string normalizeOpenShiftStatus(const std::string& rawStatus) {
            const std::string status = toLower(rawStatus);
            if (status == "ready" || status == "running" || status == "active" || status == "available") {
                return "Ready";
            }
            if (status == "failed" || status == "error" || status == "unhealthy" || status == "deleted" || status == "terminated") {
                return "Failed";
            }
            if (status == "pending" || status == "accepted" || status == "queued" || status == "requested") {
                return "Pending";
            }
            if (status == "provisioning" || status == "gitops-syncing" || status == "gitops_syncing"
                || status == "syncing" || status == "installing" || status == "creating"
                || status == "build" || status == "rebuild" || status == "spawning") {
                return "Provisioning";
            }
            return "Unknown";
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

        nlohmann::json parseJsonBodyStrict(const std::string& body) {
            try {
                return nlohmann::json::parse(body);
            } catch (const nlohmann::json::parse_error& error) {
                throw std::runtime_error("Invalid JSON body: " + std::string(error.what()));
            }
        }

        std::string simulateConnectionHealthCheck(const std::string& endpoint) {
            if (endpoint.rfind("https://", 0) == 0 || endpoint.rfind("http://", 0) == 0) {
                return "healthy";
            }
            return "unhealthy";
        }

        nlohmann::json connectionSnapshotToJson(const NMC::Server::Models::Connection& connection) {
            return connection.toJsonString();
        }

        NMC::Server::Models::Connection connectionSnapshotFromJson(const nlohmann::json& payload) {
            NMC::Server::Models::Connection connection;
            if (!payload.is_object()) {
                return connection;
            }
            connection.name = payload.value("name", "");
            connection.endpoint = payload.value("endpoint", "");
            connection.isActive = payload.value("isActive", false);
            connection.healthStatus = payload.value("healthStatus", std::string("unknown"));
            return connection;
        }

        nlohmann::json vmSnapshotToJson(const NMC::Server::Models::VM& vm) {
            return {
                    {"id", vm.id},
                    {"name", vm.name},
                    {"sku", vm.sku},
                    {"region", vm.region},
                    {"osImage", vm.osImage},
                    {"publicKeyId", vm.publicKeyId},
                    {"initScript", vm.initScript},
                    {"status", vm.status}
            };
        }

        NMC::Server::Models::VM vmSnapshotFromJson(const nlohmann::json& payload) {
            NMC::Server::Models::VM vm;
            if (!payload.is_object()) {
                return vm;
            }
            vm.id = payload.value("id", "");
            vm.name = payload.value("name", "");
            vm.sku = payload.value("sku", "");
            vm.region = payload.value("region", "");
            vm.osImage = payload.value("osImage", "");
            vm.publicKeyId = payload.value("publicKeyId", "");
            vm.initScript = payload.value("initScript", "");
            vm.status = payload.value("status", "");
            return vm;
        }

        nlohmann::json sshKeySnapshotToJson(const NMC::Server::Models::SSHKey& key) {
            return {
                    {"id", key.id},
                    {"name", key.name},
                    {"publicKey", key.publicKey},
                    {"description", key.description}
            };
        }

        NMC::Server::Models::SSHKey sshKeySnapshotFromJson(const nlohmann::json& payload) {
            NMC::Server::Models::SSHKey key;
            if (!payload.is_object()) {
                return key;
            }
            key.id = payload.value("id", "");
            key.name = payload.value("name", "");
            key.publicKey = payload.value("publicKey", "");
            key.description = payload.value("description", "");
            return key;
        }

        nlohmann::json k8sClusterSnapshotToJson(const NMC::Server::Models::K8sCluster& cluster) {
            return {
                    {"id", cluster.id},
                    {"name", cluster.name},
                    {"region", cluster.region},
                    {"status", cluster.status},
                    {"is_vcluster", cluster.is_vcluster},
                    {"parent_cluster", cluster.parent_cluster},
                    {"vcluster_namespace", cluster.vcluster_namespace},
                    {"config_id", cluster.config_id}
            };
        }

        NMC::Server::Models::K8sCluster k8sClusterSnapshotFromJson(const nlohmann::json& payload) {
            return NMC::Server::Models::K8sCluster::fromJson(payload);
        }

        nlohmann::json vclusterConfigSnapshotToJson(const NMC::Server::Models::VClusterConfig& config) {
            nlohmann::json payload = config.toJson();
            payload["id"] = config.id;
            if (!config.security.ca_cert.empty()) {
                payload["security"]["ca_cert"] = config.security.ca_cert;
            }
            return payload;
        }

        void normalizeConnectionRegistry(std::vector<NMC::Server::Models::Connection>& connections,
                                         std::string& currentConnectionName) {
            currentConnectionName = trim(currentConnectionName);

            auto findByName = [&](const std::string& name) {
                return std::find_if(
                        connections.begin(),
                        connections.end(),
                        [&](const NMC::Server::Models::Connection& connection) {
                            return connection.name == name;
                        }
                );
            };

            auto selectedIt = currentConnectionName.empty() ? connections.end() : findByName(currentConnectionName);
            if (selectedIt == connections.end()) {
                selectedIt = std::find_if(
                        connections.begin(),
                        connections.end(),
                        [](const NMC::Server::Models::Connection& connection) { return connection.isActive; }
                );
                currentConnectionName = selectedIt != connections.end() ? selectedIt->name : "";
            }

            for (auto& connection : connections) {
                connection.isActive = !currentConnectionName.empty() && connection.name == currentConnectionName;
            }
        }

        constexpr double SAME_HARDWARE_MAX_LOAD_PER_CPU = 0.85;
        constexpr double SAME_HARDWARE_MAX_MEMORY_USED_RATIO = 0.85;
        constexpr int64_t SAME_HARDWARE_TRACEY_FRESHNESS_FLOOR_MS = 30'000;

        struct LocalHostIdentity {
            std::set<std::string> aliases;
            std::set<std::string> addresses;
        };

        struct HostMatchResult {
            bool sameHardware{false};
            nlohmann::json details;
        };

        struct SystemCapacitySnapshot {
            unsigned int cpuCores{0};
            double load1{0.0};
            double loadPerCpu{0.0};
            uint64_t totalMemoryBytes{0};
            uint64_t availableMemoryBytes{0};
            double memoryUsedRatio{0.0};
            bool loadAvailable{false};
            bool memoryAvailable{false};
        };

        void normalizeClusterStatus(nlohmann::json& cluster) {
            if (cluster.is_object() && cluster.contains("status") && cluster["status"].is_string()) {
                cluster["status"] = normalizeOpenShiftStatus(cluster["status"].get<std::string>());
            }
        }

        void addUniqueString(std::vector<std::string>& values, const std::string& value) {
            const std::string trimmed = trim(value);
            if (trimmed.empty()) {
                return;
            }
            if (std::find(values.begin(), values.end(), trimmed) == values.end()) {
                values.push_back(trimmed);
            }
        }

        void addUniqueInt(std::vector<int>& values, int value) {
            if (value <= 0) {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
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

        int firstPositiveIntValue(const nlohmann::json& objectNode, std::initializer_list<const char*> keys) {
            if (!objectNode.is_object()) {
                return -1;
            }
            for (const char* key : keys) {
                const auto it = objectNode.find(key);
                if (it == objectNode.end()) {
                    continue;
                }
                if (it->is_number_integer()) {
                    const int value = it->get<int>();
                    if (value > 0) {
                        return value;
                    }
                    continue;
                }
                if (it->is_number_unsigned()) {
                    const int value = static_cast<int>(it->get<unsigned int>());
                    if (value > 0) {
                        return value;
                    }
                }
            }
            return -1;
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

        double clampValue(double value, double minValue, double maxValue) {
            return std::max(minValue, std::min(maxValue, value));
        }

        double safeRatio(double numerator, double denominator, double fallback = 0.0) {
            if (denominator <= 0.0) {
                return fallback;
            }
            return numerator / denominator;
        }

        double traceyScenarioGrowthScale(double growthPctPerMin, double minutes) {
            return clampValue(1.0 + ((growthPctPerMin / 100.0) * minutes), 0.40, 8.0);
        }

        std::string traceyDominantProcess(const nlohmann::json& serverNode,
                                          const nlohmann::json* simulationNode) {
            if (simulationNode != nullptr) {
                const std::string existing = firstStringValue(
                        *simulationNode,
                        {"dominant_process", "dominantProcess"}
                );
                if (!existing.empty()) {
                    return existing;
                }
            }

            const auto* networkNode = firstObjectValue(serverNode, {"network"});
            const auto* topProcesses = networkNode != nullptr ? firstArrayValue(*networkNode, {"top_processes", "topProcesses"}) : nullptr;
            if (topProcesses == nullptr) {
                return "";
            }

            double bestBps = -1.0;
            std::string bestProcess;
            for (const auto& process : *topProcesses) {
                if (!process.is_object()) {
                    continue;
                }
                const std::string name = firstStringValue(process, {"name", "exe_path", "exePath"});
                const double totalBps = std::max(0.0, firstDoubleValue(process, {"total_bps", "totalBps"}, 0.0));
                if (!name.empty() && totalBps >= bestBps) {
                    bestBps = totalBps;
                    bestProcess = name;
                }
            }
            return bestProcess;
        }

        std::string traceyDominantRemoteIp(const nlohmann::json& serverNode,
                                           const nlohmann::json* simulationNode) {
            if (simulationNode != nullptr) {
                const std::string existing = firstStringValue(
                        *simulationNode,
                        {"dominant_remote_ip", "dominantRemoteIp"}
                );
                if (!existing.empty()) {
                    return existing;
                }
            }

            const auto* networkNode = firstObjectValue(serverNode, {"network"});
            const auto* topFlows = networkNode != nullptr ? firstArrayValue(*networkNode, {"top_flows", "topFlows"}) : nullptr;
            if (topFlows == nullptr) {
                return "";
            }

            double bestBps = -1.0;
            std::string bestRemote;
            for (const auto& flow : *topFlows) {
                if (!flow.is_object()) {
                    continue;
                }
                const double totalBps = std::max(
                        0.0,
                        firstDoubleValue(
                                flow,
                                {"total_bps", "totalBps"},
                                firstDoubleValue(flow, {"rx_bps", "rxBps"}, 0.0) +
                                firstDoubleValue(flow, {"tx_bps", "txBps"}, 0.0)
                        )
                );
                const std::string remoteIp = firstStringValue(flow, {"remote_ip", "remoteIp"});
                if (!remoteIp.empty() && totalBps >= bestBps) {
                    bestBps = totalBps;
                    bestRemote = remoteIp;
                }
            }
            return bestRemote;
        }

        double traceyObservedCpuCoreEstimate(const nlohmann::json& serverNode,
                                             const nlohmann::json* simulationNode,
                                             int gpuCount) {
            double estimate = simulationNode != nullptr
                              ? std::max(0.0, firstDoubleValue(*simulationNode, {"estimated_cpu_cores", "estimatedCpuCores"}, 0.0))
                              : 0.0;

            const auto* processes = firstArrayValue(serverNode, {"processes"});
            if (processes != nullptr) {
                double processCpuCores = 0.0;
                for (const auto& process : *processes) {
                    if (!process.is_object()) {
                        continue;
                    }
                    processCpuCores += std::max(0.0, firstDoubleValue(process, {"cpu_pct", "cpuPct"}, 0.0)) / 100.0;
                }
                estimate = std::max(estimate, processCpuCores);
            }

            const double cpuUsagePct = std::max(0.0, firstDoubleValue(serverNode, {"cpu_usage_pct", "cpuUsagePct"}, 0.0));
            estimate = std::max(estimate, cpuUsagePct / 12.5);
            estimate = std::max(estimate, static_cast<double>(std::max(1, gpuCount)) * 1.5);
            return std::max(1.0, estimate);
        }

        std::string normalizeTraceySimulationStrategy(std::string strategy) {
            strategy = toLower(trim(strategy));
            if (strategy == "throughput" || strategy == "collective") {
                return strategy;
            }
            return "balanced";
        }

        std::string inferTraceyExpansionTopology(int targetNodes,
                                                 int targetGpus,
                                                 double crossNetworkShare,
                                                 double latencyPressure,
                                                 double queuePressure) {
            if (targetNodes <= 1 && targetGpus <= 8) {
                return "Switch";
            }
            if (targetNodes >= 8 || crossNetworkShare >= 0.58 || latencyPressure >= 0.48) {
                return "Switch + DoubleBinaryTree";
            }
            if (targetGpus >= 8 || queuePressure >= 0.40 || crossNetworkShare >= 0.32) {
                return "Switch + Ring";
            }
            return "Switch";
        }

        std::string inferTraceyExpansionCollective(int targetNodes,
                                                   int targetGpus,
                                                   double crossNetworkShare,
                                                   double latencyPressure,
                                                   double queuePressure) {
            if (targetNodes >= 8 || crossNetworkShare >= 0.60 || latencyPressure >= 0.50) {
                return "DoubleBinaryTree";
            }
            if (targetGpus >= 8 || crossNetworkShare >= 0.35 || queuePressure >= 0.35) {
                return "Ring";
            }
            return "SwitchLocal";
        }

        struct TraceyExpansionEstimate {
            double networkBps{0.0};
            double crossNetworkBps{0.0};
            double activeFlows{0.0};
            double gpuUtilizationPct{0.0};
            double cpuUsagePct{0.0};
            double memoryBytes{0.0};
            double powerW{0.0};
            double latencyPressure{0.0};
            double queuePressure{0.0};
            double collectivePenalty{0.0};
            double crossNetworkShare{0.0};
            double confidence{0.0};
            std::string strategy{"balanced"};
            std::string topology{"Switch"};
            std::string recommendedCollective{"Ring"};
            std::string dominantProcess;
            std::string dominantRemoteIp;
            std::string simulationFocus{"Heuristic fabric"};
        };

        TraceyExpansionEstimate estimateTraceyExpansionScenario(const nlohmann::json& baseline,
                                                                const nlohmann::json& heuristics,
                                                                int targetNodes,
                                                                int targetGpus,
                                                                double targetCpuCores,
                                                                const std::string& strategy = "balanced") {
            TraceyExpansionEstimate estimate;
            const std::string normalizedStrategy = normalizeTraceySimulationStrategy(strategy);
            estimate.strategy = normalizedStrategy;

            const double strategyNetworkBias = normalizedStrategy == "throughput"
                                               ? 1.12
                                               : (normalizedStrategy == "collective" ? 0.96 : 1.0);
            const double strategyCrossBias = normalizedStrategy == "throughput"
                                             ? 1.08
                                             : (normalizedStrategy == "collective" ? 1.16 : 1.0);
            const double strategyGpuBias = normalizedStrategy == "throughput"
                                           ? 1.08
                                           : (normalizedStrategy == "collective" ? 1.02 : 1.0);
            const double strategyCpuBias = normalizedStrategy == "throughput"
                                           ? 1.05
                                           : (normalizedStrategy == "collective" ? 1.10 : 1.0);
            const double strategyCollectiveBias = normalizedStrategy == "throughput"
                                                  ? 1.05
                                                  : (normalizedStrategy == "collective" ? 1.18 : 1.0);

            const int baselineNodes = std::max(1, static_cast<int>(firstInt64Value(baseline, {"node_count", "nodeCount"}, 1)));
            const int baselineGpus = std::max(1, static_cast<int>(firstInt64Value(baseline, {"gpu_count", "gpuCount"}, 1)));
            const double baselineCpuCores = std::max(1.0, firstDoubleValue(baseline, {"cpu_core_estimate", "cpuCoreEstimate"}, 1.0));
            const double baselineNetworkBps = std::max(0.0, firstDoubleValue(baseline, {"attributed_total_bps", "attributedTotalBps"}, 0.0));
            const double baselineCrossNetworkBps = std::max(0.0, firstDoubleValue(baseline, {"cross_network_bps", "crossNetworkBps"}, 0.0));
            const double baselineActiveFlows = std::max(0.0, firstDoubleValue(baseline, {"active_flows", "activeFlows"}, 0.0));
            const double baselineGpuUtilPct = clampValue(firstDoubleValue(baseline, {"gpu_utilization_avg_pct", "gpuUtilizationAvgPct"}, 0.0), 0.0, 100.0);
            const double baselineCpuUsagePct = clampValue(firstDoubleValue(baseline, {"cpu_usage_pct", "cpuUsagePct"}, 0.0), 0.0, 100.0);
            const double baselineMemoryBytes = std::max(0.0, firstDoubleValue(baseline, {"memory_working_set_bytes", "memoryWorkingSetBytes"}, 0.0));
            const double baselinePowerW = std::max(0.0, firstDoubleValue(baseline, {"power_w", "powerW"}, 0.0));
            const double baselineLatencyPressure = clampValue(firstDoubleValue(baseline, {"latency_pressure", "latencyPressure"}, 0.0), 0.0, 1.0);
            const double baselineQueuePressure = clampValue(firstDoubleValue(baseline, {"queue_pressure", "queuePressure"}, 0.0), 0.0, 1.0);
            const double growthPctPerMin = firstDoubleValue(heuristics, {"traffic_growth_pct_per_min", "trafficGrowthPctPerMin"}, 0.0);

            const double nodeRatio = std::max(1.0, static_cast<double>(std::max(targetNodes, baselineNodes)) / static_cast<double>(baselineNodes));
            const double gpuRatio = std::max(1.0, static_cast<double>(std::max(targetGpus, baselineGpus)) / static_cast<double>(baselineGpus));
            const double cpuRatio = std::max(1.0, std::max(targetCpuCores, baselineCpuCores) / baselineCpuCores);

            const double nodeScaleExponent = clampValue(
                    firstDoubleValue(heuristics, {"node_scale_exponent", "nodeScaleExponent"}, 1.08) * strategyNetworkBias,
                    0.70,
                    2.00
            );
            const double gpuScaleExponent = clampValue(
                    firstDoubleValue(heuristics, {"gpu_scale_exponent", "gpuScaleExponent"}, 0.82) * strategyGpuBias,
                    0.60,
                    1.50
            );
            const double cpuScaleExponent = clampValue(
                    firstDoubleValue(heuristics, {"cpu_scale_exponent", "cpuScaleExponent"}, 0.74) * strategyCpuBias,
                    0.50,
                    1.40
            );
            const double collectiveScaleExponent = clampValue(
                    firstDoubleValue(heuristics, {"collective_scale_exponent", "collectiveScaleExponent"}, 0.18) * strategyCollectiveBias,
                    0.05,
                    1.40
            );
            const double crossNetworkBias = clampValue(
                    firstDoubleValue(heuristics, {"cross_network_bias", "crossNetworkBias"}, 0.70) * strategyCrossBias,
                    0.05,
                    2.50
            );
            const double latencyPenalty = clampValue(firstDoubleValue(heuristics, {"latency_penalty", "latencyPenalty"}, 0.10), 0.0, 1.50);
            const double queuePenalty = clampValue(firstDoubleValue(heuristics, {"queue_penalty", "queuePenalty"}, 0.10), 0.0, 1.50);
            const double baseCrossNetworkShare = clampValue(
                    firstDoubleValue(
                            baseline,
                            {"cross_network_share", "crossNetworkShare"},
                            safeRatio(baselineCrossNetworkBps, baselineNetworkBps, 0.0)
                    ),
                    0.0,
                    1.0
            );

            const double collectiveFactor = 1.0 + (collectiveScaleExponent * std::log2(std::max(1.0, nodeRatio)));
            const double weightedScale = (0.46 * std::pow(nodeRatio, nodeScaleExponent)) +
                                         (0.36 * std::pow(gpuRatio, gpuScaleExponent)) +
                                         (0.18 * std::pow(cpuRatio, cpuScaleExponent));
            const double growthScale = traceyScenarioGrowthScale(growthPctPerMin, 12.0);
            const double pressureScale = 1.0 +
                                         (latencyPenalty * std::max(0.0, nodeRatio - 1.0) * 0.08) +
                                         (queuePenalty * std::max(0.0, gpuRatio - 1.0) * 0.05);

            estimate.networkBps = baselineNetworkBps > 0.0
                                  ? baselineNetworkBps * weightedScale * collectiveFactor * growthScale * pressureScale * strategyNetworkBias
                                  : 0.0;
            estimate.crossNetworkShare = clampValue(
                    baseCrossNetworkShare +
                    (crossNetworkBias * std::max(0.0, nodeRatio - 1.0) * 0.09) +
                    (std::max(0.0, gpuRatio - 1.0) * 0.03),
                    0.02,
                    0.97
            );
            estimate.crossNetworkBps = estimate.networkBps * estimate.crossNetworkShare;
            estimate.activeFlows = baselineActiveFlows > 0.0
                                   ? baselineActiveFlows *
                                     ((0.40 * nodeRatio) + (0.35 * gpuRatio) + (0.25 * cpuRatio)) *
                                     (1.0 + (estimate.crossNetworkShare * 0.20))
                                   : 0.0;
            estimate.gpuUtilizationPct = clampValue(
                    baselineGpuUtilPct * (0.84 + (std::max(0.0, gpuRatio - 1.0) * 0.12) + ((collectiveFactor - 1.0) * 0.18)),
                    4.0,
                    99.0
            );
            estimate.cpuUsagePct = clampValue(
                    (baselineCpuUsagePct * (0.78 + (std::max(0.0, cpuRatio - 1.0) * 0.22))) +
                    (std::max(0.0, nodeRatio - 1.0) * 6.0) +
                    (std::max(0.0, gpuRatio - 1.0) * 3.0),
                    3.0,
                    99.0
            );
            estimate.memoryBytes = baselineMemoryBytes > 0.0
                                   ? baselineMemoryBytes * std::max(1.0, (0.55 * gpuRatio) + (0.45 * cpuRatio))
                                   : 0.0;
            estimate.powerW = baselinePowerW > 0.0
                              ? baselinePowerW *
                                std::max(1.0, (0.48 * gpuRatio) + (0.24 * nodeRatio) +
                                                 (0.28 * safeRatio(estimate.gpuUtilizationPct, std::max(1.0, baselineGpuUtilPct), 1.0)))
                              : 0.0;
            estimate.latencyPressure = clampValue(
                    baselineLatencyPressure +
                    (std::log2(std::max(1.0, nodeRatio)) * 0.12) +
                    (estimate.crossNetworkShare * 0.10) +
                    (std::max(0.0, gpuRatio - 1.0) * 0.02),
                    0.0,
                    1.0
            );
            estimate.queuePressure = clampValue(
                    baselineQueuePressure +
                    (std::log2(std::max(1.0, std::max(gpuRatio, cpuRatio))) * 0.10) +
                    (estimate.crossNetworkShare * 0.06),
                    0.0,
                    1.0
            );
            estimate.collectivePenalty = std::max(0.0, collectiveFactor - 1.0);
            estimate.topology = inferTraceyExpansionTopology(
                    std::max(targetNodes, baselineNodes),
                    std::max(targetGpus, baselineGpus),
                    estimate.crossNetworkShare,
                    estimate.latencyPressure,
                    estimate.queuePressure
            );
            estimate.recommendedCollective = normalizedStrategy == "collective"
                                            ? "DoubleBinaryTree"
                                            : inferTraceyExpansionCollective(
                                                    std::max(targetNodes, baselineNodes),
                                                    std::max(targetGpus, baselineGpus),
                                                    estimate.crossNetworkShare,
                                                    estimate.latencyPressure,
                                                    estimate.queuePressure
                                            );
            estimate.confidence = clampValue(
                    firstDoubleValue(
                            heuristics,
                            {"confidence", "attribution_confidence", "attributionConfidence"},
                            0.50
                    ) * (normalizedStrategy == "collective" ? 0.96 : 1.0),
                    0.10,
                    0.99
            );
            estimate.dominantProcess = firstStringValue(heuristics, {"dominant_process", "dominantProcess"});
            estimate.dominantRemoteIp = firstStringValue(heuristics, {"dominant_remote_ip", "dominantRemoteIp"});
            if (!estimate.dominantProcess.empty()) {
                estimate.simulationFocus = estimate.dominantProcess + " driving " + estimate.topology;
            } else if (!estimate.dominantRemoteIp.empty()) {
                estimate.simulationFocus = estimate.dominantRemoteIp + " on " + estimate.recommendedCollective;
            } else {
                estimate.simulationFocus = estimate.topology + " fabric";
            }
            return estimate;
        }

        void mergeJsonObjectDefaults(nlohmann::json& target, const nlohmann::json& defaults) {
            if (!defaults.is_object()) {
                return;
            }
            if (!target.is_object()) {
                target = nlohmann::json::object();
            }

            for (auto it = defaults.begin(); it != defaults.end(); ++it) {
                const std::string key = it.key();
                if (!target.contains(key) || target[key].is_null()) {
                    target[key] = it.value();
                    continue;
                }
                if (target[key].is_object() && it.value().is_object()) {
                    mergeJsonObjectDefaults(target[key], it.value());
                    continue;
                }
                if (target[key].is_string()) {
                    const std::string current = trim(target[key].get<std::string>());
                    if (current.empty()) {
                        target[key] = it.value();
                    }
                }
            }
        }

        void enrichTraceyResourceForecastView(const nlohmann::json& serverNode,
                                              const nlohmann::json* networkSummary,
                                              const nlohmann::json& gpus,
                                              nlohmann::json& resourceForecast,
                                              int64_t nowMs) {
            if (!resourceForecast.is_object()) {
                resourceForecast = nlohmann::json::object();
            }

            nlohmann::json simulationView = firstObjectValue(resourceForecast, {"simulation"}) != nullptr
                                           ? *firstObjectValue(resourceForecast, {"simulation"})
                                           : nlohmann::json::object();

            const double attributedTotalBps = networkSummary != nullptr
                                              ? std::max(0.0, firstDoubleValue(*networkSummary, {"attributed_total_bps", "attributedTotalBps"}, 0.0))
                                              : 0.0;
            const double crossNetworkBps = networkSummary != nullptr
                                           ? std::max(0.0, firstDoubleValue(*networkSummary, {"cross_network_bps", "crossNetworkBps"}, 0.0))
                                           : 0.0;
            const double activeFlows = networkSummary != nullptr
                                       ? std::max(0.0, firstDoubleValue(*networkSummary, {"active_flows", "activeFlows"}, 0.0))
                                       : 0.0;
            const double crossNetworkShare = attributedTotalBps > 0.0
                                             ? clampValue(crossNetworkBps / attributedTotalBps, 0.0, 1.0)
                                             : 0.0;
            const double attributionConfidence = networkSummary != nullptr
                                                 ? clampValue(firstDoubleValue(*networkSummary, {"attribution_confidence", "attributionConfidence"}, 0.0), 0.0, 1.0)
                                                 : 0.0;
            const double latencyPressure = networkSummary != nullptr
                                           ? clampValue(firstDoubleValue(*networkSummary, {"latency_pressure", "latencyPressure"}, 0.0), 0.0, 1.0)
                                           : 0.0;
            const double queuePressure = networkSummary != nullptr
                                         ? clampValue(firstDoubleValue(*networkSummary, {"queue_pressure", "queuePressure"}, 0.0), 0.0, 1.0)
                                         : 0.0;
            const double trafficGrowthPctPerMin = networkSummary != nullptr
                                                  ? firstDoubleValue(*networkSummary, {"traffic_growth_pct_per_min", "trafficGrowthPctPerMin"}, 0.0)
                                                  : 0.0;
            const double crossGrowthPctPerMin = networkSummary != nullptr
                                                ? firstDoubleValue(*networkSummary, {"cross_network_growth_pct_per_min", "crossNetworkGrowthPctPerMin"}, 0.0)
                                                : 0.0;
            const double flowGrowthPctPerMin = networkSummary != nullptr
                                               ? firstDoubleValue(*networkSummary, {"flow_growth_pct_per_min", "flowGrowthPctPerMin"}, 0.0)
                                               : 0.0;
            const int gpuCount = gpus.is_array()
                                 ? static_cast<int>(gpus.size())
                                 : std::max(1, static_cast<int>(firstInt64Value(serverNode, {"gpu_count", "gpuCount"}, 0)));
            const double gpuUtilizationAvgPct = clampValue(firstDoubleValue(serverNode, {"gpu_utilization_avg_pct", "gpuUtilizationAvgPct"}, 0.0), 0.0, 100.0);
            const double cpuUsagePct = clampValue(firstDoubleValue(serverNode, {"cpu_usage_pct", "cpuUsagePct"}, 0.0), 0.0, 100.0);
            const double powerW = std::max(
                    0.0,
                    firstDoubleValue(
                            simulationView,
                            {"estimated_power_w", "estimatedPowerW"},
                            firstDoubleValue(serverNode, {"gpu_power_total_w", "gpuPowerTotalW"}, 0.0)
                    )
            );
            const double memoryWorkingSetBytes = std::max(
                    0.0,
                    firstDoubleValue(
                            simulationView,
                            {"estimated_memory_working_set_bytes", "estimatedMemoryWorkingSetBytes"},
                            firstDoubleValue(serverNode, {"mem_working_set_bytes", "memWorkingSetBytes", "mem_used_bytes", "memUsedBytes"}, 0.0)
                    )
            );
            const double cpuCoreEstimate = traceyObservedCpuCoreEstimate(
                    serverNode,
                    simulationView.empty() ? nullptr : &simulationView,
                    gpuCount
            );
            const std::string dominantProcess = traceyDominantProcess(
                    serverNode,
                    simulationView.empty() ? nullptr : &simulationView
            );
            const std::string dominantRemoteIp = traceyDominantRemoteIp(
                    serverNode,
                    simulationView.empty() ? nullptr : &simulationView
            );
            const int sampleCount = std::max(0, static_cast<int>(firstInt64Value(resourceForecast, {"sample_count", "sampleCount"}, 0)));

            const double nodeScaleExponent = clampValue(
                    1.02 + (crossNetworkShare * 0.20) + (latencyPressure * 0.10) + (queuePressure * 0.06),
                    1.02,
                    1.42
            );
            const double gpuScaleExponent = clampValue(
                    0.80 + (crossNetworkShare * 0.08) + (gpuCount > 8 ? 0.06 : 0.0),
                    0.75,
                    1.12
            );
            const double cpuScaleExponent = clampValue(
                    0.68 + (queuePressure * 0.12) + (clampValue(cpuUsagePct / 100.0, 0.0, 1.0) * 0.10),
                    0.68,
                    1.00
            );
            const double collectiveScaleExponent = clampValue(
                    0.16 + (crossNetworkShare * 0.28) + std::min(0.18, std::log2(std::max(2, gpuCount)) / 10.0),
                    0.16,
                    0.90
            );
            const double crossNetworkBias = clampValue(
                    0.45 + (crossNetworkShare * 0.75) + (std::max(0.0, trafficGrowthPctPerMin) / 100.0 * 1.50),
                    0.45,
                    1.75
            );
            const double confidence = clampValue(
                    0.18 + (attributionConfidence * 0.52) + (sampleCount > 0 ? 0.10 : 0.0) +
                    (activeFlows > 0.0 ? 0.08 : 0.0) + (!dominantProcess.empty() ? 0.05 : 0.0),
                    0.10,
                    0.98
            );

            const int recommendedNodes = std::max(
                    2,
                    std::min(
                            16,
                            1 + static_cast<int>(std::ceil(1.0 + (crossNetworkShare * 3.0) +
                                                            (std::max(0.0, trafficGrowthPctPerMin) / 20.0)))
                    )
            );
            const int recommendedGpus = std::max(
                    std::max(1, gpuCount),
                    std::min(
                            64,
                            std::max(gpuCount * (crossNetworkShare >= 0.35 ? 2 : 3), gpuCount + 4)
                    )
            );
            const double recommendedCpuCores = std::ceil(
                    cpuCoreEstimate * (1.40 + (queuePressure * 0.50) + (crossNetworkShare * 0.30))
            );

            const nlohmann::json baseline = {
                    {"node_count", 1},
                    {"gpu_count", std::max(1, gpuCount)},
                    {"cpu_core_estimate", cpuCoreEstimate},
                    {"attributed_total_bps", attributedTotalBps},
                    {"cross_network_bps", crossNetworkBps},
                    {"cross_network_share", crossNetworkShare},
                    {"active_flows", activeFlows},
                    {"gpu_utilization_avg_pct", gpuUtilizationAvgPct},
                    {"cpu_usage_pct", cpuUsagePct},
                    {"memory_working_set_bytes", memoryWorkingSetBytes},
                    {"power_w", powerW},
                    {"latency_pressure", latencyPressure},
                    {"queue_pressure", queuePressure}
            };

            const nlohmann::json heuristics = {
                    {"inferred_topology", inferTraceyExpansionTopology(1, gpuCount, crossNetworkShare, latencyPressure, queuePressure)},
                    {"recommended_collective", inferTraceyExpansionCollective(recommendedNodes, recommendedGpus, crossNetworkShare, latencyPressure, queuePressure)},
                    {"node_scale_exponent", nodeScaleExponent},
                    {"gpu_scale_exponent", gpuScaleExponent},
                    {"cpu_scale_exponent", cpuScaleExponent},
                    {"collective_scale_exponent", collectiveScaleExponent},
                    {"cross_network_bias", crossNetworkBias},
                    {"latency_penalty", clampValue(0.12 + (latencyPressure * 0.88), 0.12, 1.0)},
                    {"queue_penalty", clampValue(0.10 + (queuePressure * 0.90), 0.10, 1.0)},
                    {"traffic_growth_pct_per_min", trafficGrowthPctPerMin},
                    {"cross_network_growth_pct_per_min", crossGrowthPctPerMin},
                    {"flow_growth_pct_per_min", flowGrowthPctPerMin},
                    {"attribution_confidence", attributionConfidence},
                    {"confidence", confidence},
                    {"dominant_process", dominantProcess},
                    {"dominant_remote_ip", dominantRemoteIp}
            };

            const TraceyExpansionEstimate recommendedSimulation = estimateTraceyExpansionScenario(
                    baseline,
                    heuristics,
                    recommendedNodes,
                    recommendedGpus,
                    recommendedCpuCores
            );

            const double projectedTotalBps5m = std::max(
                    0.0,
                    firstDoubleValue(
                            resourceForecast,
                            {"projected_total_bps_5m", "projectedTotalBps5m"},
                            attributedTotalBps * traceyScenarioGrowthScale(trafficGrowthPctPerMin, 5.0)
                    )
            );
            const double projectedTotalBps15m = std::max(
                    0.0,
                    firstDoubleValue(
                            resourceForecast,
                            {"projected_total_bps_15m", "projectedTotalBps15m"},
                            attributedTotalBps * traceyScenarioGrowthScale(trafficGrowthPctPerMin, 15.0)
                    )
            );
            const double projectedCrossBps5m = std::max(
                    0.0,
                    firstDoubleValue(
                            resourceForecast,
                            {"projected_cross_network_bps_5m", "projectedCrossNetworkBps5m"},
                            crossNetworkBps * traceyScenarioGrowthScale(crossGrowthPctPerMin, 5.0)
                    )
            );
            const double projectedCrossBps15m = std::max(
                    0.0,
                    firstDoubleValue(
                            resourceForecast,
                            {"projected_cross_network_bps_15m", "projectedCrossNetworkBps15m"},
                            crossNetworkBps * traceyScenarioGrowthScale(crossGrowthPctPerMin, 15.0)
                    )
            );
            const double projectedActiveFlows5m = std::max(
                    0.0,
                    firstDoubleValue(
                            resourceForecast,
                            {"projected_active_flows_5m", "projectedActiveFlows5m"},
                            activeFlows * traceyScenarioGrowthScale(flowGrowthPctPerMin, 5.0)
                    )
            );
            const double projectedActiveFlows15m = std::max(
                    0.0,
                    firstDoubleValue(
                            resourceForecast,
                            {"projected_active_flows_15m", "projectedActiveFlows15m"},
                            activeFlows * traceyScenarioGrowthScale(flowGrowthPctPerMin, 15.0)
                    )
            );

            mergeJsonObjectDefaults(
                    simulationView,
                    {
                            {"estimated_cpu_cores", recommendedCpuCores},
                            {"estimated_memory_working_set_bytes", recommendedSimulation.memoryBytes},
                            {"estimated_network_bps", recommendedSimulation.networkBps},
                            {"estimated_power_w", recommendedSimulation.powerW},
                            {"estimated_gpu_utilization_avg_pct", recommendedSimulation.gpuUtilizationPct},
                            {"estimated_cpu_usage_pct", recommendedSimulation.cpuUsagePct},
                            {"estimated_active_flows", recommendedSimulation.activeFlows},
                            {"cpu_scale_factor", safeRatio(recommendedCpuCores, cpuCoreEstimate, 1.0)},
                            {"memory_scale_factor", safeRatio(recommendedSimulation.memoryBytes, std::max(1.0, memoryWorkingSetBytes), safeRatio(recommendedCpuCores, cpuCoreEstimate, 1.0))},
                            {"power_scale_factor", safeRatio(recommendedSimulation.powerW, std::max(1.0, powerW), 1.0)},
                            {"latency_pressure", recommendedSimulation.latencyPressure},
                            {"queue_pressure", recommendedSimulation.queuePressure},
                            {"cross_network_flows", static_cast<int64_t>(std::llround(recommendedSimulation.activeFlows * recommendedSimulation.crossNetworkShare))},
                            {"collective_penalty", recommendedSimulation.collectivePenalty},
                            {"recommended_topology", heuristics.value("inferred_topology", std::string("Switch"))},
                            {"recommended_collective", heuristics.value("recommended_collective", std::string("Ring"))},
                            {"dominant_process", dominantProcess},
                            {"dominant_remote_ip", dominantRemoteIp}
                    }
            );

            mergeJsonObjectDefaults(
                    resourceForecast,
                    {
                            {"projected_total_bps_5m", projectedTotalBps5m},
                            {"projected_total_bps_15m", projectedTotalBps15m},
                            {"projected_cross_network_bps_5m", projectedCrossBps5m},
                            {"projected_cross_network_bps_15m", projectedCrossBps15m},
                            {"projected_active_flows_5m", projectedActiveFlows5m},
                            {"projected_active_flows_15m", projectedActiveFlows15m},
                            {"status", attributedTotalBps > 0.0 ? "heuristic" : "disabled"}
                    }
            );

            const std::string topologyLabel = heuristics.value("inferred_topology", std::string("Switch"));
            const std::string collectiveLabel = heuristics.value("recommended_collective", std::string("Ring"));
            nlohmann::json aiAdvice = firstObjectValue(resourceForecast, {"ai_advice", "aiAdvice"}) != nullptr
                                      ? *firstObjectValue(resourceForecast, {"ai_advice", "aiAdvice"})
                                      : nlohmann::json::object();
            mergeJsonObjectDefaults(
                    aiAdvice,
                    {
                            {"provider", "continuum"},
                            {"model", "heuristic-expansion-v1"},
                            {"generated_epoch_ms", nowMs},
                            {"summary", std::string("Tracey live network telemetry suggests a ") + topologyLabel +
                                        " expansion shape; keep collectives " + collectiveLabel +
                                        " beyond " + std::to_string(recommendedNodes) + " nodes."},
                            {"latency_focus", latencyPressure >= 0.35
                                              ? "Inter-node latency is already elevated; prefer tree-shaped reductions before adding more racks."
                                              : "Latency headroom is available; local switch expansion should remain efficient for the next growth step."},
                            {"cpu_focus", cpuUsagePct >= 70.0
                                          ? "CPU orchestration pressure is rising; scale host cores with each GPU cohort."
                                          : "CPU headroom is still workable, but core growth should track active-flow growth as nodes increase."},
                            {"memory_focus", memoryWorkingSetBytes > 0.0
                                             ? "Working-set size should grow with GPU and core expansion; keep memory provisioning near the simulated estimate."
                                             : "Memory telemetry is partial; treat the CPU-core target as the lower-bound for host sizing."},
                            {"power_focus", powerW > 0.0
                                            ? std::string("Power scales roughly with GPU expansion; plan for about ") + std::to_string(static_cast<int>(std::round(recommendedSimulation.powerW))) + " W at the recommended target."
                                            : "Power telemetry is limited; use GPU utilization and active-flow growth as the main expansion signals."},
                            {"simulation_focus", !dominantProcess.empty()
                                                 ? dominantProcess + " driving " + topologyLabel + " / " + collectiveLabel
                                                 : topologyLabel + " / " + collectiveLabel + " fabric path"},
                            {"confidence", confidence}
                    }
            );

            resourceForecast["simulation"] = simulationView;
            resourceForecast["ai_advice"] = aiAdvice;
            resourceForecast["continuum_expansion"] = {
                    {"version", 1},
                    {"source", "continuum.heuristic"},
                    {"generated_epoch_ms", nowMs},
                    {"baseline", baseline},
                    {"recommended_targets", {
                            {"node_count", recommendedNodes},
                            {"gpu_count", recommendedGpus},
                            {"cpu_core_estimate", recommendedCpuCores}
                    }},
                    {"maximum_targets", {
                            {"node_count", std::max(recommendedNodes, 24)},
                            {"gpu_count", std::max(recommendedGpus, 96)},
                            {"cpu_core_estimate", std::max(recommendedCpuCores, 96.0)}
                    }},
                    {"heuristics", heuristics},
                    {"dimensions", nlohmann::json::array({
                            {
                                    {"name", "intra_node"},
                                    {"topology", "Switch"},
                                    {"participant_count", std::max(1, std::min(gpuCount, 8))},
                                    {"bandwidth_bias", clampValue(1.0 - (crossNetworkShare * 0.35), 0.55, 1.15)},
                                    {"latency_bias", clampValue(1.0 + (latencyPressure * 0.35), 1.0, 1.45)}
                            },
                            {
                                    {"name", "inter_node"},
                                    {"topology", collectiveLabel == "DoubleBinaryTree" ? "DoubleBinaryTree" : "Ring"},
                                    {"participant_count", recommendedNodes},
                                    {"bandwidth_bias", clampValue(0.70 + (crossNetworkShare * 0.55), 0.55, 1.40)},
                                    {"latency_bias", clampValue(1.0 + (latencyPressure * 0.80) + (queuePressure * 0.20), 1.0, 1.95)}
                            },
                            {
                                    {"name", "host_orchestration"},
                                    {"topology", "FullyConnected"},
                                    {"participant_count", static_cast<int>(std::ceil(recommendedCpuCores))},
                                    {"bandwidth_bias", clampValue(0.35 + (queuePressure * 0.45), 0.30, 1.0)},
                                    {"latency_bias", clampValue(1.0 + (queuePressure * 0.75), 1.0, 1.75)}
                            }
                    })},
                    {"recommended_simulation", {
                            {"estimated_network_bps", recommendedSimulation.networkBps},
                            {"estimated_cross_network_bps", recommendedSimulation.crossNetworkBps},
                            {"estimated_active_flows", recommendedSimulation.activeFlows},
                            {"estimated_gpu_utilization_avg_pct", recommendedSimulation.gpuUtilizationPct},
                            {"estimated_cpu_usage_pct", recommendedSimulation.cpuUsagePct},
                            {"estimated_memory_working_set_bytes", recommendedSimulation.memoryBytes},
                            {"estimated_power_w", recommendedSimulation.powerW},
                            {"estimated_latency_pressure", recommendedSimulation.latencyPressure},
                            {"estimated_queue_pressure", recommendedSimulation.queuePressure},
                            {"collective_penalty", recommendedSimulation.collectivePenalty},
                            {"cross_network_share", recommendedSimulation.crossNetworkShare},
                            {"strategy", recommendedSimulation.strategy},
                            {"topology", recommendedSimulation.topology},
                            {"recommended_collective", recommendedSimulation.recommendedCollective},
                            {"confidence", recommendedSimulation.confidence},
                            {"dominant_process", recommendedSimulation.dominantProcess},
                            {"dominant_remote_ip", recommendedSimulation.dominantRemoteIp},
                            {"simulation_focus", recommendedSimulation.simulationFocus},
                            {"target_nodes", recommendedNodes},
                            {"target_gpus", recommendedGpus},
                            {"target_cpu_cores", recommendedCpuCores}
                    }}
            };
        }

        struct TraceySimulationQuery {
            bool hasNodes{false};
            bool hasGpus{false};
            bool hasCpuCores{false};
            bool hasStrategy{false};
            int targetNodes{0};
            int targetGpus{0};
            double targetCpuCores{0.0};
            std::string strategy{"balanced"};

            [[nodiscard]] bool requested() const {
                return hasNodes || hasGpus || hasCpuCores || hasStrategy;
            }
        };

        struct TraceyResolvedSimulationRequest {
            std::string scope{"server"};
            std::string strategy{"balanced"};
            int baselineNodes{1};
            int baselineGpus{1};
            double baselineCpuCores{1.0};
            int recommendedNodes{1};
            int recommendedGpus{1};
            double recommendedCpuCores{1.0};
            int heuristicMaxNodes{1};
            int heuristicMaxGpus{1};
            double heuristicMaxCpuCores{1.0};
            int targetNodes{1};
            int targetGpus{1};
            double targetCpuCores{1.0};
            bool withinHeuristicLimits{true};
            bool clamped{false};
        };

        int64_t parseQueryInt64(const httplib::Request& req,
                                const char* key,
                                int64_t fallback,
                                int64_t minValue,
                                int64_t maxValue);

        TraceySimulationQuery parseTraceySimulationQuery(const httplib::Request& req) {
            TraceySimulationQuery query;
            if (req.has_param("simulation_nodes")) {
                query.hasNodes = true;
                query.targetNodes = static_cast<int>(parseQueryInt64(req, "simulation_nodes", 1, 1, 4096));
            }
            if (req.has_param("simulation_gpus")) {
                query.hasGpus = true;
                query.targetGpus = static_cast<int>(parseQueryInt64(req, "simulation_gpus", 1, 1, 65536));
            }
            if (req.has_param("simulation_cores")) {
                query.hasCpuCores = true;
                query.targetCpuCores = static_cast<double>(parseQueryInt64(req, "simulation_cores", 1, 1, 262144));
            }
            if (req.has_param("simulation_strategy")) {
                query.hasStrategy = true;
                query.strategy = normalizeTraceySimulationStrategy(req.get_param_value("simulation_strategy"));
            }
            return query;
        }

        TraceyResolvedSimulationRequest resolveTraceySimulationRequest(const nlohmann::json& expansionModel,
                                                                       const TraceySimulationQuery& query,
                                                                       const std::string& scope) {
            const nlohmann::json baseline = firstObjectValue(expansionModel, {"baseline"}) != nullptr
                                           ? *firstObjectValue(expansionModel, {"baseline"})
                                           : nlohmann::json::object();
            const nlohmann::json recommended = firstObjectValue(expansionModel, {"recommended_targets", "recommendedTargets"}) != nullptr
                                              ? *firstObjectValue(expansionModel, {"recommended_targets", "recommendedTargets"})
                                              : nlohmann::json::object();
            const nlohmann::json maximum = firstObjectValue(expansionModel, {"maximum_targets", "maximumTargets"}) != nullptr
                                          ? *firstObjectValue(expansionModel, {"maximum_targets", "maximumTargets"})
                                          : nlohmann::json::object();

            TraceyResolvedSimulationRequest resolved;
            resolved.scope = scope;
            resolved.strategy = query.hasStrategy ? normalizeTraceySimulationStrategy(query.strategy) : "balanced";
            resolved.baselineNodes = std::max(1, static_cast<int>(firstInt64Value(baseline, {"node_count", "nodeCount"}, 1)));
            resolved.baselineGpus = std::max(1, static_cast<int>(firstInt64Value(baseline, {"gpu_count", "gpuCount"}, 1)));
            resolved.baselineCpuCores = std::max(1.0, firstDoubleValue(baseline, {"cpu_core_estimate", "cpuCoreEstimate"}, 1.0));
            resolved.recommendedNodes = std::max(
                    resolved.baselineNodes,
                    static_cast<int>(firstInt64Value(recommended, {"node_count", "nodeCount"}, resolved.baselineNodes))
            );
            resolved.recommendedGpus = std::max(
                    resolved.baselineGpus,
                    static_cast<int>(firstInt64Value(recommended, {"gpu_count", "gpuCount"}, resolved.baselineGpus))
            );
            resolved.recommendedCpuCores = std::max(
                    resolved.baselineCpuCores,
                    firstDoubleValue(recommended, {"cpu_core_estimate", "cpuCoreEstimate"}, resolved.baselineCpuCores)
            );
            resolved.heuristicMaxNodes = std::max(
                    resolved.recommendedNodes,
                    static_cast<int>(firstInt64Value(maximum, {"node_count", "nodeCount"}, resolved.recommendedNodes))
            );
            resolved.heuristicMaxGpus = std::max(
                    resolved.recommendedGpus,
                    static_cast<int>(firstInt64Value(maximum, {"gpu_count", "gpuCount"}, resolved.recommendedGpus))
            );
            resolved.heuristicMaxCpuCores = std::max(
                    resolved.recommendedCpuCores,
                    firstDoubleValue(maximum, {"cpu_core_estimate", "cpuCoreEstimate"}, resolved.recommendedCpuCores)
            );

            const int desiredNodes = query.hasNodes ? query.targetNodes : resolved.recommendedNodes;
            const int desiredGpus = query.hasGpus ? query.targetGpus : resolved.recommendedGpus;
            const double desiredCpuCores = query.hasCpuCores ? query.targetCpuCores : resolved.recommendedCpuCores;

            resolved.targetNodes = std::clamp(desiredNodes, resolved.baselineNodes, std::max(resolved.heuristicMaxNodes, 4096));
            resolved.targetGpus = std::clamp(desiredGpus, resolved.baselineGpus, std::max(resolved.heuristicMaxGpus, 65536));
            resolved.targetCpuCores = std::clamp(desiredCpuCores, resolved.baselineCpuCores, std::max(resolved.heuristicMaxCpuCores, 262144.0));
            resolved.clamped = resolved.targetNodes != desiredNodes ||
                               resolved.targetGpus != desiredGpus ||
                               std::abs(resolved.targetCpuCores - desiredCpuCores) > 0.01;
            resolved.withinHeuristicLimits = resolved.targetNodes <= resolved.heuristicMaxNodes &&
                                             resolved.targetGpus <= resolved.heuristicMaxGpus &&
                                             resolved.targetCpuCores <= resolved.heuristicMaxCpuCores;
            return resolved;
        }

        nlohmann::json buildTraceySimulationRequestView(const TraceyResolvedSimulationRequest& request) {
            return {
                    {"scope", request.scope},
                    {"strategy", request.strategy},
                    {"baseline_nodes", request.baselineNodes},
                    {"baseline_gpus", request.baselineGpus},
                    {"baseline_cpu_cores", request.baselineCpuCores},
                    {"recommended_nodes", request.recommendedNodes},
                    {"recommended_gpus", request.recommendedGpus},
                    {"recommended_cpu_cores", request.recommendedCpuCores},
                    {"heuristic_max_nodes", request.heuristicMaxNodes},
                    {"heuristic_max_gpus", request.heuristicMaxGpus},
                    {"heuristic_max_cpu_cores", request.heuristicMaxCpuCores},
                    {"target_nodes", request.targetNodes},
                    {"target_gpus", request.targetGpus},
                    {"target_cpu_cores", request.targetCpuCores},
                    {"within_heuristic_limits", request.withinHeuristicLimits},
                    {"clamped", request.clamped}
            };
        }

        nlohmann::json buildTraceySimulationForecastView(const nlohmann::json& expansionModel,
                                                         const TraceyResolvedSimulationRequest& request) {
            const nlohmann::json baseline = firstObjectValue(expansionModel, {"baseline"}) != nullptr
                                           ? *firstObjectValue(expansionModel, {"baseline"})
                                           : nlohmann::json::object();
            const nlohmann::json heuristics = firstObjectValue(expansionModel, {"heuristics"}) != nullptr
                                             ? *firstObjectValue(expansionModel, {"heuristics"})
                                             : nlohmann::json::object();
            const TraceyExpansionEstimate estimate = estimateTraceyExpansionScenario(
                    baseline,
                    heuristics,
                    request.targetNodes,
                    request.targetGpus,
                    request.targetCpuCores,
                    request.strategy
            );
            return {
                    {"scope", request.scope},
                    {"strategy", request.strategy},
                    {"estimated_network_bps", estimate.networkBps},
                    {"estimated_cross_network_bps", estimate.crossNetworkBps},
                    {"estimated_active_flows", estimate.activeFlows},
                    {"estimated_gpu_utilization_avg_pct", estimate.gpuUtilizationPct},
                    {"estimated_cpu_usage_pct", estimate.cpuUsagePct},
                    {"estimated_memory_working_set_bytes", estimate.memoryBytes},
                    {"estimated_power_w", estimate.powerW},
                    {"estimated_latency_pressure", estimate.latencyPressure},
                    {"estimated_queue_pressure", estimate.queuePressure},
                    {"collective_penalty", estimate.collectivePenalty},
                    {"cross_network_share", estimate.crossNetworkShare},
                    {"topology", estimate.topology},
                    {"recommended_collective", estimate.recommendedCollective},
                    {"confidence", estimate.confidence},
                    {"dominant_process", estimate.dominantProcess},
                    {"dominant_remote_ip", estimate.dominantRemoteIp},
                    {"simulation_focus", estimate.simulationFocus},
                    {"target_nodes", request.targetNodes},
                    {"target_gpus", request.targetGpus},
                    {"target_cpu_cores", request.targetCpuCores},
                    {"extra_nodes", std::max(0, request.targetNodes - request.baselineNodes)},
                    {"extra_gpus", std::max(0, request.targetGpus - request.baselineGpus)},
                    {"extra_cpu_cores", std::max(0.0, request.targetCpuCores - request.baselineCpuCores)},
                    {"within_heuristic_limits", request.withinHeuristicLimits},
                    {"clamped", request.clamped}
            };
        }

        bool buildTraceyRequestedSimulation(const nlohmann::json& resourceForecast,
                                            const TraceySimulationQuery& query,
                                            const std::string& scope,
                                            nlohmann::json& requestOut,
                                            nlohmann::json& forecastOut) {
            requestOut = nlohmann::json::object();
            forecastOut = nlohmann::json::object();
            if (!query.requested() || !resourceForecast.is_object()) {
                return false;
            }
            const auto* expansionModel = firstObjectValue(resourceForecast, {"continuum_expansion", "continuumExpansion"});
            if (expansionModel == nullptr || !expansionModel->is_object()) {
                return false;
            }
            const TraceyResolvedSimulationRequest resolved = resolveTraceySimulationRequest(*expansionModel, query, scope);
            requestOut = buildTraceySimulationRequestView(resolved);
            forecastOut = buildTraceySimulationForecastView(*expansionModel, resolved);
            return true;
        }

        nlohmann::json buildTraceyGpuSimulationForecast(const nlohmann::json& gpuDetail,
                                                        const nlohmann::json& resourceForecast,
                                                        const nlohmann::json& simulationForecast) {
            if (!gpuDetail.is_object() || !resourceForecast.is_object() || !simulationForecast.is_object()) {
                return nlohmann::json::object();
            }
            const auto* expansionModel = firstObjectValue(resourceForecast, {"continuum_expansion", "continuumExpansion"});
            if (expansionModel == nullptr || !expansionModel->is_object()) {
                return nlohmann::json::object();
            }

            const nlohmann::json baseline = firstObjectValue(*expansionModel, {"baseline"}) != nullptr
                                           ? *firstObjectValue(*expansionModel, {"baseline"})
                                           : nlohmann::json::object();
            const double baselineNodes = std::max(1.0, firstDoubleValue(baseline, {"node_count", "nodeCount"}, 1.0));
            const double baselineGpus = std::max(1.0, firstDoubleValue(baseline, {"gpu_count", "gpuCount"}, 1.0));
            const double baselineCpuCores = std::max(1.0, firstDoubleValue(baseline, {"cpu_core_estimate", "cpuCoreEstimate"}, 1.0));

            const double targetNodes = std::max(baselineNodes, firstDoubleValue(simulationForecast, {"target_nodes", "targetNodes"}, baselineNodes));
            const double targetGpus = std::max(baselineGpus, firstDoubleValue(simulationForecast, {"target_gpus", "targetGpus"}, baselineGpus));
            const double targetCpuCores = std::max(baselineCpuCores, firstDoubleValue(simulationForecast, {"target_cpu_cores", "targetCpuCores"}, baselineCpuCores));
            const double nodeRatio = std::max(1.0, targetNodes / baselineNodes);
            const double gpuRatio = std::max(1.0, targetGpus / baselineGpus);
            const double cpuRatio = std::max(1.0, targetCpuCores / baselineCpuCores);

            const double baseUtilPct = clampValue(firstDoubleValue(gpuDetail, {"util_pct", "utilPct"}, 0.0), 0.0, 100.0);
            const double basePowerW = std::max(0.0, firstDoubleValue(gpuDetail, {"power_w", "powerW"}, 0.0));
            const double baseMemUsedBytes = std::max(0.0, firstDoubleValue(gpuDetail, {"mem_used_bytes", "memUsedBytes"}, 0.0));
            const double baseTempC = std::max(0.0, firstDoubleValue(gpuDetail, {"temp_c", "tempC"}, 0.0));
            const double baseReliability = clampValue(firstDoubleValue(gpuDetail, {"reliability_score", "reliabilityScore"}, 1.0), 0.0, 1.0);
            const double baseRisk = clampValue(firstDoubleValue(gpuDetail, {"last_guard_risk", "lastGuardRisk"}, 0.0), 0.0, 1.0);
            const double baseConfidence = clampValue(firstDoubleValue(gpuDetail, {"last_guard_confidence", "lastGuardConfidence"}, 0.0), 0.0, 1.0);
            const double latencyPressure = clampValue(firstDoubleValue(simulationForecast, {"estimated_latency_pressure", "estimatedLatencyPressure"}, 0.0), 0.0, 1.0);
            const double queuePressure = clampValue(firstDoubleValue(simulationForecast, {"estimated_queue_pressure", "estimatedQueuePressure"}, 0.0), 0.0, 1.0);
            const double collectivePenalty = std::max(0.0, firstDoubleValue(simulationForecast, {"collective_penalty", "collectivePenalty"}, 0.0));
            const int64_t baseProbeFails = std::max<int64_t>(0, firstInt64Value(gpuDetail, {"probe_fail_count", "probeFailCount"}, 0));
            const int64_t baseProbeErrors = std::max<int64_t>(0, firstInt64Value(gpuDetail, {"probe_error_count", "probeErrorCount"}, 0));
            const int64_t smCount = std::max<int64_t>(0, firstInt64Value(gpuDetail, {"sm_count", "smCount"}, 0));

            const double estimatedUtilPct = clampValue(
                    baseUtilPct * (0.88 + (std::max(0.0, gpuRatio - 1.0) * 0.10) + (collectivePenalty * 0.16)),
                    1.0,
                    99.0
            );
            const double estimatedPowerW = basePowerW > 0.0
                                           ? basePowerW * std::max(
                                                   1.0,
                                                   (0.54 * gpuRatio) +
                                                   (0.18 * nodeRatio) +
                                                   (0.28 * safeRatio(estimatedUtilPct, std::max(1.0, baseUtilPct), 1.0))
                                           )
                                           : 0.0;
            const double estimatedMemUsedBytes = baseMemUsedBytes > 0.0
                                                 ? baseMemUsedBytes * std::max(1.0, (0.62 * gpuRatio) + (0.38 * cpuRatio))
                                                 : 0.0;
            const double estimatedTempC = clampValue(
                    (baseTempC > 0.0 ? baseTempC : 32.0) +
                    (std::log2(std::max(1.0, gpuRatio)) * 3.5) +
                    (std::max(0.0, estimatedUtilPct - baseUtilPct) * 0.10),
                    baseTempC > 0.0 ? baseTempC : 32.0,
                    99.0
            );
            const double estimatedReliability = clampValue(
                    baseReliability -
                    (((latencyPressure + queuePressure) * 0.5) * 0.05) -
                    (std::max(0.0, gpuRatio - 1.0) * 0.01),
                    0.45,
                    1.0
            );
            const double estimatedRisk = clampValue(
                    baseRisk +
                    (latencyPressure * 0.18) +
                    (queuePressure * 0.18) +
                    (std::max(0.0, gpuRatio - 1.0) * 0.04),
                    0.0,
                    0.99
            );
            const double estimatedConfidence = clampValue(
                    std::max(baseConfidence, firstDoubleValue(simulationForecast, {"confidence"}, 0.0)) *
                    (1.0 - (std::max(0.0, estimatedRisk - baseRisk) * 0.10)),
                    0.05,
                    0.99
            );

            int64_t estimatedProbeFails = static_cast<int64_t>(std::llround(
                    static_cast<double>(baseProbeFails) *
                    std::max(1.0, (gpuRatio * 0.75) + std::max(0.0, (estimatedRisk - 0.35) * 4.0))
            ));
            int64_t estimatedProbeErrors = static_cast<int64_t>(std::llround(
                    static_cast<double>(baseProbeErrors) *
                    std::max(1.0, (cpuRatio * 0.40) + (queuePressure * 2.5))
            ));
            if (estimatedProbeFails == 0 && estimatedRisk >= 0.70) {
                estimatedProbeFails = 1;
            }
            if (estimatedProbeErrors == 0 && queuePressure >= 0.70) {
                estimatedProbeErrors = 1;
            }

            return {
                    {"gpu_id", firstStringValue(gpuDetail, {"gpu_id", "gpuId"})},
                    {"strategy", firstStringValue(simulationForecast, {"strategy"}, "balanced")},
                    {"estimated_util_pct", estimatedUtilPct},
                    {"estimated_power_w", estimatedPowerW},
                    {"estimated_mem_used_bytes", estimatedMemUsedBytes},
                    {"estimated_temp_c", estimatedTempC},
                    {"estimated_reliability_score", estimatedReliability},
                    {"estimated_guard_risk", estimatedRisk},
                    {"estimated_guard_confidence", estimatedConfidence},
                    {"estimated_probe_fail_count", std::max<int64_t>(estimatedProbeFails, baseProbeFails)},
                    {"estimated_probe_error_count", std::max<int64_t>(estimatedProbeErrors, baseProbeErrors)},
                    {"estimated_sm_active", smCount > 0 ? std::min<int64_t>(smCount, static_cast<int64_t>(std::llround((estimatedUtilPct / 100.0) * static_cast<double>(smCount)))) : 0},
                    {"estimated_sm_pressure_pct", smCount > 0 ? clampValue(estimatedUtilPct + (queuePressure * 12.0), 0.0, 100.0) : 0.0},
                    {"topology", firstStringValue(simulationForecast, {"topology"})},
                    {"recommended_collective", firstStringValue(simulationForecast, {"recommended_collective", "recommendedCollective"})},
                    {"simulation_focus", firstStringValue(simulationForecast, {"simulation_focus", "simulationFocus"})},
                    {"confidence", firstDoubleValue(simulationForecast, {"confidence"}, 0.0)},
                    {"target_nodes", targetNodes},
                    {"target_gpus", targetGpus},
                    {"target_cpu_cores", targetCpuCores}
            };
        }

        nlohmann::json buildTraceyAggregateResourceForecastView(const std::vector<nlohmann::json>& items,
                                                                int64_t nowMs) {
            if (items.empty()) {
                return nlohmann::json::object();
            }

            double cpuUsageSum = 0.0;
            int cpuUsageCount = 0;
            int gpuCountTotal = 0;
            double gpuUtilWeighted = 0.0;
            double powerWTotal = 0.0;
            double memoryWorkingSetBytes = 0.0;
            double attributedTotalBps = 0.0;
            double crossNetworkBps = 0.0;
            double activeFlows = 0.0;
            double attributionConfidenceWeighted = 0.0;
            double latencyPressureWeighted = 0.0;
            double queuePressureWeighted = 0.0;
            double trafficGrowthWeighted = 0.0;
            double crossGrowthWeighted = 0.0;
            double flowGrowthWeighted = 0.0;
            double weightSum = 0.0;
            int sampleCount = 0;
            double dominantWeight = -1.0;
            std::string dominantProcess;
            std::string dominantRemoteIp;

            for (const auto& item : items) {
                const nlohmann::json summary = firstObjectValue(item, {"summary", "server_summary"}) != nullptr
                                               ? *firstObjectValue(item, {"summary", "server_summary"})
                                               : nlohmann::json::object();
                const nlohmann::json resourceForecast = firstObjectValue(item, {"resource_forecast", "resourceForecast"}) != nullptr
                                                       ? *firstObjectValue(item, {"resource_forecast", "resourceForecast"})
                                                       : nlohmann::json::object();
                const nlohmann::json simulation = firstObjectValue(resourceForecast, {"simulation"}) != nullptr
                                                  ? *firstObjectValue(resourceForecast, {"simulation"})
                                                  : nlohmann::json::object();
                const nlohmann::json expansion = firstObjectValue(resourceForecast, {"continuum_expansion", "continuumExpansion"}) != nullptr
                                                 ? *firstObjectValue(resourceForecast, {"continuum_expansion", "continuumExpansion"})
                                                 : nlohmann::json::object();
                const nlohmann::json heuristics = firstObjectValue(expansion, {"heuristics"}) != nullptr
                                                  ? *firstObjectValue(expansion, {"heuristics"})
                                                  : nlohmann::json::object();

                const int itemGpuCount = std::max(
                        1,
                        static_cast<int>(firstInt64Value(
                                summary,
                                {"gpu_count", "gpuCount"},
                                std::max<int64_t>(1, item.value("gpu_count", 1))
                        ))
                );
                gpuCountTotal += itemGpuCount;
                const double itemCpuUsage = clampValue(firstDoubleValue(summary, {"cpu_usage_pct", "cpuUsagePct"}, 0.0), 0.0, 100.0);
                cpuUsageSum += itemCpuUsage;
                cpuUsageCount++;
                gpuUtilWeighted += clampValue(firstDoubleValue(summary, {"gpu_utilization_avg_pct", "gpuUtilizationAvgPct"}, 0.0), 0.0, 100.0) * itemGpuCount;
                powerWTotal += std::max(
                        0.0,
                        firstDoubleValue(
                                simulation,
                                {"estimated_power_w", "estimatedPowerW"},
                                firstDoubleValue(summary, {"gpu_power_total_w", "gpuPowerTotalW"}, 0.0)
                        )
                );
                memoryWorkingSetBytes += std::max(
                        0.0,
                        firstDoubleValue(
                                simulation,
                                {"estimated_memory_working_set_bytes", "estimatedMemoryWorkingSetBytes"},
                                firstDoubleValue(summary, {"estimated_memory_working_set_bytes", "estimatedMemoryWorkingSetBytes"}, 0.0)
                        )
                );

                const double itemAttributedTotalBps = std::max(0.0, firstDoubleValue(summary, {"attributed_total_bps", "attributedTotalBps"}, 0.0));
                const double itemCrossNetworkBps = std::max(0.0, firstDoubleValue(summary, {"cross_network_bps", "crossNetworkBps"}, 0.0));
                const double itemActiveFlows = std::max(0.0, firstDoubleValue(summary, {"network_active_flows", "activeFlows"}, 0.0));
                const double networkWeight = std::max(1.0, itemAttributedTotalBps);
                attributedTotalBps += itemAttributedTotalBps;
                crossNetworkBps += itemCrossNetworkBps;
                activeFlows += itemActiveFlows;
                attributionConfidenceWeighted += clampValue(
                        firstDoubleValue(summary, {"network_attribution_confidence", "attribution_confidence", "attributionConfidence"}, 0.0),
                        0.0,
                        1.0
                ) * networkWeight;
                latencyPressureWeighted += clampValue(
                        firstDoubleValue(summary, {"network_latency_pressure", "latency_pressure", "latencyPressure"}, 0.0),
                        0.0,
                        1.0
                ) * networkWeight;
                queuePressureWeighted += clampValue(
                        firstDoubleValue(summary, {"network_queue_pressure", "queue_pressure", "queuePressure"}, 0.0),
                        0.0,
                        1.0
                ) * networkWeight;
                trafficGrowthWeighted += firstDoubleValue(
                        summary,
                        {"network_traffic_growth_pct_per_min", "traffic_growth_pct_per_min", "trafficGrowthPctPerMin"},
                        firstDoubleValue(heuristics, {"traffic_growth_pct_per_min", "trafficGrowthPctPerMin"}, 0.0)
                ) * networkWeight;
                crossGrowthWeighted += firstDoubleValue(
                        summary,
                        {"network_cross_network_growth_pct_per_min", "cross_network_growth_pct_per_min", "crossNetworkGrowthPctPerMin"},
                        firstDoubleValue(heuristics, {"cross_network_growth_pct_per_min", "crossNetworkGrowthPctPerMin"}, 0.0)
                ) * networkWeight;
                flowGrowthWeighted += firstDoubleValue(
                        summary,
                        {"network_flow_growth_pct_per_min", "flow_growth_pct_per_min", "flowGrowthPctPerMin"},
                        firstDoubleValue(heuristics, {"flow_growth_pct_per_min", "flowGrowthPctPerMin"}, 0.0)
                ) * networkWeight;
                weightSum += networkWeight;
                sampleCount += std::max(0, static_cast<int>(firstInt64Value(resourceForecast, {"sample_count", "sampleCount"}, 0)));

                if (networkWeight >= dominantWeight) {
                    dominantWeight = networkWeight;
                    dominantProcess = firstStringValue(
                            heuristics,
                            {"dominant_process", "dominantProcess"},
                            firstStringValue(simulation, {"dominant_process", "dominantProcess"})
                    );
                    dominantRemoteIp = firstStringValue(
                            heuristics,
                            {"dominant_remote_ip", "dominantRemoteIp"},
                            firstStringValue(simulation, {"dominant_remote_ip", "dominantRemoteIp"})
                    );
                }
            }

            nlohmann::json aggregateServerNode = {
                    {"gpu_count", gpuCountTotal},
                    {"cpu_usage_pct", cpuUsageCount > 0 ? cpuUsageSum / static_cast<double>(cpuUsageCount) : 0.0},
                    {"gpu_utilization_avg_pct", gpuCountTotal > 0 ? gpuUtilWeighted / static_cast<double>(gpuCountTotal) : 0.0},
                    {"gpu_power_total_w", powerWTotal},
                    {"mem_working_set_bytes", memoryWorkingSetBytes}
            };
            nlohmann::json aggregateNetworkSummary = {
                    {"attributed_total_bps", attributedTotalBps},
                    {"cross_network_bps", crossNetworkBps},
                    {"active_flows", activeFlows},
                    {"attribution_confidence", weightSum > 0.0 ? attributionConfidenceWeighted / weightSum : 0.0},
                    {"latency_pressure", weightSum > 0.0 ? latencyPressureWeighted / weightSum : 0.0},
                    {"queue_pressure", weightSum > 0.0 ? queuePressureWeighted / weightSum : 0.0},
                    {"traffic_growth_pct_per_min", weightSum > 0.0 ? trafficGrowthWeighted / weightSum : 0.0},
                    {"cross_network_growth_pct_per_min", weightSum > 0.0 ? crossGrowthWeighted / weightSum : 0.0},
                    {"flow_growth_pct_per_min", weightSum > 0.0 ? flowGrowthWeighted / weightSum : 0.0}
            };
            nlohmann::json aggregateResourceForecast = {
                    {"sample_count", sampleCount},
                    {"simulation", {
                            {"dominant_process", dominantProcess},
                            {"dominant_remote_ip", dominantRemoteIp}
                    }}
            };
            enrichTraceyResourceForecastView(
                    aggregateServerNode,
                    &aggregateNetworkSummary,
                    nlohmann::json::array(),
                    aggregateResourceForecast,
                    nowMs
            );
            return aggregateResourceForecast;
        }

        nlohmann::json extractTraceyLoaderThreatSummary(const nlohmann::json& statusSnapshot) {
            if (!statusSnapshot.is_object()) {
                return nlohmann::json::object();
            }

            const nlohmann::json* loaderThreatNode = firstObjectValue(
                    statusSnapshot,
                    {"loader_threats", "loaderThreats", "loader_security", "loaderSecurity"}
            );
            if (loaderThreatNode == nullptr) {
                const auto loaderIt = statusSnapshot.find("loader");
                if (loaderIt != statusSnapshot.end() && loaderIt->is_object()) {
                    loaderThreatNode = firstObjectValue(
                            *loaderIt,
                            {"threats", "security", "loader_threats", "loaderThreats"}
                    );
                }
            }
            if (loaderThreatNode == nullptr) {
                return nlohmann::json::object();
            }

            const nlohmann::json* summaryNode = firstObjectValue(
                    *loaderThreatNode,
                    {"summary", "totals", "counts"}
            );
            const nlohmann::json& source = summaryNode != nullptr ? *summaryNode : *loaderThreatNode;

            const auto readInt = [&](std::initializer_list<const char*> keys) -> int64_t {
                return std::max<int64_t>(0, firstInt64Value(source, keys, 0));
            };
            const auto readDouble = [&](std::initializer_list<const char*> keys) -> double {
                const auto* value = firstValue(source, keys);
                return value != nullptr ? std::max(0.0, jsonDoubleValue(*value, 0.0)) : 0.0;
            };

            return {
                    {"local_provider_count", readInt({"local_provider_count", "localProviderCount", "providers", "provider_count"})},
                    {"local_artifact_count", readInt({"local_artifact_count", "localArtifactCount", "artifacts", "artifact_count"})},
                    {"remote_provider_count", readInt({"remote_provider_count", "remoteProviderCount"})},
                    {"remote_artifact_count", readInt({"remote_artifact_count", "remoteArtifactCount"})},
                    {"remote_reporters", readInt({"remote_reporters", "remoteReporters", "reporters"})},
                    {"blocked_provider_count", readInt({"blocked_provider_count", "blockedProviderCount", "blocked_providers", "recommended_provider_blocks"})},
                    {"blocked_artifact_count", readInt({"blocked_artifact_count", "blockedArtifactCount", "blocked_artifacts", "recommended_artifact_blocks"})},
                    {"highest_provider_risk", readDouble({"highest_provider_risk", "highestProviderRisk", "provider_risk"})},
                    {"highest_artifact_risk", readDouble({"highest_artifact_risk", "highestArtifactRisk", "artifact_risk"})}
            };
        }

        nlohmann::json sanitizeTraceyProbeWatchAlert(const nlohmann::json& alert) {
            if (!alert.is_object()) {
                return nlohmann::json::object();
            }
            const auto* authorizedNode = firstValue(alert, {"authorized"});
            const auto* knownRouteNode = firstValue(alert, {"known_route", "knownRoute"});
            return {
                    {"ts_ms", std::max<int64_t>(0, firstInt64Value(alert, {"ts_ms", "tsMs"}, 0))},
                    {"surface", firstStringValue(alert, {"surface"})},
                    {"source", firstStringValue(alert, {"source", "source_ip", "sourceIp"})},
                    {"method", firstStringValue(alert, {"method"})},
                    {"path", firstStringValue(alert, {"path"})},
                    {"status_code", std::max<int64_t>(0, firstInt64Value(alert, {"status_code", "statusCode"}, 0))},
                    {"classification", firstStringValue(alert, {"classification"})},
                    {"severity", toLower(firstStringValue(alert, {"severity"}, "unknown"))},
                    {"signal", std::max(0.0, firstDoubleValue(alert, {"signal"}, 0.0))},
                    {"reason", firstStringValue(alert, {"reason"})},
                    {"run_id", firstStringValue(alert, {"run_id", "runId"})},
                    {"authorized", authorizedNode != nullptr ? jsonBoolValue(*authorizedNode, false) : false},
                    {"known_route", knownRouteNode != nullptr ? jsonBoolValue(*knownRouteNode, false) : false}
            };
        }

        nlohmann::json extractTraceyProbeWatchSummary(const nlohmann::json& statusSnapshot) {
            if (!statusSnapshot.is_object()) {
                return nlohmann::json::object();
            }

            const nlohmann::json* probeWatchNode = firstObjectValue(
                    statusSnapshot,
                    {"probe_watch", "probeWatch"}
            );
            if (probeWatchNode == nullptr) {
                return nlohmann::json::object();
            }

            const auto readInt = [&](std::initializer_list<const char*> keys) -> int64_t {
                return std::max<int64_t>(0, firstInt64Value(*probeWatchNode, keys, 0));
            };

            nlohmann::json recentAlerts = nlohmann::json::array();
            int64_t highSeverityAlerts = 0;
            int64_t mediumSeverityAlerts = 0;
            int64_t unauthorizedControlAlerts = 0;
            int64_t cooperativeAlerts = 0;
            int64_t latestAlertMs = 0;
            std::string latestSurface;
            std::string latestClassification;
            std::string latestSeverity;

            const auto* recentAlertsNode = firstArrayValue(*probeWatchNode, {"recent_alerts", "recentAlerts"});
            if (recentAlertsNode != nullptr) {
                for (const auto& alert : *recentAlertsNode) {
                    if (!alert.is_object()) {
                        continue;
                    }
                    const nlohmann::json sanitized = sanitizeTraceyProbeWatchAlert(alert);
                    if (recentAlerts.size() < 12) {
                        recentAlerts.push_back(sanitized);
                    }
                    const std::string severity = toLower(firstStringValue(alert, {"severity"}, "unknown"));
                    const std::string classification = firstStringValue(alert, {"classification"});
                    const int64_t tsMs = std::max<int64_t>(0, firstInt64Value(alert, {"ts_ms", "tsMs"}, 0));
                    if (severity == "high" || severity == "critical") {
                        highSeverityAlerts++;
                    } else if (severity == "medium") {
                        mediumSeverityAlerts++;
                    }
                    if (classification == "unauthorized_control_probe") {
                        unauthorizedControlAlerts++;
                    }
                    if (classification == "cooperative_probe") {
                        cooperativeAlerts++;
                    }
                    if (tsMs >= latestAlertMs) {
                        latestAlertMs = tsMs;
                        latestSurface = firstStringValue(alert, {"surface"});
                        latestClassification = classification;
                        latestSeverity = severity;
                    }
                }
            }

            nlohmann::json surfaces = nlohmann::json::array();
            int64_t alertedSurfaceCount = 0;
            int64_t surfaceCount = 0;
            const auto* surfacesNode = firstArrayValue(*probeWatchNode, {"surfaces"});
            if (surfacesNode != nullptr) {
                for (const auto& surface : *surfacesNode) {
                    if (!surface.is_object()) {
                        continue;
                    }
                    surfaceCount++;
                    const int64_t totalAlerts = std::max<int64_t>(
                            0,
                            firstInt64Value(surface, {"total_alerts", "totalAlerts"}, 0)
                    );
                    if (totalAlerts > 0) {
                        alertedSurfaceCount++;
                    }
                    if (surfaces.size() < 8) {
                        surfaces.push_back({
                                {"surface", firstStringValue(surface, {"surface"})},
                                {"total_observations", std::max<int64_t>(0, firstInt64Value(
                                        surface,
                                        {"total_observations", "totalObservations"},
                                        0
                                ))},
                                {"total_alerts", totalAlerts},
                                {"distinct_sources", std::max<int64_t>(0, firstInt64Value(
                                        surface,
                                        {"distinct_sources", "distinctSources"},
                                        0
                                ))}
                        });
                    }
                }
            }
            if (surfaceCount == 0 && readInt({"total_alerts", "totalAlerts"}) > 0) {
                alertedSurfaceCount = 1;
            }

            return {
                    {"enabled", firstValue(*probeWatchNode, {"enabled"}) != nullptr
                                        ? jsonBoolValue(*firstValue(*probeWatchNode, {"enabled"}), false)
                                        : false},
                    {"total_observations", readInt({"total_observations", "totalObservations"})},
                    {"total_alerts", readInt({"total_alerts", "totalAlerts"})},
                    {"distinct_sources", readInt({"distinct_sources", "distinctSources"})},
                    {"surface_count", surfaceCount},
                    {"alerted_surface_count", alertedSurfaceCount},
                    {"recent_alert_count", static_cast<int64_t>(recentAlerts.size())},
                    {"high_severity_alerts", highSeverityAlerts},
                    {"medium_severity_alerts", mediumSeverityAlerts},
                    {"unauthorized_control_alerts", unauthorizedControlAlerts},
                    {"cooperative_alerts", cooperativeAlerts},
                    {"latest_alert_ms", latestAlertMs},
                    {"latest_surface", latestSurface},
                    {"latest_classification", latestClassification},
                    {"latest_severity", latestSeverity},
                    {"recent_alerts", recentAlerts},
                    {"surfaces", surfaces}
            };
        }

        const nlohmann::json* extractTraceyStatusSnapshotNode(const nlohmann::json& metricsNode) {
            return firstObjectValue(metricsNode, {"status_snapshot", "statusSnapshot"});
        }

        const nlohmann::json* extractContinuumTelemetryNode(const nlohmann::json& metricsNode) {
            const auto* statusSnapshot = extractTraceyStatusSnapshotNode(metricsNode);
            if (statusSnapshot == nullptr) {
                return nullptr;
            }
            return firstObjectValue(*statusSnapshot, {"continuum_telemetry", "continuumTelemetry", "continuum"});
        }

        const nlohmann::json* extractTraceyGuardNode(const nlohmann::json& metricsNode) {
            const auto* statusSnapshot = extractTraceyStatusSnapshotNode(metricsNode);
            if (statusSnapshot == nullptr) {
                return nullptr;
            }
            return firstObjectValue(*statusSnapshot, {"tracey_guard", "traceyGuard"});
        }

        const nlohmann::json* extractTraceyGuardSummaryNode(const nlohmann::json& metricsNode) {
            const auto* traceyGuard = extractTraceyGuardNode(metricsNode);
            if (traceyGuard == nullptr) {
                return nullptr;
            }
            return firstObjectValue(*traceyGuard, {"summary"});
        }

        const nlohmann::json* extractContinuumAssessmentNode(const nlohmann::json& metricsNode) {
            const auto* statusSnapshot = extractTraceyStatusSnapshotNode(metricsNode);
            if (statusSnapshot == nullptr) {
                return nullptr;
            }
            return firstObjectValue(
                    *statusSnapshot,
                    {"continuum_assessment", "continuumAssessment", "compromise_assessment", "compromiseAssessment"}
            );
        }

        const nlohmann::json* extractContinuumAssessmentSummaryNode(const nlohmann::json& metricsNode) {
            const auto* assessment = extractContinuumAssessmentNode(metricsNode);
            if (assessment == nullptr) {
                return nullptr;
            }
            const auto* summary = firstObjectValue(*assessment, {"summary"});
            return summary != nullptr ? summary : assessment;
        }

        std::string defaultTopologyLabel(const std::string& value, const std::string& fallback) {
            const std::string cleaned = trim(value);
            return cleaned.empty() ? fallback : cleaned;
        }

        int traceyStatusSeverityRank(const std::string& status) {
            const std::string normalized = normalizeTraceyStatus(status);
            if (normalized == "offline") {
                return 3;
            }
            if (normalized == "degraded") {
                return 2;
            }
            if (normalized == "unknown") {
                return 1;
            }
            return 0;
        }

        std::string rollupTraceyStatusCounts(int healthy, int degraded, int offline, int unknown) {
            if (offline > 0) {
                return "offline";
            }
            if (degraded > 0) {
                return "degraded";
            }
            if (healthy > 0 && unknown == 0) {
                return "healthy";
            }
            if (healthy > 0) {
                return "degraded";
            }
            return "unknown";
        }

        void sortGpuTiles(nlohmann::json& gpus) {
            if (!gpus.is_array()) {
                return;
            }
            std::sort(gpus.begin(), gpus.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
                const int64_t leftSlot = firstInt64Value(left, {"slot_index", "slotIndex"}, 1'000'000);
                const int64_t rightSlot = firstInt64Value(right, {"slot_index", "slotIndex"}, 1'000'000);
                if (leftSlot != rightSlot) {
                    return leftSlot < rightSlot;
                }
                const std::string leftId = firstStringValue(left, {"gpu_id", "gpuId"});
                const std::string rightId = firstStringValue(right, {"gpu_id", "gpuId"});
                return leftId < rightId;
            });
        }

        void sortActionsByTime(nlohmann::json& actions) {
            if (!actions.is_array()) {
                return;
            }
            std::sort(actions.begin(), actions.end(), [](const nlohmann::json& left, const nlohmann::json& right) {
                return firstInt64Value(left, {"ts_ms", "tsMs"}, 0) > firstInt64Value(right, {"ts_ms", "tsMs"}, 0);
            });
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

        struct TraceyEffectiveStatus {
            std::string status;
            bool stale{false};
            int64_t lastSignalMs{0};
            int64_t ageSeconds{0};
        };

        TraceyEffectiveStatus buildTraceyEffectiveStatus(const std::string& rawStatus,
                                                        bool staleFlag,
                                                        int64_t lastSeenEpochMs,
                                                        int64_t lastAnnouncementEpochMs,
                                                        int64_t nowMs,
                                                        int64_t staleAfterSeconds) {
            TraceyEffectiveStatus view;
            view.lastSignalMs = std::max(lastSeenEpochMs, lastAnnouncementEpochMs);
            if (view.lastSignalMs > 0 && nowMs > view.lastSignalMs) {
                view.ageSeconds = (nowMs - view.lastSignalMs) / 1000;
            }
            view.stale = staleFlag || view.ageSeconds > staleAfterSeconds;
            view.status = view.stale ? "offline" : normalizeTraceyStatus(rawStatus);
            return view;
        }

        nlohmann::json buildTraceyFleetViewFromAgentsImpl(const std::vector<nlohmann::json>& agentViews,
                                                          int64_t nowMs) {
            struct RackAgg {
                std::string rack;
                std::string zone;
                int agentCount{0};
                int healthy{0};
                int degraded{0};
                int offline{0};
                int unknown{0};
                int reachable{0};
                int gpuCount{0};
                int probeAlerts{0};
                int probeHighAlerts{0};
                int probeAlertedSurfaces{0};
                int traceyGuardQuarantined{0};
                int blockedLoaderProviders{0};
                int blockedLoaderArtifacts{0};
                int thermalAlerts{0};
                int fanAlerts{0};
                int autonomyActions{0};
                int activeFlows{0};
                int listeners{0};
                double gpuUtilSum{0.0};
                int gpuUtilCount{0};
                double gpuPowerTotal{0.0};
                double netRxBps{0.0};
                double netTxBps{0.0};
                double attributedTotalBps{0.0};
                double crossNetworkBps{0.0};
                double projectedTotalBps15m{0.0};
                double projectedCrossNetworkBps15m{0.0};
                double projectedActiveFlows15m{0.0};
                double autonomyRiskMax{0.0};
                bool hasAutonomyRisk{false};
                double gpuTempMax{0.0};
                bool hasGpuTempMax{false};
                uint64_t eccCorrectedTotal{0};
                uint64_t eccUncorrectedTotal{0};
                nlohmann::json heatCells{nlohmann::json::array()};
            };

            struct ZoneAgg {
                std::string zone;
                int agentCount{0};
                int healthy{0};
                int degraded{0};
                int offline{0};
                int unknown{0};
                int reachable{0};
                int gpuCount{0};
                int rackCount{0};
                int probeAlerts{0};
                int probeHighAlerts{0};
                int probeAlertedSurfaces{0};
                int thermalAlerts{0};
                int fanAlerts{0};
                int autonomyActions{0};
                int activeFlows{0};
                int listeners{0};
                int traceyGuardQuarantined{0};
                double gpuUtilSum{0.0};
                int gpuUtilCount{0};
                double gpuPowerTotal{0.0};
                double attributedTotalBps{0.0};
                double crossNetworkBps{0.0};
                double projectedTotalBps15m{0.0};
                double gpuTempMax{0.0};
                bool hasGpuTempMax{false};
                std::set<std::string> racks;
            };

            int healthy = 0;
            int degraded = 0;
            int offline = 0;
            int unknown = 0;
            int reachable = 0;
            int gpuCount = 0;
            int probeAlertCount = 0;
            int probeHighAlertCount = 0;
            int probeAlertedSurfaceCount = 0;
            int thermalAlerts = 0;
            int fanAlerts = 0;
            int traceyGuardQuarantined = 0;
            int blockedLoaderProviders = 0;
            int blockedLoaderArtifacts = 0;
            int autonomyActionCount = 0;
            int agentsWithCves = 0;
            int agentsWithKev = 0;
            int elevatedAgents = 0;
            int compromisedAgents = 0;
            int totalCveMatches = 0;
            int totalKevMatches = 0;
            int activeFlows = 0;
            int listeners = 0;
            int forecastAgents = 0;
            double gpuUtilSum = 0.0;
            int gpuUtilCount = 0;
            double gpuPowerTotal = 0.0;
            double attributedTotalBps = 0.0;
            double crossNetworkBps = 0.0;
            double projectedTotalBps15m = 0.0;
            double projectedCrossNetworkBps15m = 0.0;
            double projectedActiveFlows15m = 0.0;
            double maxTrafficGrowthPctPerMin = 0.0;
            double autonomyRiskMax = 0.0;
            bool hasAutonomyRisk = false;
            double highestCompromiseRisk = 0.0;
            bool hasCompromiseRisk = false;
            double gpuTempMax = 0.0;
            bool hasGpuTempMax = false;
            uint64_t eccCorrectedTotal = 0;
            uint64_t eccUncorrectedTotal = 0;

            std::map<std::string, RackAgg> rackAggs;
            std::map<std::string, ZoneAgg> zoneAggs;
            nlohmann::json recentActions = nlohmann::json::array();
            nlohmann::json agents = nlohmann::json::array();

            for (const auto& agentView : agentViews) {
                const std::string status = normalizeTraceyStatus(firstStringValue(agentView, {"status"}));
                if (status == "healthy") {
                    healthy++;
                } else if (status == "degraded") {
                    degraded++;
                } else if (status == "offline") {
                    offline++;
                } else {
                    unknown++;
                }
                if (agentView.value("status_reachable", false)) {
                    reachable++;
                }

                const nlohmann::json summary = firstObjectValue(agentView, {"summary"}) != nullptr
                                               ? *firstObjectValue(agentView, {"summary"})
                                               : nlohmann::json::object();
                const nlohmann::json traceyGuardSummary = firstObjectValue(agentView, {"tracey_guard_summary"}) != nullptr
                                                          ? *firstObjectValue(agentView, {"tracey_guard_summary"})
                                                          : nlohmann::json::object();
                const nlohmann::json loaderThreats = firstObjectValue(agentView, {"loader_threats"}) != nullptr
                                                     ? *firstObjectValue(agentView, {"loader_threats"})
                                                     : nlohmann::json::object();
                const nlohmann::json assessmentSummary = firstObjectValue(agentView, {"continuum_assessment_summary"}) != nullptr
                                                         ? *firstObjectValue(agentView, {"continuum_assessment_summary"})
                                                         : nlohmann::json::object();
                const auto* serverNode = firstObjectValue(agentView, {"server"});
                const auto* gpus = firstArrayValue(agentView, {"gpus"});
                const auto* actions = firstArrayValue(agentView, {"recent_actions"});

                const std::string rack = defaultTopologyLabel(firstStringValue(agentView, {"rack"}), "unassigned");
                const std::string zone = defaultTopologyLabel(firstStringValue(agentView, {"zone"}), "unassigned");
                auto& rackAgg = rackAggs[rack];
                if (rackAgg.rack.empty()) {
                    rackAgg.rack = rack;
                    rackAgg.zone = zone;
                }
                auto& zoneAgg = zoneAggs[zone];
                if (zoneAgg.zone.empty()) {
                    zoneAgg.zone = zone;
                }

                rackAgg.agentCount++;
                zoneAgg.agentCount++;
                zoneAgg.racks.insert(rack);
                if (agentView.value("status_reachable", false)) {
                    rackAgg.reachable++;
                    zoneAgg.reachable++;
                }

                if (status == "healthy") {
                    rackAgg.healthy++;
                    zoneAgg.healthy++;
                } else if (status == "degraded") {
                    rackAgg.degraded++;
                    zoneAgg.degraded++;
                } else if (status == "offline") {
                    rackAgg.offline++;
                    zoneAgg.offline++;
                } else {
                    rackAgg.unknown++;
                    zoneAgg.unknown++;
                }

                const int agentGpuCount = std::max(0, static_cast<int>(firstInt64Value(summary, {"gpu_count"}, 0)));
                rackAgg.gpuCount += agentGpuCount;
                zoneAgg.gpuCount += agentGpuCount;
                gpuCount += agentGpuCount;

                const int agentProbeAlerts = std::max(0, static_cast<int>(firstInt64Value(summary, {"probe_alerts"}, 0)));
                const int agentProbeHighAlerts = std::max(0, static_cast<int>(firstInt64Value(summary, {"probe_high_alerts"}, 0)));
                const int agentProbeAlertedSurfaces = std::max(0, static_cast<int>(firstInt64Value(summary, {"probe_alerted_surfaces"}, 0)));
                rackAgg.probeAlerts += agentProbeAlerts;
                rackAgg.probeHighAlerts += agentProbeHighAlerts;
                rackAgg.probeAlertedSurfaces += agentProbeAlertedSurfaces;
                zoneAgg.probeAlerts += agentProbeAlerts;
                zoneAgg.probeHighAlerts += agentProbeHighAlerts;
                zoneAgg.probeAlertedSurfaces += agentProbeAlertedSurfaces;
                probeAlertCount += agentProbeAlerts;
                probeHighAlertCount += agentProbeHighAlerts;
                probeAlertedSurfaceCount += agentProbeAlertedSurfaces;

                const int agentThermalAlerts = std::max(0, static_cast<int>(firstInt64Value(summary, {"thermal_alerts"}, 0)));
                const int agentFanAlerts = std::max(0, static_cast<int>(firstInt64Value(summary, {"fan_alerts"}, 0)));
                rackAgg.thermalAlerts += agentThermalAlerts;
                rackAgg.fanAlerts += agentFanAlerts;
                zoneAgg.thermalAlerts += agentThermalAlerts;
                zoneAgg.fanAlerts += agentFanAlerts;
                thermalAlerts += agentThermalAlerts;
                fanAlerts += agentFanAlerts;

                const int agentQuarantined = std::max(0, static_cast<int>(firstInt64Value(
                        traceyGuardSummary,
                        {"quarantined_devices", "quarantinedDevices"},
                        0
                )));
                rackAgg.traceyGuardQuarantined += agentQuarantined;
                zoneAgg.traceyGuardQuarantined += agentQuarantined;
                traceyGuardQuarantined += agentQuarantined;

                const int agentBlockedProviders = std::max(0, static_cast<int>(firstInt64Value(
                        loaderThreats,
                        {"blocked_provider_count", "blockedProviderCount"},
                        0
                )));
                const int agentBlockedArtifacts = std::max(0, static_cast<int>(firstInt64Value(
                        loaderThreats,
                        {"blocked_artifact_count", "blockedArtifactCount"},
                        0
                )));
                rackAgg.blockedLoaderProviders += agentBlockedProviders;
                rackAgg.blockedLoaderArtifacts += agentBlockedArtifacts;
                blockedLoaderProviders += agentBlockedProviders;
                blockedLoaderArtifacts += agentBlockedArtifacts;

                const double agentUtil = firstDoubleValue(summary, {"gpu_utilization_avg_pct"}, -1.0);
                if (agentUtil >= 0.0) {
                    rackAgg.gpuUtilSum += agentUtil * std::max(1, agentGpuCount);
                    rackAgg.gpuUtilCount += std::max(1, agentGpuCount);
                    zoneAgg.gpuUtilSum += agentUtil * std::max(1, agentGpuCount);
                    zoneAgg.gpuUtilCount += std::max(1, agentGpuCount);
                    gpuUtilSum += agentUtil * std::max(1, agentGpuCount);
                    gpuUtilCount += std::max(1, agentGpuCount);
                }

                const double agentPower = firstDoubleValue(summary, {"gpu_power_total_w"}, 0.0);
                if (agentPower > 0.0) {
                    rackAgg.gpuPowerTotal += agentPower;
                    zoneAgg.gpuPowerTotal += agentPower;
                    gpuPowerTotal += agentPower;
                }

                const double agentNetRx = firstDoubleValue(summary, {"net_rx_bps"}, 0.0);
                const double agentNetTx = firstDoubleValue(summary, {"net_tx_bps"}, 0.0);
                if (agentNetRx > 0.0) {
                    rackAgg.netRxBps += agentNetRx;
                }
                if (agentNetTx > 0.0) {
                    rackAgg.netTxBps += agentNetTx;
                }

                const double agentAttributedTotal = std::max(0.0, firstDoubleValue(summary, {"attributed_total_bps"}, 0.0));
                const double agentCrossNetworkBps = std::max(0.0, firstDoubleValue(summary, {"cross_network_bps"}, 0.0));
                const int agentActiveFlows = std::max(0, static_cast<int>(firstInt64Value(summary, {"network_active_flows"}, 0)));
                const int agentListeners = std::max(0, static_cast<int>(firstInt64Value(summary, {"network_listeners"}, 0)));
                const double agentProjectedTotalBps15m = std::max(0.0, firstDoubleValue(summary, {"projected_total_bps_15m"}, 0.0));
                const double agentProjectedCrossBps15m = std::max(0.0, firstDoubleValue(summary, {"projected_cross_network_bps_15m"}, 0.0));
                const double agentProjectedActiveFlows15m = std::max(0.0, firstDoubleValue(summary, {"projected_active_flows_15m"}, 0.0));
                const double agentTrafficGrowthPctPerMin = std::max(0.0, firstDoubleValue(summary, {"network_traffic_growth_pct_per_min"}, 0.0));
                const std::string forecastStatus = toLower(firstStringValue(summary, {"forecast_status"}, "disabled"));

                rackAgg.attributedTotalBps += agentAttributedTotal;
                rackAgg.crossNetworkBps += agentCrossNetworkBps;
                rackAgg.activeFlows += agentActiveFlows;
                rackAgg.listeners += agentListeners;
                rackAgg.projectedTotalBps15m += agentProjectedTotalBps15m;
                rackAgg.projectedCrossNetworkBps15m += agentProjectedCrossBps15m;
                rackAgg.projectedActiveFlows15m += agentProjectedActiveFlows15m;
                zoneAgg.attributedTotalBps += agentAttributedTotal;
                zoneAgg.crossNetworkBps += agentCrossNetworkBps;
                zoneAgg.activeFlows += agentActiveFlows;
                zoneAgg.listeners += agentListeners;
                zoneAgg.projectedTotalBps15m += agentProjectedTotalBps15m;
                attributedTotalBps += agentAttributedTotal;
                crossNetworkBps += agentCrossNetworkBps;
                activeFlows += agentActiveFlows;
                listeners += agentListeners;
                projectedTotalBps15m += agentProjectedTotalBps15m;
                projectedCrossNetworkBps15m += agentProjectedCrossBps15m;
                projectedActiveFlows15m += agentProjectedActiveFlows15m;
                maxTrafficGrowthPctPerMin = std::max(maxTrafficGrowthPctPerMin, agentTrafficGrowthPctPerMin);
                if (forecastStatus == "forecasting" || forecastStatus == "collecting") {
                    forecastAgents++;
                }

                const double agentTempMax = firstDoubleValue(summary, {"gpu_temperature_max_c"}, -1.0);
                if (agentTempMax >= 0.0) {
                    rackAgg.gpuTempMax = rackAgg.hasGpuTempMax ? std::max(rackAgg.gpuTempMax, agentTempMax) : agentTempMax;
                    rackAgg.hasGpuTempMax = true;
                    zoneAgg.gpuTempMax = zoneAgg.hasGpuTempMax ? std::max(zoneAgg.gpuTempMax, agentTempMax) : agentTempMax;
                    zoneAgg.hasGpuTempMax = true;
                    gpuTempMax = hasGpuTempMax ? std::max(gpuTempMax, agentTempMax) : agentTempMax;
                    hasGpuTempMax = true;
                }

                const double agentAutonomyRisk = firstDoubleValue(summary, {"autonomy_risk"}, -1.0);
                if (agentAutonomyRisk >= 0.0) {
                    rackAgg.autonomyRiskMax = rackAgg.hasAutonomyRisk ? std::max(rackAgg.autonomyRiskMax, agentAutonomyRisk) : agentAutonomyRisk;
                    rackAgg.hasAutonomyRisk = true;
                    autonomyRiskMax = hasAutonomyRisk ? std::max(autonomyRiskMax, agentAutonomyRisk) : agentAutonomyRisk;
                    hasAutonomyRisk = true;
                }

                const int agentCveMatches = std::max(0, static_cast<int>(firstInt64Value(
                        assessmentSummary,
                        {"cve_matches", "cveMatches"},
                        0
                )));
                const int agentKevMatches = std::max(0, static_cast<int>(firstInt64Value(
                        assessmentSummary,
                        {"kev_matches", "kevMatches"},
                        0
                )));
                const double agentCompromiseRisk = firstDoubleValue(
                        assessmentSummary,
                        {"compromise_risk", "compromiseRisk"},
                        -1.0
                );
                if (agentCveMatches > 0) {
                    agentsWithCves++;
                }
                if (agentKevMatches > 0) {
                    agentsWithKev++;
                }
                totalCveMatches += agentCveMatches;
                totalKevMatches += agentKevMatches;
                if (agentCompromiseRisk >= 0.80) {
                    compromisedAgents++;
                } else if (agentCompromiseRisk >= 0.55) {
                    elevatedAgents++;
                }
                if (agentCompromiseRisk >= 0.0) {
                    highestCompromiseRisk = hasCompromiseRisk
                                            ? std::max(highestCompromiseRisk, agentCompromiseRisk)
                                            : agentCompromiseRisk;
                    hasCompromiseRisk = true;
                }

                const auto* ecc = serverNode != nullptr ? firstObjectValue(*serverNode, {"ecc"}) : nullptr;
                if (ecc != nullptr) {
                    const auto corrected = static_cast<uint64_t>(std::max<int64_t>(0, firstInt64Value(*ecc, {"corrected_total", "correctedTotal"}, 0)));
                    const auto uncorrected = static_cast<uint64_t>(std::max<int64_t>(0, firstInt64Value(*ecc, {"uncorrected_total", "uncorrectedTotal"}, 0)));
                    rackAgg.eccCorrectedTotal += corrected;
                    rackAgg.eccUncorrectedTotal += uncorrected;
                    eccCorrectedTotal += corrected;
                    eccUncorrectedTotal += uncorrected;
                }

                if (actions != nullptr) {
                    for (const auto& action : *actions) {
                        nlohmann::json enriched = action.is_object() ? action : nlohmann::json::object();
                        enriched["agent_id"] = firstStringValue(agentView, {"agent_id"});
                        enriched["host"] = firstStringValue(agentView, {"host"});
                        enriched["rack"] = rack;
                        enriched["zone"] = zone;
                        recentActions.push_back(enriched);
                        const std::string category = toLower(firstStringValue(enriched, {"category"}));
                        if (category == "autonomy" || category == "automation") {
                            rackAgg.autonomyActions++;
                            zoneAgg.autonomyActions++;
                            autonomyActionCount++;
                        }
                    }
                }

                if (gpus != nullptr) {
                    for (const auto& gpu : *gpus) {
                        if (!gpu.is_object()) {
                            continue;
                        }
                        nlohmann::json cell = gpu;
                        cell["agent_id"] = firstStringValue(agentView, {"agent_id"});
                        cell["host"] = firstStringValue(agentView, {"host"});
                        cell["rack"] = rack;
                        cell["zone"] = zone;
                        cell["status"] = status;
                        rackAgg.heatCells.push_back(cell);
                    }
                }

                agents.push_back({
                        {"agent_id", firstStringValue(agentView, {"agent_id"})},
                        {"cluster", firstStringValue(agentView, {"cluster"})},
                        {"status", status},
                        {"host", firstStringValue(agentView, {"host"})},
                        {"rack", rack},
                        {"zone", zone},
                        {"gpu_count", agentGpuCount},
                        {"gpu_utilization_avg_pct", agentUtil >= 0.0 ? agentUtil : 0.0},
                        {"gpu_temperature_max_c", agentTempMax >= 0.0 ? agentTempMax : 0.0},
                        {"autonomy_risk", agentAutonomyRisk >= 0.0 ? agentAutonomyRisk : 0.0},
                        {"compromise_risk", agentCompromiseRisk >= 0.0 ? agentCompromiseRisk : 0.0},
                        {"cve_matches", agentCveMatches},
                        {"kev_matches", agentKevMatches},
                        {"assessment_status", firstStringValue(assessmentSummary, {"status"}, "unknown")},
                        {"assessment_action", firstStringValue(assessmentSummary, {"recommended_action", "recommendedAction"})},
                        {"attributed_total_bps", agentAttributedTotal},
                        {"cross_network_bps", agentCrossNetworkBps},
                        {"network_active_flows", agentActiveFlows},
                        {"network_listeners", agentListeners},
                        {"network_estimated_flows", std::max(0, static_cast<int>(firstInt64Value(summary, {"network_estimated_flows"}, 0)))},
                        {"network_udp_active_flows", std::max(0, static_cast<int>(firstInt64Value(summary, {"network_udp_active_flows"}, 0)))},
                        {"network_udp_drop_delta", std::max<int64_t>(0, firstInt64Value(summary, {"network_udp_drop_delta"}, 0))},
                        {"network_attribution_confidence", std::max(0.0, firstDoubleValue(summary, {"network_attribution_confidence"}, 0.0))},
                        {"network_collector_backend", firstStringValue(summary, {"network_collector_backend"})},
                        {"projected_total_bps_5m", std::max(0.0, firstDoubleValue(summary, {"projected_total_bps_5m"}, 0.0))},
                        {"projected_total_bps_15m", agentProjectedTotalBps15m},
                        {"projected_cross_network_bps_5m", std::max(0.0, firstDoubleValue(summary, {"projected_cross_network_bps_5m"}, 0.0))},
                        {"projected_cross_network_bps_15m", std::max(0.0, firstDoubleValue(summary, {"projected_cross_network_bps_15m"}, 0.0))},
                        {"projected_active_flows_5m", std::max(0.0, firstDoubleValue(summary, {"projected_active_flows_5m"}, 0.0))},
                        {"projected_active_flows_15m", std::max(0.0, firstDoubleValue(summary, {"projected_active_flows_15m"}, 0.0))},
                        {"estimated_network_bps", std::max(0.0, firstDoubleValue(summary, {"estimated_network_bps"}, 0.0))},
                        {"forecast_status", forecastStatus},
                        {"network", serverNode != nullptr ? serverNode->value("network", nlohmann::json::object()) : nlohmann::json::object()},
                        {"resource_forecast", agentView.value("resource_forecast", nlohmann::json::object())},
                        {"recent_action_count", std::max(0, static_cast<int>(firstInt64Value(summary, {"recent_action_count"}, 0)))},
                        {"last_seen_seconds_ago", std::max<int64_t>(0, firstInt64Value(agentView, {"last_seen_seconds_ago"}, 0))}
                });
            }

            sortActionsByTime(recentActions);
            recentActions = limitedJsonArray(recentActions, 64);

            nlohmann::json zoneBreakdown = nlohmann::json::array();
            for (const auto& [zoneKey, agg] : zoneAggs) {
                (void)zoneKey;
                zoneBreakdown.push_back({
                        {"zone", agg.zone},
                        {"health", rollupTraceyStatusCounts(agg.healthy, agg.degraded, agg.offline, agg.unknown)},
                        {"agent_count", agg.agentCount},
                        {"rack_count", static_cast<int>(agg.racks.size())},
                        {"gpu_count", agg.gpuCount},
                        {"reachable_agents", agg.reachable},
                        {"gpu_utilization_avg_pct", agg.gpuUtilCount > 0 ? agg.gpuUtilSum / static_cast<double>(agg.gpuUtilCount) : 0.0},
                        {"gpu_temperature_max_c", agg.hasGpuTempMax ? agg.gpuTempMax : 0.0},
                        {"gpu_power_total_w", agg.gpuPowerTotal},
                        {"probe_alerts", agg.probeAlerts},
                        {"probe_high_alerts", agg.probeHighAlerts},
                        {"probe_alerted_surfaces", agg.probeAlertedSurfaces},
                        {"attributed_total_bps", agg.attributedTotalBps},
                        {"cross_network_bps", agg.crossNetworkBps},
                        {"network_active_flows", agg.activeFlows},
                        {"network_listeners", agg.listeners},
                        {"projected_total_bps_15m", agg.projectedTotalBps15m},
                        {"thermal_alerts", agg.thermalAlerts},
                        {"fan_alerts", agg.fanAlerts},
                        {"tracey_guard_quarantined", agg.traceyGuardQuarantined},
                        {"autonomy_actions", agg.autonomyActions}
                });
            }

            nlohmann::json racks = nlohmann::json::array();
            nlohmann::json gpuHeatmap = nlohmann::json::array();
            for (auto& [rackKey, agg] : rackAggs) {
                (void)rackKey;
                sortGpuTiles(agg.heatCells);
                racks.push_back({
                        {"rack", agg.rack},
                        {"zone", agg.zone},
                        {"health", rollupTraceyStatusCounts(agg.healthy, agg.degraded, agg.offline, agg.unknown)},
                        {"agent_count", agg.agentCount},
                        {"reachable_agents", agg.reachable},
                        {"gpu_count", agg.gpuCount},
                        {"gpu_utilization_avg_pct", agg.gpuUtilCount > 0 ? agg.gpuUtilSum / static_cast<double>(agg.gpuUtilCount) : 0.0},
                        {"gpu_temperature_max_c", agg.hasGpuTempMax ? agg.gpuTempMax : 0.0},
                        {"gpu_power_total_w", agg.gpuPowerTotal},
                        {"net_rx_bps", agg.netRxBps},
                        {"net_tx_bps", agg.netTxBps},
                        {"probe_alerts", agg.probeAlerts},
                        {"probe_high_alerts", agg.probeHighAlerts},
                        {"probe_alerted_surfaces", agg.probeAlertedSurfaces},
                        {"attributed_total_bps", agg.attributedTotalBps},
                        {"cross_network_bps", agg.crossNetworkBps},
                        {"network_active_flows", agg.activeFlows},
                        {"network_listeners", agg.listeners},
                        {"projected_total_bps_15m", agg.projectedTotalBps15m},
                        {"projected_cross_network_bps_15m", agg.projectedCrossNetworkBps15m},
                        {"projected_active_flows_15m", agg.projectedActiveFlows15m},
                        {"thermal_alerts", agg.thermalAlerts},
                        {"fan_alerts", agg.fanAlerts},
                        {"ecc_corrected_total", agg.eccCorrectedTotal},
                        {"ecc_uncorrected_total", agg.eccUncorrectedTotal},
                        {"tracey_guard_quarantined", agg.traceyGuardQuarantined},
                        {"blocked_loader_providers", agg.blockedLoaderProviders},
                        {"blocked_loader_artifacts", agg.blockedLoaderArtifacts},
                        {"autonomy_actions", agg.autonomyActions},
                        {"autonomy_risk_max", agg.hasAutonomyRisk ? agg.autonomyRiskMax : 0.0}
                });
                gpuHeatmap.push_back({
                        {"rack", agg.rack},
                        {"zone", agg.zone},
                        {"cells", agg.heatCells}
                });
            }

            return {
                    {"generated_epoch_ms", nowMs},
                    {"summary", {
                            {"agents_total", static_cast<int>(agentViews.size())},
                            {"healthy", healthy},
                            {"degraded", degraded},
                            {"offline", offline},
                            {"unknown", unknown},
                            {"reachable", reachable},
                            {"racks_total", static_cast<int>(rackAggs.size())},
                            {"zones_total", static_cast<int>(zoneAggs.size())},
                            {"gpu_total", gpuCount},
                            {"probe_alerts", probeAlertCount},
                            {"probe_high_alerts", probeHighAlertCount},
                            {"probe_alerted_surfaces", probeAlertedSurfaceCount},
                            {"gpu_utilization_avg_pct", gpuUtilCount > 0 ? gpuUtilSum / static_cast<double>(gpuUtilCount) : 0.0},
                            {"gpu_temperature_max_c", hasGpuTempMax ? gpuTempMax : 0.0},
                            {"gpu_power_total_w", gpuPowerTotal},
                            {"attributed_total_bps", attributedTotalBps},
                            {"cross_network_bps", crossNetworkBps},
                            {"network_active_flows", activeFlows},
                            {"network_listeners", listeners},
                            {"projected_total_bps_15m", projectedTotalBps15m},
                            {"projected_cross_network_bps_15m", projectedCrossNetworkBps15m},
                            {"projected_active_flows_15m", projectedActiveFlows15m},
                            {"forecast_agents", forecastAgents},
                            {"network_traffic_growth_pct_per_min_max", maxTrafficGrowthPctPerMin},
                            {"thermal_alerts", thermalAlerts},
                            {"fan_alerts", fanAlerts},
                            {"ecc_corrected_total", eccCorrectedTotal},
                            {"ecc_uncorrected_total", eccUncorrectedTotal},
                            {"tracey_guard_quarantined", traceyGuardQuarantined},
                            {"blocked_loader_providers", blockedLoaderProviders},
                            {"blocked_loader_artifacts", blockedLoaderArtifacts},
                            {"autonomy_actions", autonomyActionCount},
                            {"autonomy_risk_max", hasAutonomyRisk ? autonomyRiskMax : 0.0},
                            {"agents_with_cves", agentsWithCves},
                            {"agents_with_kev", agentsWithKev},
                            {"elevated_agents", elevatedAgents},
                            {"compromised_agents", compromisedAgents},
                            {"cve_matches", totalCveMatches},
                            {"kev_matches", totalKevMatches},
                            {"compromise_risk_max", hasCompromiseRisk ? highestCompromiseRisk : 0.0}
                    }},
                    {"zone_breakdown", zoneBreakdown},
                    {"racks", racks},
                    {"gpu_heatmap", gpuHeatmap},
                    {"recent_actions", recentActions},
                    {"agents", agents}
            };
        }

        std::string clusterIdentityKey(const nlohmann::json& cluster) {
            const std::string name = toLower(firstStringValue(cluster, {"name"}));
            const std::string id = toLower(firstStringValue(cluster, {"id", "cluster_id", "uuid"}));
            return name + "|" + id;
        }

        void appendClusterArray(const nlohmann::json& clusterArray,
                                std::vector<nlohmann::json>& out,
                                std::set<std::string>& seenKeys) {
            if (!clusterArray.is_array()) {
                return;
            }
            for (const auto& item : clusterArray) {
                if (!item.is_object()) {
                    continue;
                }
                nlohmann::json normalized = item;
                normalizeClusterStatus(normalized);
                const std::string key = clusterIdentityKey(normalized);
                if (!key.empty() && seenKeys.find(key) != seenKeys.end()) {
                    continue;
                }
                if (!key.empty()) {
                    seenKeys.insert(key);
                }
                out.push_back(normalized);
            }
        }

        std::vector<nlohmann::json> collectClusterObjects(const nlohmann::json& payload) {
            std::vector<nlohmann::json> clusters;
            std::set<std::string> seenKeys;

            if (payload.is_array()) {
                appendClusterArray(payload, clusters, seenKeys);
                return clusters;
            }

            if (!payload.is_object()) {
                return clusters;
            }

            if (payload.contains("clusters")) {
                appendClusterArray(payload["clusters"], clusters, seenKeys);
            }
            if (payload.contains("data")) {
                if (payload["data"].is_array()) {
                    appendClusterArray(payload["data"], clusters, seenKeys);
                } else if (payload["data"].is_object() && payload["data"].contains("clusters")) {
                    appendClusterArray(payload["data"]["clusters"], clusters, seenKeys);
                }
            }
            if (payload.contains("items")) {
                appendClusterArray(payload["items"], clusters, seenKeys);
            }
            if (clusters.empty() && payload.contains("cluster") && payload["cluster"].is_object()) {
                nlohmann::json normalized = payload["cluster"];
                normalizeClusterStatus(normalized);
                clusters.push_back(normalized);
            }

            return clusters;
        }

        bool parseBoolValue(const char* raw, bool fallback) {
            if (!raw || !*raw) {
                return fallback;
            }
            const std::string value = toLower(trim(raw));
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                return false;
            }
            return fallback;
        }

        int64_t parseInt64Value(const char* raw, int64_t fallback, int64_t minValue, int64_t maxValue) {
            if (!raw || !*raw) {
                return fallback;
            }
            try {
                int64_t parsed = std::stoll(raw);
                if (parsed < minValue) {
                    parsed = minValue;
                } else if (parsed > maxValue) {
                    parsed = maxValue;
                }
                return parsed;
            } catch (const std::exception&) {
                return fallback;
            }
        }

        struct TraceyEndpoint {
            std::string host;
            int port{80};
            bool https{false};
            std::string basePath;
            std::string normalized;
        };

        bool isPrivateIpv4(const in_addr& addr) {
            const uint32_t ip = ntohl(addr.s_addr);
            if ((ip & 0xFF000000U) == 0x0A000000U) { // 10.0.0.0/8
                return true;
            }
            if ((ip & 0xFFF00000U) == 0xAC100000U) { // 172.16.0.0/12
                return true;
            }
            if ((ip & 0xFFFF0000U) == 0xC0A80000U) { // 192.168.0.0/16
                return true;
            }
            if ((ip & 0xFF000000U) == 0x7F000000U) { // 127.0.0.0/8
                return true;
            }
            if ((ip & 0xFFFF0000U) == 0xA9FE0000U) { // 169.254.0.0/16
                return true;
            }
            if ((ip & 0xFFC00000U) == 0x64400000U) { // 100.64.0.0/10
                return true;
            }
            return false;
        }

        bool isPrivateIpv6(const in6_addr& addr) {
            static const in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
            if (std::memcmp(&addr, &loopback, sizeof(in6_addr)) == 0) {
                return true;
            }
            if ((addr.s6_addr[0] & 0xFEU) == 0xFCU) { // fc00::/7
                return true;
            }
            if (addr.s6_addr[0] == 0xFEU && (addr.s6_addr[1] & 0xC0U) == 0x80U) { // fe80::/10
                return true;
            }
            return false;
        }

        bool hostResolvesToLocal(const std::string& host) {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG;
            addrinfo* result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0 || result == nullptr) {
                return false;
            }

            bool foundPrivate = false;
            for (addrinfo* cursor = result; cursor != nullptr; cursor = cursor->ai_next) {
                if (cursor->ai_family == AF_INET) {
                    const auto* in = reinterpret_cast<sockaddr_in*>(cursor->ai_addr);
                    if (!isPrivateIpv4(in->sin_addr)) {
                        ::freeaddrinfo(result);
                        return false;
                    }
                    foundPrivate = true;
                } else if (cursor->ai_family == AF_INET6) {
                    const auto* in6 = reinterpret_cast<sockaddr_in6*>(cursor->ai_addr);
                    if (!isPrivateIpv6(in6->sin6_addr)) {
                        ::freeaddrinfo(result);
                        return false;
                    }
                    foundPrivate = true;
                }
            }

            ::freeaddrinfo(result);
            return foundPrivate;
        }

        bool isLocalOrPrivateHost(const std::string& host, bool allowPublicAddr) {
            if (allowPublicAddr) {
                return true;
            }
            const std::string lowered = toLower(host);
            if (lowered == "localhost") {
                return true;
            }

            in_addr in4{};
            if (::inet_pton(AF_INET, host.c_str(), &in4) == 1) {
                return isPrivateIpv4(in4);
            }

            in6_addr in6{};
            if (::inet_pton(AF_INET6, host.c_str(), &in6) == 1) {
                return isPrivateIpv6(in6);
            }

            return hostResolvesToLocal(host);
        }

        std::string normalizeHostIdentity(std::string value) {
            value = toLower(trim(value));
            while (!value.empty() && value.back() == '.') {
                value.pop_back();
            }
            return value;
        }

        std::vector<std::string> resolveHostAddresses(const std::string& host) {
            std::vector<std::string> addresses;
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_ADDRCONFIG;
            addrinfo* result = nullptr;
            const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
            if (rc != 0 || result == nullptr) {
                return addresses;
            }

            std::set<std::string> unique;
            char buffer[INET6_ADDRSTRLEN]{};
            for (addrinfo* cursor = result; cursor != nullptr; cursor = cursor->ai_next) {
                const void* source = nullptr;
                if (cursor->ai_family == AF_INET) {
                    source = &reinterpret_cast<const sockaddr_in*>(cursor->ai_addr)->sin_addr;
                } else if (cursor->ai_family == AF_INET6) {
                    source = &reinterpret_cast<const sockaddr_in6*>(cursor->ai_addr)->sin6_addr;
                }
                if (!source) {
                    continue;
                }
                if (::inet_ntop(cursor->ai_family, source, buffer, sizeof(buffer)) != nullptr) {
                    const std::string normalized = normalizeHostIdentity(buffer);
                    if (!normalized.empty() && unique.insert(normalized).second) {
                        addresses.push_back(normalized);
                    }
                }
            }

            ::freeaddrinfo(result);
            return addresses;
        }

        LocalHostIdentity collectLocalHostIdentity() {
            LocalHostIdentity identity;
            identity.aliases.insert("localhost");
            identity.addresses.insert("127.0.0.1");
            identity.addresses.insert("::1");

            std::array<char, 256> hostnameBuf{};
            if (::gethostname(hostnameBuf.data(), hostnameBuf.size()) == 0) {
                hostnameBuf.back() = '\0';
                const std::string hostname = normalizeHostIdentity(hostnameBuf.data());
                if (!hostname.empty()) {
                    identity.aliases.insert(hostname);
                    const auto dot = hostname.find('.');
                    if (dot != std::string::npos) {
                        identity.aliases.insert(hostname.substr(0, dot));
                    }
                    for (const auto& address : resolveHostAddresses(hostname)) {
                        identity.addresses.insert(address);
                    }
                }
            }

            if (const char* envHostname = std::getenv("HOSTNAME")) {
                const std::string hostname = normalizeHostIdentity(envHostname);
                if (!hostname.empty()) {
                    identity.aliases.insert(hostname);
                }
            }

            ifaddrs* interfaces = nullptr;
            if (::getifaddrs(&interfaces) == 0 && interfaces != nullptr) {
                char buffer[INET6_ADDRSTRLEN]{};
                for (ifaddrs* cursor = interfaces; cursor != nullptr; cursor = cursor->ifa_next) {
                    if (!cursor->ifa_addr) {
                        continue;
                    }
                    const void* source = nullptr;
                    if (cursor->ifa_addr->sa_family == AF_INET) {
                        source = &reinterpret_cast<const sockaddr_in*>(cursor->ifa_addr)->sin_addr;
                    } else if (cursor->ifa_addr->sa_family == AF_INET6) {
                        source = &reinterpret_cast<const sockaddr_in6*>(cursor->ifa_addr)->sin6_addr;
                    }
                    if (!source) {
                        continue;
                    }
                    if (::inet_ntop(cursor->ifa_addr->sa_family, source, buffer, sizeof(buffer)) != nullptr) {
                        const std::string address = normalizeHostIdentity(buffer);
                        if (!address.empty()) {
                            identity.addresses.insert(address);
                        }
                    }
                }
                ::freeifaddrs(interfaces);
            }

            return identity;
        }

        HostMatchResult classifyRecruitHost(const std::string& host, const LocalHostIdentity& identity) {
            HostMatchResult result;
            result.details = {
                    {"requested_host", host},
                    {"resolved_addresses", nlohmann::json::array()}
            };

            const std::string normalizedHost = normalizeHostIdentity(host);
            if (normalizedHost.empty()) {
                result.details["matched_by"] = "invalid";
                return result;
            }

            if (identity.aliases.find(normalizedHost) != identity.aliases.end()) {
                result.sameHardware = true;
                result.details["matched_by"] = "hostname";
                return result;
            }

            if (identity.addresses.find(normalizedHost) != identity.addresses.end()) {
                result.sameHardware = true;
                result.details["matched_by"] = "address";
                return result;
            }

            const auto resolved = resolveHostAddresses(host);
            for (const auto& address : resolved) {
                result.details["resolved_addresses"].push_back(address);
            }
            if (!resolved.empty()) {
                const bool allLocal = std::all_of(resolved.begin(), resolved.end(), [&](const std::string& address) {
                    return identity.addresses.find(address) != identity.addresses.end();
                });
                if (allLocal) {
                    result.sameHardware = true;
                    result.details["matched_by"] = "dns";
                    return result;
                }
            }

            result.details["matched_by"] = "remote";
            return result;
        }

        bool readProcMemInfo(uint64_t& totalBytes, uint64_t& availableBytes) {
            std::ifstream input("/proc/meminfo");
            if (!input.is_open()) {
                return false;
            }

            uint64_t memTotalKb = 0;
            uint64_t memAvailableKb = 0;
            uint64_t memFreeKb = 0;
            uint64_t buffersKb = 0;
            uint64_t cachedKb = 0;
            std::string key;
            uint64_t value = 0;
            std::string unit;
            while (input >> key >> value >> unit) {
                if (key == "MemTotal:") {
                    memTotalKb = value;
                } else if (key == "MemAvailable:") {
                    memAvailableKb = value;
                } else if (key == "MemFree:") {
                    memFreeKb = value;
                } else if (key == "Buffers:") {
                    buffersKb = value;
                } else if (key == "Cached:") {
                    cachedKb = value;
                }
            }

            if (memTotalKb == 0) {
                return false;
            }
            if (memAvailableKb == 0) {
                memAvailableKb = memFreeKb + buffersKb + cachedKb;
            }
            totalBytes = memTotalKb * 1024ULL;
            availableBytes = memAvailableKb * 1024ULL;
            return availableBytes <= totalBytes;
        }

        SystemCapacitySnapshot collectSystemCapacitySnapshot() {
            SystemCapacitySnapshot snapshot;
            snapshot.cpuCores = std::max(1u, std::thread::hardware_concurrency());

            double load[3]{0.0, 0.0, 0.0};
            if (::getloadavg(load, 1) == 1) {
                snapshot.load1 = load[0];
                snapshot.loadPerCpu = snapshot.load1 / static_cast<double>(snapshot.cpuCores);
                snapshot.loadAvailable = true;
            }

            snapshot.memoryAvailable = readProcMemInfo(
                    snapshot.totalMemoryBytes,
                    snapshot.availableMemoryBytes
            );
            if (snapshot.memoryAvailable && snapshot.totalMemoryBytes > 0) {
                snapshot.memoryUsedRatio =
                        1.0 - (static_cast<double>(snapshot.availableMemoryBytes) /
                               static_cast<double>(snapshot.totalMemoryBytes));
            }

            return snapshot;
        }

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

        nlohmann::json findClusterByIdentifier(const std::vector<nlohmann::json>& clusters,
                                               const std::string& identifier) {
            const std::string needle = toLower(trim(identifier));
            if (needle.empty()) {
                return nullptr;
            }

            for (const auto& cluster : clusters) {
                const std::string name = toLower(firstStringValue(cluster, {"name"}));
                const std::string id = toLower(firstStringValue(cluster, {"id", "cluster_id", "uuid"}));
                if ((!name.empty() && name == needle) || (!id.empty() && id == needle)) {
                    return cluster;
                }
            }
            return nullptr;
        }

        nlohmann::json buildClusterDetailsWithNetworking(const nlohmann::json& selectedCluster) {
            std::vector<std::string> endpointCandidates;
            std::vector<std::string> ipsInUse;
            std::vector<int> portsInUse;

            const auto captureEndpoint = [&](const std::string& value) {
                const std::string endpoint = trim(value);
                if (endpoint.empty()) {
                    return;
                }
                addUniqueString(endpointCandidates, endpoint);
                TraceyEndpoint parsed{};
                if (parseTraceyEndpoint(endpoint, parsed)) {
                    addUniqueString(ipsInUse, parsed.host);
                    addUniqueInt(portsInUse, parsed.port);
                }
            };

            captureEndpoint(firstStringValue(selectedCluster, {"endpoint", "api", "api_url", "url", "apiServer", "api_server"}));
            captureEndpoint(firstStringValue(selectedCluster, {"console", "console_url", "consoleUrl"}));

            if (selectedCluster.contains("networking") && selectedCluster["networking"].is_object()) {
                const auto& networking = selectedCluster["networking"];
                captureEndpoint(firstStringValue(networking, {"endpoint", "api", "api_url", "url", "apiServer", "api_server"}));
                captureEndpoint(firstStringValue(networking, {"console", "console_url", "consoleUrl"}));

                if (networking.contains("ips_in_use") && networking["ips_in_use"].is_array()) {
                    for (const auto& ip : networking["ips_in_use"]) {
                        if (ip.is_string()) {
                            addUniqueString(ipsInUse, ip.get<std::string>());
                        }
                    }
                }
                if (networking.contains("ports_in_use") && networking["ports_in_use"].is_array()) {
                    for (const auto& port : networking["ports_in_use"]) {
                        if (port.is_number_integer()) {
                            addUniqueInt(portsInUse, port.get<int>());
                        }
                    }
                }
            }

            addUniqueInt(portsInUse, firstPositiveIntValue(selectedCluster, {"port", "api_port", "apiPort", "service_port"}));
            if (selectedCluster.contains("networking") && selectedCluster["networking"].is_object()) {
                addUniqueInt(portsInUse, firstPositiveIntValue(selectedCluster["networking"], {"port", "api_port", "apiPort", "service_port"}));
            }

            std::sort(endpointCandidates.begin(), endpointCandidates.end());
            std::sort(ipsInUse.begin(), ipsInUse.end());
            std::sort(portsInUse.begin(), portsInUse.end());

            nlohmann::json details = selectedCluster;
            nlohmann::json networkingPayload = nlohmann::json::object();
            if (details.contains("networking") && details["networking"].is_object()) {
                networkingPayload = details["networking"];
            }
            networkingPayload["endpoint"] = firstStringValue(details, {"endpoint", "api", "api_url", "url", "apiServer", "api_server"});
            networkingPayload["endpoint_candidates"] = endpointCandidates;
            networkingPayload["ips_in_use"] = ipsInUse;
            networkingPayload["ports_in_use"] = portsInUse;
            details["networking"] = networkingPayload;
            return details;
        }

        std::string buildTraceyStatusPath(const TraceyEndpoint& endpoint) {
            if (endpoint.basePath.empty()) {
                return "/status";
            }
            return endpoint.basePath + "/status";
        }

        std::string buildTraceyPath(const TraceyEndpoint& endpoint, const std::string& suffix) {
            const std::string normalizedSuffix = suffix.empty() || suffix.front() == '/'
                                                 ? suffix
                                                 : ("/" + suffix);
            if (endpoint.basePath.empty()) {
                return normalizedSuffix.empty() ? "/" : normalizedSuffix;
            }
            return endpoint.basePath + normalizedSuffix;
        }

        std::string getenvTrimmed(const char* key) {
            if (!key) {
                return "";
            }
            const char* raw = std::getenv(key);
            return raw ? trim(raw) : "";
        }

        std::string getenvTrimmedOr(const char* primary, const char* fallback, const std::string& defaultValue = "") {
            const std::string primaryValue = getenvTrimmed(primary);
            if (!primaryValue.empty()) {
                return primaryValue;
            }
            const std::string fallbackValue = getenvTrimmed(fallback);
            if (!fallbackValue.empty()) {
                return fallbackValue;
            }
            return defaultValue;
        }

        std::string normalizeHttpBaseUrl(std::string value) {
            value = trim(value);
            while (!value.empty() && value.back() == '/') {
                value.pop_back();
            }
            return value;
        }

        bool startsWithHttpScheme(const std::string& value) {
            return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
        }

        bool resolveOptionalRecruitGailEnvironment(std::string& baseUrlOut,
                                                   std::string& apiTokenOut,
                                                   std::string& errorOut) {
            baseUrlOut = normalizeHttpBaseUrl(getenvTrimmedOr("NMC_GAIL_BASE_URL", "GAIL_BASE_URL"));
            if (baseUrlOut.empty()) {
                baseUrlOut = normalizeHttpBaseUrl(getenvTrimmedOr("NMC_GAIL_URL", "GAIL_URL"));
            }

            apiTokenOut.clear();
            static constexpr std::array<const char*, 6> tokenKeys = {
                    "NMC_GAIL_API_TOKEN",
                    "GAIL_API_TOKEN",
                    "NMC_GAIL_BEARER_TOKEN",
                    "GAIL_BEARER_TOKEN",
                    "NMC_GAIL_AUTH_TOKEN",
                    "GAIL_AUTH_TOKEN"
            };
            for (const char* tokenKey : tokenKeys) {
                apiTokenOut = getenvTrimmed(tokenKey);
                if (!apiTokenOut.empty()) {
                    break;
                }
            }

            errorOut.clear();
            if (baseUrlOut.empty()) {
                return true;
            }
            if (!startsWithHttpScheme(baseUrlOut)) {
                errorOut = "NMC_GAIL_BASE_URL must begin with http:// or https://.";
                return false;
            }
            return true;
        }

        std::string toUpper(std::string value) {
            for (auto& ch : value) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            return value;
        }

        bool looksLikeTextPayload(const std::string& payload) {
            if (payload.empty()) {
                return true;
            }
            size_t controlChars = 0;
            for (const unsigned char ch : payload) {
                if (ch == '\n' || ch == '\r' || ch == '\t') {
                    continue;
                }
                if (ch < 0x20 || ch == 0x7f) {
                    ++controlChars;
                }
            }
            return controlChars == 0;
        }

        std::string base64Encode(const std::string& input) {
            static const char table[] =
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string output;
            output.reserve(((input.size() + 2) / 3) * 4);

            int val = 0;
            int valb = -6;
            for (const unsigned char ch : input) {
                val = (val << 8) + ch;
                valb += 8;
                while (valb >= 0) {
                    output.push_back(table[(val >> valb) & 0x3F]);
                    valb -= 6;
                }
            }
            if (valb > -6) {
                output.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
            }
            while (output.size() % 4 != 0) {
                output.push_back('=');
            }
            return output;
        }

        std::optional<nlohmann::json> findK8sItemByNameOrComponent(
                const nlohmann::json& listPayload,
                const std::string& exactName,
                const std::string& appName,
                const std::string& component
        ) {
            if (!listPayload.is_object() || !listPayload.contains("items") || !listPayload["items"].is_array()) {
                return std::nullopt;
            }

            std::optional<nlohmann::json> componentMatch;
            for (const auto& item : listPayload["items"]) {
                if (!item.is_object()) {
                    continue;
                }
                const auto metadataIt = item.find("metadata");
                if (metadataIt == item.end() || !metadataIt->is_object()) {
                    continue;
                }
                const auto& metadata = *metadataIt;
                const std::string name = metadata.value("name", "");
                if (!exactName.empty() && name == exactName) {
                    return item;
                }
                const auto labelsIt = metadata.find("labels");
                if (labelsIt == metadata.end() || !labelsIt->is_object()) {
                    continue;
                }
                const auto& labels = *labelsIt;
                const std::string labeledApp = labels.value("app.kubernetes.io/name", labels.value("app", ""));
                const std::string labeledComponent = labels.value("app.kubernetes.io/component", "");
                if (!component.empty()
                    && labeledComponent == component
                    && (appName.empty() || labeledApp.empty() || labeledApp == appName)) {
                    componentMatch = item;
                }
            }
            return componentMatch;
        }

        int extractServicePortValue(
                const nlohmann::json& servicePayload,
                const std::string& preferredPortName,
                int fallbackPort
        ) {
            const auto specIt = servicePayload.find("spec");
            if (specIt == servicePayload.end() || !specIt->is_object()) {
                return fallbackPort;
            }
            const auto portsIt = specIt->find("ports");
            if (portsIt == specIt->end() || !portsIt->is_array()) {
                return fallbackPort;
            }

            int firstPort = -1;
            for (const auto& portNode : *portsIt) {
                if (!portNode.is_object()) {
                    continue;
                }
                const int portValue = portNode.value("port", -1);
                if (portValue > 0 && firstPort <= 0) {
                    firstPort = portValue;
                }
                if (!preferredPortName.empty() && portNode.value("name", "") == preferredPortName && portValue > 0) {
                    return portValue;
                }
            }
            return firstPort > 0 ? firstPort : fallbackPort;
        }

        nlohmann::json collectServicePorts(const nlohmann::json& servicePayload) {
            nlohmann::json ports = nlohmann::json::array();
            const auto specIt = servicePayload.find("spec");
            if (specIt == servicePayload.end() || !specIt->is_object()) {
                return ports;
            }
            const auto portsIt = specIt->find("ports");
            if (portsIt == specIt->end() || !portsIt->is_array()) {
                return ports;
            }
            for (const auto& portNode : *portsIt) {
                if (!portNode.is_object()) {
                    continue;
                }
                ports.push_back({
                        {"name", portNode.value("name", "")},
                        {"port", portNode.value("port", 0)},
                        {"target_port", portNode.contains("targetPort") ? portNode["targetPort"] : nlohmann::json(nullptr)},
                        {"protocol", portNode.value("protocol", "TCP")}
                });
            }
            return ports;
        }

        nlohmann::json buildAarnnServiceSummary(
                const nlohmann::json& servicePayload,
                const std::string& component,
                const std::string& scheme,
                const std::string& urlField,
                const std::string& preferredPortName,
                int fallbackPort
        ) {
            nlohmann::json summary = {
                    {"found", false},
                    {"component", component},
                    {"source", "kubernetes"}
            };
            if (!servicePayload.is_object()) {
                return summary;
            }

            const nlohmann::json metadata = servicePayload.value("metadata", nlohmann::json::object());
            const nlohmann::json spec = servicePayload.value("spec", nlohmann::json::object());
            const std::string namespaceName = metadata.value("namespace", "");
            const std::string serviceName = metadata.value("name", "");
            const std::string dnsName = !namespaceName.empty() && !serviceName.empty()
                                        ? serviceName + "." + namespaceName + ".svc.cluster.local"
                                        : "";
            const std::string clusterIp = trim(spec.value("clusterIP", ""));
            const std::string host = (!clusterIp.empty() && clusterIp != "None") ? clusterIp : dnsName;
            const int portValue = extractServicePortValue(servicePayload, preferredPortName, fallbackPort);

            summary["found"] = !host.empty() && portValue > 0;
            summary["namespace"] = namespaceName;
            summary["service_name"] = serviceName;
            summary["cluster_ip"] = clusterIp.empty() ? nlohmann::json(nullptr) : nlohmann::json(clusterIp);
            summary["dns_name"] = dnsName.empty() ? nlohmann::json(nullptr) : nlohmann::json(dnsName);
            summary["host"] = host.empty() ? nlohmann::json(nullptr) : nlohmann::json(host);
            summary["port"] = portValue;
            summary["service"] = {
                    {"observed", true},
                    {"type", spec.value("type", "ClusterIP")},
                    {"ports", collectServicePorts(servicePayload)}
            };
            if (summary["found"].get<bool>()) {
                summary[urlField] = scheme + "://" + host + ":" + std::to_string(portValue);
            }
            return summary;
        }

        nlohmann::json buildAarnnDeploymentSummary(
                const nlohmann::json& deploymentPayload,
                const std::string& fallbackNamespace,
                const std::string& fallbackDeployment
        ) {
            int desiredReplicas = 0;
            int readyReplicas = 0;
            int availableReplicas = 0;
            std::string namespaceName = fallbackNamespace;
            std::string deploymentName = fallbackDeployment;

            if (deploymentPayload.contains("metadata") && deploymentPayload["metadata"].is_object()) {
                const auto& metadata = deploymentPayload["metadata"];
                namespaceName = metadata.value("namespace", namespaceName);
                deploymentName = metadata.value("name", deploymentName);
            }
            if (deploymentPayload.contains("spec") && deploymentPayload["spec"].is_object()) {
                desiredReplicas = deploymentPayload["spec"].value("replicas", 0);
            }
            if (deploymentPayload.contains("status") && deploymentPayload["status"].is_object()) {
                readyReplicas = deploymentPayload["status"].value("readyReplicas", 0);
                availableReplicas = deploymentPayload["status"].value("availableReplicas", 0);
            }

            bool healthy = false;
            std::string status = "Pending";
            if (desiredReplicas <= 0) {
                status = "Suspended";
                healthy = true;
            } else if (readyReplicas >= desiredReplicas && availableReplicas >= desiredReplicas) {
                status = "Running";
                healthy = true;
            } else if (readyReplicas > 0 || availableReplicas > 0) {
                status = "Degraded";
            }

            return {
                    {"observed", true},
                    {"healthy", healthy},
                    {"status", status},
                    {"namespace", namespaceName},
                    {"deployment", deploymentName},
                    {"desired_replicas", desiredReplicas},
                    {"ready_replicas", readyReplicas},
                    {"available_replicas", availableReplicas}
            };
        }

        std::string encodeQueryComponent(const std::string& value) {
            std::ostringstream encoded;
            encoded.fill('0');
            encoded << std::hex << std::uppercase;
            for (const unsigned char ch : value) {
                if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                    encoded << static_cast<char>(ch);
                } else {
                    encoded << '%' << std::setw(2) << static_cast<int>(ch);
                }
            }
            return encoded.str();
        }

        void appendEncodedQueryParameter(std::string& path, const std::string& key, const std::string& value) {
            if (trim(value).empty()) {
                return;
            }
            path += (path.find('?') == std::string::npos) ? "?" : "&";
            path += encodeQueryComponent(key) + "=" + encodeQueryComponent(value);
        }

        bool pathHasQueryParameter(const std::string& path, const std::string& key) {
            const auto queryPos = path.find('?');
            if (queryPos == std::string::npos || queryPos + 1 >= path.size()) {
                return false;
            }
            const std::string query = path.substr(queryPos + 1);
            std::stringstream stream(query);
            std::string item;
            while (std::getline(stream, item, '&')) {
                const auto equalsPos = item.find('=');
                const std::string candidate = equalsPos == std::string::npos ? item : item.substr(0, equalsPos);
                if (candidate == key) {
                    return true;
                }
            }
            return false;
        }

        std::string sanitizeAarnnIdSegment(const std::string& value) {
            std::string sanitized;
            sanitized.reserve(value.size());
            for (const unsigned char ch : value) {
                if (std::isalnum(ch)) {
                    sanitized.push_back(static_cast<char>(std::tolower(ch)));
                } else if (!sanitized.empty() && sanitized.back() != '-') {
                    sanitized.push_back('-');
                }
            }
            while (!sanitized.empty() && sanitized.back() == '-') {
                sanitized.pop_back();
            }
            return sanitized.empty() ? "unknown" : sanitized;
        }

        std::string buildAarnnSyntheticId(const std::string& prefix, const std::string& host, int port) {
            return prefix + "-" + sanitizeAarnnIdSegment(host) + "-" + std::to_string(std::max(port, 0));
        }

        std::string normalizedUrlWithHost(const TraceyEndpoint& endpoint, const std::string& host) {
            const bool hostIsIpv6 = host.find(':') != std::string::npos;
            std::string normalized = endpoint.https ? "https://" : "http://";
            normalized += hostIsIpv6 ? "[" + host + "]" : host;
            normalized += ":" + std::to_string(endpoint.port);
            normalized += endpoint.basePath;
            return normalized;
        }

        std::string deriveLinkSecurity(bool signaturePresent, const TraceyEndpoint* endpoint, bool tlsVerify) {
            std::string linkSecurity;
            if (endpoint == nullptr) {
                linkSecurity = "announcement-only";
            } else if (endpoint->https) {
                linkSecurity = tlsVerify ? "tls-verified" : "tls-unverified";
            } else {
                linkSecurity = "plaintext";
            }
            if (signaturePresent) {
                linkSecurity += "+signed-announcement";
            } else {
                linkSecurity += "+unsigned-announcement";
            }
            return linkSecurity;
        }

        std::string mergeTraceySource(const std::string& current, const std::string& incoming) {
            std::set<std::string> tokens;
            auto collect = [&tokens](const std::string& value) {
                if (value.empty()) {
                    return;
                }
                std::stringstream ss(value);
                std::string token;
                while (std::getline(ss, token, '+')) {
                    const std::string normalized = trim(token);
                    if (!normalized.empty()) {
                        tokens.insert(normalized);
                    }
                }
            };

            collect(current);
            collect(incoming);
            if (tokens.empty()) {
                return "";
            }

            const std::vector<std::string> orderedKnown = {
                    "heartbeat",
                    "discovery",
                    "continuum-provisioning"
            };

            std::vector<std::string> ordered;
            for (const auto& known : orderedKnown) {
                auto it = tokens.find(known);
                if (it != tokens.end()) {
                    ordered.push_back(*it);
                    tokens.erase(it);
                }
            }
            for (const auto& extra : tokens) {
                ordered.push_back(extra);
            }

            std::ostringstream out;
            for (size_t i = 0; i < ordered.size(); ++i) {
                if (i > 0) {
                    out << "+";
                }
                out << ordered[i];
            }
            return out.str();
        }

        int64_t computeBackoffMs(int failures, int64_t baseMs, int64_t maxMs) {
            int64_t value = std::max<int64_t>(baseMs, 1000);
            for (int i = 1; i < failures; ++i) {
                if (value >= maxMs) {
                    break;
                }
                if (value > maxMs / 2) {
                    value = maxMs;
                    break;
                }
                value *= 2;
            }
            return std::clamp(value, std::max<int64_t>(baseMs, 1000), std::max<int64_t>(maxMs, baseMs));
        }

        int64_t parseQueryInt64(const httplib::Request& req,
                                const char* key,
                                int64_t fallback,
                                int64_t minValue,
                                int64_t maxValue) {
            if (!req.has_param(key)) {
                return fallback;
            }
            const std::string raw = trim(req.get_param_value(key));
            if (raw.empty()) {
                return fallback;
            }
            try {
                int64_t value = std::stoll(raw);
                return std::clamp(value, minValue, maxValue);
            } catch (const std::exception&) {
                return fallback;
            }
        }

        std::string normalizeLogLevel(std::string level) {
            level = toLower(trim(level));
            if (level == "warning") {
                return "warn";
            }
            if (level != "info" && level != "warn" && level != "error") {
                return "info";
            }
            return level;
        }

        struct ExecResult {
            int exitCode{1};
            std::string output;
        };

        std::string shellQuote(const std::string& value) {
            std::string quoted = "'";
            for (const char ch : value) {
                if (ch == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted.push_back(ch);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        std::string commandToShellString(const std::vector<std::string>& args) {
            std::ostringstream command;
            bool first = true;
            for (const auto& arg : args) {
                if (!first) {
                    command << ' ';
                }
                command << shellQuote(arg);
                first = false;
            }
            return command.str();
        }

        ExecResult runShellCommand(const std::vector<std::string>& args, size_t outputLimitBytes = 262144) {
            ExecResult result;
            if (args.empty()) {
                result.output = "No command specified.";
                return result;
            }

            std::string command = commandToShellString(args) + " 2>&1";
            FILE* pipe = ::popen(command.c_str(), "r");
            if (!pipe) {
                result.output = "Unable to execute command.";
                return result;
            }

            std::array<char, 4096> buffer{};
            bool outputTruncated = false;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
                if (result.output.size() < outputLimitBytes) {
                    const size_t remaining = outputLimitBytes - result.output.size();
                    const std::string chunk(buffer.data());
                    result.output.append(chunk, 0, std::min(chunk.size(), remaining));
                    if (chunk.size() > remaining) {
                        outputTruncated = true;
                    }
                }
            }

            const int status = ::pclose(pipe);
            if (status != -1 && WIFEXITED(status)) {
                result.exitCode = WEXITSTATUS(status);
            }

            if (outputTruncated) {
                result.output += "\n...(output truncated)\n";
            }
            return result;
        }

        bool hasControlChars(const std::string& value) {
            return std::any_of(value.begin(), value.end(), [](const unsigned char ch) {
                return std::iscntrl(ch) != 0;
            });
        }

        bool isSafeHost(const std::string& host) {
            if (host.empty() || host.size() > 253 || host.front() == '-' || host.back() == '-') {
                return false;
            }
            return std::all_of(host.begin(), host.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '.' || ch == '-';
            });
        }

        bool isSafeUser(const std::string& user) {
            if (user.empty() || user.size() > 64) {
                return false;
            }
            const unsigned char first = static_cast<unsigned char>(user.front());
            if (std::isalpha(first) == 0 && first != '_') {
                return false;
            }
            return std::all_of(user.begin() + 1, user.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
            });
        }

        bool isSafeLocalPath(const std::string& path) {
            if (path.empty() || path.size() > 4096 || hasControlChars(path)) {
                return false;
            }
            return path.front() == '/';
        }

        bool isSafeRemotePath(const std::string& path) {
            if (path.empty() || path.size() > 4096 || hasControlChars(path) || path.front() != '/') {
                return false;
            }
            return std::all_of(path.begin(), path.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '/' || ch == '-' || ch == '_' || ch == '.';
            });
        }

        bool isKnownNodeType(const std::string& nodeType) {
            static const std::set<std::string> allowedTypes = {
                    "bare-metal",
                    "app-install",
                    "vm",
                    "virtual-machine",
                    "podman",
                    "kubernetes",
                    "openstack"
            };
            return allowedTypes.find(nodeType) != allowedTypes.end();
        }

        std::string normalizeNodeType(const std::string& rawNodeType) {
            std::string nodeType = toLower(trim(rawNodeType));
            if (nodeType.empty()) {
                nodeType = "bare-metal";
            }
            if (nodeType == "virtual-machine") {
                nodeType = "vm";
            }
            return nodeType;
        }

        bool readTextFile(const std::string& path, size_t maxBytes, std::string& contentOut, std::string& errorOut) {
            contentOut.clear();
            errorOut.clear();

            try {
                const std::filesystem::path fsPath(path);
                if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
                    errorOut = "File does not exist: " + path;
                    return false;
                }
                const uintmax_t size = std::filesystem::file_size(fsPath);
                if (size > maxBytes) {
                    errorOut = "File exceeds size limit (" + std::to_string(maxBytes) + " bytes): " + path;
                    return false;
                }
            } catch (const std::exception& e) {
                errorOut = "Unable to inspect file '" + path + "': " + std::string(e.what());
                return false;
            }

            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                errorOut = "Unable to open file: " + path;
                return false;
            }

            std::ostringstream oss;
            oss << input.rdbuf();
            contentOut = oss.str();
            return true;
        }

        bool writeTempFile(const std::string& content, std::string& pathOut, std::string& errorOut) {
            pathOut.clear();
            errorOut.clear();

            char tmpName[] = "/tmp/nmc-recruit-XXXXXX";
            const int fd = ::mkstemp(tmpName);
            if (fd < 0) {
                errorOut = "Failed to create temporary file.";
                return false;
            }
            ::close(fd);

            try {
                std::ofstream output(tmpName, std::ios::binary | std::ios::trunc);
                if (!output.is_open()) {
                    errorOut = "Failed to open temporary file for writing.";
                    ::unlink(tmpName);
                    return false;
                }
                output << content;
                output.close();
                std::filesystem::permissions(
                        tmpName,
                        std::filesystem::perms::owner_read |
                        std::filesystem::perms::owner_write |
                        std::filesystem::perms::owner_exec,
                        std::filesystem::perm_options::replace
                );
                pathOut = tmpName;
                return true;
            } catch (const std::exception& e) {
                errorOut = "Failed to write temporary file: " + std::string(e.what());
                ::unlink(tmpName);
                return false;
            }
        }

        std::string buildDefaultRecruitScript() {
            return R"(#!/usr/bin/env bash
set -euo pipefail
umask 077

export DEBIAN_FRONTEND=noninteractive
if command -v apt-get >/dev/null 2>&1; then
  apt-get update -y
  apt-get install -y --allow-downgrades curl ca-certificates jq
fi

mkdir -p /opt/continuum/node
cat >/opt/continuum/node/recruitment.env <<EOF
NMC_NODE_TYPE=${NMC_NODE_TYPE:-bare-metal}
NMC_NODE_REGION=${NMC_NODE_REGION:-}
NMC_NODE_NAME=${NMC_NODE_NAME:-}
NMC_CONTINUUM_URL=${NMC_CONTINUUM_URL:-}
NMC_CONTINUUM_AUTH_TOKEN=${NMC_CONTINUUM_AUTH_TOKEN:-}
NMC_GAIL_BASE_URL=${NMC_GAIL_BASE_URL:-}
NMC_GAIL_API_TOKEN=${NMC_GAIL_API_TOKEN:-}
TRACEY_AGENT_ID=${TRACEY_AGENT_ID:-}
TRACEY_STATUS_ADDR=${TRACEY_STATUS_ADDR:-}
RECRUITED_AT_UTC=$(date -u +%FT%TZ)
EOF

case "${NMC_NODE_TYPE:-bare-metal}" in
  podman)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y --allow-downgrades podman
    fi
    ;;
  kubernetes)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y apt-transport-https containerd
    fi
    ;;
  openstack)
    if command -v apt-get >/dev/null 2>&1; then
      apt-get install -y python3-openstackclient
    fi
    ;;
  vm|app-install|bare-metal)
    true
    ;;
esac

echo "ready" >/opt/continuum/node/state
echo "Continuum recruitment completed for ${NMC_NODE_NAME:-unknown-node} (${NMC_NODE_TYPE:-bare-metal})."
)";
        }

        int parsePortOrDefault(const nlohmann::json& payload, int fallback) {
            if (!payload.contains("port")) {
                return fallback;
            }
            try {
                if (payload["port"].is_number_integer()) {
                    return payload["port"].get<int>();
                }
                if (payload["port"].is_string()) {
                    return std::stoi(payload["port"].get<std::string>());
                }
            } catch (const std::exception&) {
                return -1;
            }
            return -1;
        }

        std::vector<std::string> parseBinaryArgs(const nlohmann::json& payload, std::string& errorOut) {
            std::vector<std::string> args;
            errorOut.clear();

            if (payload.contains("binary_args")) {
                const auto& value = payload["binary_args"];
                if (value.is_array()) {
                    for (const auto& item : value) {
                        if (!item.is_string()) {
                            errorOut = "binary_args entries must be strings.";
                            return {};
                        }
                        args.push_back(item.get<std::string>());
                    }
                } else if (value.is_string()) {
                    args.push_back(value.get<std::string>());
                } else {
                    errorOut = "binary_args must be a string or array of strings.";
                    return {};
                }
            }

            if (payload.contains("binary_arg")) {
                if (!payload["binary_arg"].is_string()) {
                    errorOut = "binary_arg must be a string.";
                    return {};
                }
                args.push_back(payload["binary_arg"].get<std::string>());
            }
            return args;
        }

        std::string normalizeCapability(std::string capability) {
            capability = toLower(trim(capability));
            if (capability == "app" || capability == "app-install") {
                return "apps";
            }
            if (capability == "k8s") {
                return "kubernetes";
            }
            if (capability == "virtual-machine") {
                return "vm";
            }
            if (capability == "os") {
                return "openstack";
            }
            return capability;
        }

        bool isKnownCapability(const std::string& capability) {
            static const std::set<std::string> allowed = {
                    "apps",
                    "vm",
                    "podman",
                    "kubernetes",
                    "openstack"
            };
            return allowed.find(capability) != allowed.end();
        }

        void appendUnique(std::vector<std::string>& values, const std::string& value) {
            if (value.empty()) {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }

        bool parseCapabilitiesField(const nlohmann::json& payload,
                                    const std::string& fieldName,
                                    std::vector<std::string>& capabilities,
                                    std::string& errorOut) {
            if (!payload.contains(fieldName)) {
                return true;
            }
            const auto& value = payload[fieldName];
            if (value.is_string()) {
                std::stringstream ss(value.get<std::string>());
                std::string token;
                while (std::getline(ss, token, ',')) {
                    const std::string normalized = normalizeCapability(token);
                    if (normalized.empty()) {
                        continue;
                    }
                    if (!isKnownCapability(normalized)) {
                        errorOut = "Unsupported capability '" + token + "'.";
                        return false;
                    }
                    appendUnique(capabilities, normalized);
                }
                return true;
            }
            if (value.is_array()) {
                for (const auto& item : value) {
                    if (!item.is_string()) {
                        errorOut = fieldName + " entries must be strings.";
                        return false;
                    }
                    const std::string normalized = normalizeCapability(item.get<std::string>());
                    if (normalized.empty()) {
                        continue;
                    }
                    if (!isKnownCapability(normalized)) {
                        errorOut = "Unsupported capability '" + item.get<std::string>() + "'.";
                        return false;
                    }
                    appendUnique(capabilities, normalized);
                }
                return true;
            }
            errorOut = fieldName + " must be a string or array of strings.";
            return false;
        }

        std::vector<std::string> defaultCapabilitiesForNodeType(const std::string& nodeType) {
            if (nodeType == "kubernetes") {
                return {"kubernetes"};
            }
            if (nodeType == "podman") {
                return {"podman"};
            }
            if (nodeType == "vm") {
                return {"vm"};
            }
            if (nodeType == "app-install") {
                return {"apps"};
            }
            if (nodeType == "openstack") {
                return {"openstack"};
            }
            return {};
        }

        bool isSafeTenantId(const std::string& tenantId) {
            if (tenantId.empty()) {
                return true;
            }
            if (tenantId.size() > 128) {
                return false;
            }
            return std::all_of(tenantId.begin(), tenantId.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
            });
        }

        bool isSafeAnsibleVarKey(const std::string& key) {
            if (key.empty() || key.size() > 128) {
                return false;
            }
            const unsigned char first = static_cast<unsigned char>(key.front());
            if (std::isalpha(first) == 0 && first != '_') {
                return false;
            }
            return std::all_of(key.begin() + 1, key.end(), [](const unsigned char ch) {
                return std::isalnum(ch) != 0 || ch == '_';
            });
        }

        std::string resolveRecruitPlaybookPath(const std::string& requestedPath) {
            std::vector<std::filesystem::path> candidates;
            if (!requestedPath.empty()) {
                candidates.emplace_back(requestedPath);
            } else {
                const char* fromEnv = std::getenv("NMC_RECRUIT_ANSIBLE_PLAYBOOK");
                if (fromEnv && *fromEnv) {
                    candidates.emplace_back(std::string(fromEnv));
                }
            }

            if (candidates.empty()) {
                try {
                    std::filesystem::path cursor = std::filesystem::current_path();
                    for (int depth = 0; depth < 7; ++depth) {
                        candidates.push_back(cursor / "ansible" / "recruited-node.yml");
                        if (!cursor.has_parent_path()) {
                            break;
                        }
                        cursor = cursor.parent_path();
                    }
                } catch (const std::exception&) {
                    return "";
                }
            }

            for (const auto& candidate : candidates) {
                try {
                    std::filesystem::path absolute = candidate;
                    if (absolute.is_relative()) {
                        absolute = std::filesystem::current_path() / absolute;
                    }
                    absolute = std::filesystem::weakly_canonical(absolute);
                    if (std::filesystem::exists(absolute) && std::filesystem::is_regular_file(absolute)) {
                        return absolute.string();
                    }
                } catch (const std::exception&) {
                    continue;
                }
            }
            return "";
        }
