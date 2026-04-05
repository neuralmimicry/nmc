#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace NMC::Server {

class TraceyCVEIntel {
public:
    TraceyCVEIntel();
    ~TraceyCVEIntel();

    TraceyCVEIntel(const TraceyCVEIntel&) = delete;
    TraceyCVEIntel& operator=(const TraceyCVEIntel&) = delete;

    void start();
    void stop();

    nlohmann::json mirrorStatus() const;
    nlohmann::json buildAgentPlan(const std::string& agentId,
                                  const std::vector<std::string>& orderedAgentIds,
                                  int64_t nowMs);
    nlohmann::json ingestAssessmentReport(const std::string& agentId,
                                          const nlohmann::json& report,
                                          const std::vector<std::string>& orderedAgentIds,
                                          int64_t nowMs);
    nlohmann::json agentProgress(const std::string& agentId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace NMC::Server
