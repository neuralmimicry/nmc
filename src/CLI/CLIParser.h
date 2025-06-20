#ifndef NMC_CLI_CLIPARSER_H
#define NMC_CLI_CLIPARSER_H

#include "Command.h"
#include "GlobalFlags.h"
#include <vector>
#include <string>
#include <map>
#include <memory>

namespace NMC::CLI {

class CLIParser {
public:
    CLIParser();
    ~CLIParser() = default;

    void registerCommand(const std::shared_ptr<Command>& command);
    int parseAndExecute(int argc, char* argv[]);

private:
    std::map<std::string, std::shared_ptr<Command>> rootCommands;
    GlobalFlags globalFlags;

    // Helper to parse flags for a given command
    static std::map<std::string, std::string> parseFlags(std::vector<std::string>& args, Command* cmd);
    // Helper to parse arguments
    static std::vector<std::string> parseArguments(std::vector<std::string>& args, Command* cmd);

    void printRootHelp() const;
};

} // namespace NMC::CLI


#endif // NMC_CLI_CLIPARSER_H