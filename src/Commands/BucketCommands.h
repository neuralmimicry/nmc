#ifndef NMC_COMMANDS_BUCKET_COMMANDS_H
#define NMC_COMMANDS_BUCKET_COMMANDS_H

#include "BaseCommand.h"
#include <string>

namespace NMC::Commands {

// Top-level 'bucket' command
class BucketCommand : public BaseCommand {
public:
    BucketCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket create
class BucketCreateCommand : public BaseCommand {
public:
    BucketCreateCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket delete
class BucketDeleteCommand : public BaseCommand {
public:
    BucketDeleteCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket get
class BucketGetCommand : public BaseCommand {
public:
    BucketGetCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket list
class BucketListCommand : public BaseCommand {
public:
    BucketListCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket list-locations
class BucketListLocationsCommand : public BaseCommand {
public:
    BucketListLocationsCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket list-types
class BucketListTypesCommand : public BaseCommand {
public:
    BucketListTypesCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

// Subcommand: bucket reset-key
class BucketResetKeyCommand : public BaseCommand {
public:
    BucketResetKeyCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_BUCKET_COMMANDS_H