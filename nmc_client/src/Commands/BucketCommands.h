#ifndef NMC_COMMANDS_BUCKET_COMMANDS_H
#define NMC_COMMANDS_BUCKET_COMMANDS_H

#include "BaseCommand.h"
#include <string>

namespace NMC::Commands {

// Top-level 'bucket' command
class BucketCommand : public BaseCommand {
public:
    BucketCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket create
class BucketCreateCommand : public BaseCommand {
public:
    BucketCreateCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket delete
class BucketDeleteCommand : public BaseCommand {
public:
    BucketDeleteCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket get
class BucketGetCommand : public BaseCommand {
public:
    BucketGetCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket list
class BucketListCommand : public BaseCommand {
public:
    BucketListCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket list-locations
class BucketListLocationsCommand : public BaseCommand {
public:
    BucketListLocationsCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket list-types
class BucketListTypesCommand : public BaseCommand {
public:
    BucketListTypesCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket reset-key
class BucketResetKeyCommand : public BaseCommand {
public:
    BucketResetKeyCommand(std::shared_ptr<NMC::Core::CloudAPIClient> client);
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_BUCKET_COMMANDS_H