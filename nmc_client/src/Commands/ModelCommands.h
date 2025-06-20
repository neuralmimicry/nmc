#ifndef NMC_COMMANDS_MODEL_COMMANDS_H
#define NMC_COMMANDS_MODEL_COMMANDS_H

#include "BaseCommand.h"

namespace NMC::Commands {

class ModelCommand : public BaseCommand {
public:
    ModelCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

class ModelUploadCommand : public BaseCommand {
public:
    ModelUploadCommand();
    int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const CLI::GlobalFlags& globalFlags) override;
};

} // namespace NMC::Commands


#endif // NMC_COMMANDS_MODEL_COMMANDS_H