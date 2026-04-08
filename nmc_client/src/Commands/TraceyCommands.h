#ifndef NMC_COMMANDS_TRACEY_COMMANDS_H
#define NMC_COMMANDS_TRACEY_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

// Parent namespace command for Tracey telemetry and control operations.
class TraceyCommand : public BaseCommand {
public:
    TraceyCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Ingest/submit agent heartbeat metadata.
class TraceyHeartbeatCommand : public BaseCommand {
public:
    TraceyHeartbeatCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// List known agents plus compliance summary.
class TraceyAgentsCommand : public BaseCommand {
public:
    TraceyAgentsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Fleet-wide analytics query.
class TraceyAnalyticsCommand : public BaseCommand {
public:
    TraceyAnalyticsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyFleetCommand : public BaseCommand {
public:
    TraceyFleetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyAdaptiveCommand : public BaseCommand {
public:
    TraceyAdaptiveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyCveCommand : public BaseCommand {
public:
    TraceyCveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyAssessmentCommand : public BaseCommand {
public:
    TraceyAssessmentCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyAssessmentPlanCommand : public BaseCommand {
public:
    TraceyAssessmentPlanCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyAssessmentReportCommand : public BaseCommand {
public:
    TraceyAssessmentReportCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyRacksCommand : public BaseCommand {
public:
    TraceyRacksCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyRackCommand : public BaseCommand {
public:
    TraceyRackCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Per-agent analytics query.
class TraceyAnalysisCommand : public BaseCommand {
public:
    TraceyAnalysisCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyServerCommand : public BaseCommand {
public:
    TraceyServerCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyGpuCommand : public BaseCommand {
public:
    TraceyGpuCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class TraceyCompromiseCommand : public BaseCommand {
public:
    TraceyCompromiseCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Forward remote control payload to an agent via server proxy.
class TraceyControlCommand : public BaseCommand {
public:
    TraceyControlCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

// Fetch deep diagnostic snapshot for a specific agent.
class TraceyDeepDiveCommand : public BaseCommand {
public:
    TraceyDeepDiveCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_COMMANDS_TRACEY_COMMANDS_H
