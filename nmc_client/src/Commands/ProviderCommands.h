#ifndef NMC_PROVIDER_COMMANDS_H
#define NMC_PROVIDER_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class ProviderCommand final : public BaseCommand {
public:
    explicit ProviderCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

class ProviderComputeCommand final : public BaseCommand {
public:
    explicit ProviderComputeCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

class ProviderComputeStatusCommand final : public BaseCommand {
public:
    explicit ProviderComputeStatusCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

class ProviderComputeJobCommand final : public BaseCommand {
public:
    explicit ProviderComputeJobCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

class ProviderComputeInstancesCommand final : public BaseCommand {
public:
    explicit ProviderComputeInstancesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

class ProviderComputeCreateCommand final : public BaseCommand {
public:
    explicit ProviderComputeCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

class ProviderComputeActionCommand final : public BaseCommand {
public:
    explicit ProviderComputeActionCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands

#endif // NMC_PROVIDER_COMMANDS_H
