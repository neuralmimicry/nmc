#pragma once
#include "BaseCommand.h"

namespace NMC::Commands {
/** Read-only declared product identities; runtime observations and approval are
 * distinct contracts. The command never changes workload or access policy. */
class PlatformProductsCommand : public BaseCommand {
public:
    explicit PlatformProductsCommand(std::shared_ptr<Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& flags,
                const std::vector<std::string>& args,
                const CLI::GlobalFlags& globalFlags) override;
};
class PlatformCommand : public BaseCommand {
public:
    explicit PlatformCommand(std::shared_ptr<Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>&,
                const std::vector<std::string>&,
                const CLI::GlobalFlags&) override;
};
}
