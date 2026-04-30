#ifndef NMC_COMMANDS_GAIL_COMMANDS_H
#define NMC_COMMANDS_GAIL_COMMANDS_H

#include "BaseCommand.h"

#include <memory>

namespace NMC::CLI {
class Command;
}

namespace NMC::Commands {

class GailCommand : public BaseCommand {
public:
    GailCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingCommand : public BaseCommand {
public:
    GailTradingCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingStatusCommand : public BaseCommand {
public:
    GailTradingStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingPortfolioCommand : public BaseCommand {
public:
    GailTradingPortfolioCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingPositionsCommand : public BaseCommand {
public:
    GailTradingPositionsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingHistoryCommand : public BaseCommand {
public:
    GailTradingHistoryCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingLogsCommand : public BaseCommand {
public:
    GailTradingLogsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingExchangesCommand : public BaseCommand {
public:
    GailTradingExchangesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingCurrenciesCommand : public BaseCommand {
public:
    GailTradingCurrenciesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingConfigCommand : public BaseCommand {
public:
    GailTradingConfigCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingConfigSetCommand : public BaseCommand {
public:
    GailTradingConfigSetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingPauseCommand : public BaseCommand {
public:
    GailTradingPauseCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingResumeCommand : public BaseCommand {
public:
    GailTradingResumeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingOverrideCommand : public BaseCommand {
public:
    GailTradingOverrideCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

class GailTradingEvaluateCommand : public BaseCommand {
public:
    GailTradingEvaluateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags,
                const std::vector<std::string>& parsedArgs,
                const CLI::GlobalFlags& globalFlags) override;
};

void attachDirectGailSubcommands(const std::shared_ptr<NMC::CLI::Command>& root);

} // namespace NMC::Commands

#endif // NMC_COMMANDS_GAIL_COMMANDS_H
