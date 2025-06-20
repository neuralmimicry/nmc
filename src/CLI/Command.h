#ifndef NMC_CLI_COMMAND_H
#define NMC_CLI_COMMAND_H

#include <string>
#include <utility>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <iostream>
#include "GlobalFlags.h"

namespace NMC::CLI {

// Forward declaration
class CLIParser;

enum FlagType {
    String,
    Bool,
    Int
};

struct Flag {
    std::string shortName;
    std::string longName;
    std::string description;
    FlagType type;
    bool isRequired;
    std::string defaultValue; // For string and int (as string)

    // Store parsed value
    std::string stringValue;
    bool boolValue;
    int intValue;

    Flag(std::string  shortN, std::string  longN, std::string  desc, FlagType t, bool required = false, std::string  defaultVal = "")
        : shortName(std::move(shortN)), longName(std::move(longN)), description(std::move(desc)), type(t), isRequired(required), defaultValue(std::move(defaultVal)),
          boolValue(false), intValue(0) {}

    // Method to set value based on type
    void setValue(const std::string& val);
};

struct Argument {
    std::string name;
    std::string description;
    bool isRequired;
    int position; // 0-indexed position

    Argument(std::string  n, std::string  desc, bool required, int pos)
        : name(std::move(n)), description(std::move(desc)), isRequired(required), position(pos) {}
};

class Command : public std::enable_shared_from_this<Command> {
public:
    std::string name;
    std::string description;
    std::string usage;
    std::string examples;
    std::vector<std::string> aliases;

    // Subcommands
    std::map<std::string, std::shared_ptr<Command>> subcommands;

    // Flags specific to this command
    std::vector<Flag> flags;

    // Arguments specific to this command
    std::vector<Argument> arguments;

    Command(const std::string& name, std::string  description);
    virtual ~Command() = default;

    void addFlag(const Flag& flag);
    void addArgument(const Argument& arg);
    void addSubcommand(const std::shared_ptr<Command>& subcommand);
    void addAlias(const std::string& alias);

    // Pure virtual function to be implemented by concrete commands
    // This is where the command's logic will reside.
    virtual int execute(const std::map<std::string, std::string>& parsedFlags, const std::vector<std::string>& parsedArgs, const GlobalFlags& globalFlags) = 0;

    // Helper to print command help
    void printHelp() const;
    void printGlobalFlagsHelp() const;

    // Get a flag by its long name or short name
    Flag* getFlag(const std::string& name);

protected:
    // Helper to validate parsed flags
    bool validateFlags(const std::map<std::string, std::string>& parsedFlags) const;
    // Helper to validate parsed arguments
    bool validateArguments(const std::vector<std::string>& parsedArgs) const;

};

} // namespace NMC::CLI


#endif // NMC_CLI_COMMAND_H