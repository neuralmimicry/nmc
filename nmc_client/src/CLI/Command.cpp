#include "Command.h"

#include <utility>
#include "Core/Utils.h" // For string manipulation


namespace NMC::CLI {

void Flag::setValue(const std::string& val) {
    switch (type) {
        case FlagType::String:
            stringValue = val;
            break;
        case FlagType::Bool:
            // Normalize boolean values
            boolValue = (val == "true" || val == "1" || val.empty()); // Empty string often implies true for boolean flags
            break;
        case FlagType::Int:
            try {
                intValue = std::stoi(val);
                // Preserve the original string to keep downstream flag parsing consistent.
                stringValue = val;
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid integer value for flag --" << longName << ": " << val << std::endl;
                // You might want to throw an exception here or set a default
                intValue = 0;
            }
            break;
    }
}

Command::Command(const std::string& name, std::string  description)
    : name(name), description(std::move(description)), usage(""), examples("") {
    // Add default help flag to all commands
    addFlag(Flag("h", "help", "help for " + name, FlagType::Bool));
}

void Command::addFlag(const Flag& flag) {
    flags.push_back(flag);
}

void Command::addArgument(const Argument& arg) {
    arguments.push_back(arg);
}

void Command::addSubcommand(const std::shared_ptr<Command>& subcommand) {
    subcommands[subcommand->name] = subcommand;
    for (const auto& alias : subcommand->aliases) {
        subcommands[alias] = subcommand;
    }
}

void Command::addAlias(const std::string& alias) {
    aliases.push_back(alias);
}

void Command::printHelp() const {
    std::cout << description << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << usage << std::endl;

    if (!examples.empty()) {
        std::cout << "Examples:" << std::endl;
        std::cout << examples << std::endl;
    }

    if (!subcommands.empty()) {
        std::cout << "Available Commands:" << std::endl;
        for (const auto& pair : subcommands) {
            // Only print direct command names, not aliases here to avoid duplication
            bool isAlias = false;
            for (const auto& existingCommand : subcommands) {
                if (existingCommand.second == pair.second && existingCommand.first != pair.first) {
                    // Check if 'pair.first' is an alias for 'existingCommand.first'
                    for(const auto& alias : existingCommand.second->aliases) {
                        if (alias == pair.first) {
                            isAlias = true;
                            break;
                        }
                    }
                }
                if (isAlias) break;
            }
            if (!isAlias) {
                std::cout << "  " << pair.first << " " << Core::Utils::padRight(pair.second->name, 12) << pair.second->description << std::endl;
            }
        }
    }

    if (!flags.empty()) {
        std::cout << "Flags:" << std::endl;
        for (const auto& flag : flags) {
            std::cout << "  -" << flag.shortName << ", --" << flag.longName << " " << flag.description;
            if (flag.isRequired) {
                std::cout << " (required)";
            }
            if (!flag.defaultValue.empty()) {
                std::cout << " (default: " << flag.defaultValue << ")";
            }
            std::cout << std::endl;
        }
    }
    printGlobalFlagsHelp();
    std::cout << "Use \"" << name << " [command] --help\" for more information about a command." << std::endl;
}

void Command::printGlobalFlagsHelp() const {
    std::cout << "Global Flags:" << std::endl;
    std::cout << "  -h, --help            help for " << name << std::endl;
    std::cout << "  -V, --version         show nmc version" << std::endl;
    std::cout << "  -x, --output string   Output format. Empty for human-readable, 'json', 'json-line' or 'yaml'" << std::endl;
}

Flag* Command::getFlag(const std::string& flagName) {
    for (auto& flag : flags) {
        if (flag.longName == flagName || flag.shortName == flagName) {
            return &flag;
        }
    }
    return nullptr;
}

bool Command::validateFlags(const std::map<std::string, std::string>& parsedFlags) const {
    for (const auto& flagDef : flags) {
        if (flagDef.isRequired) {
            bool found = false;
            for (const auto& parsedFlag : parsedFlags) {
                if (parsedFlag.first == flagDef.longName || parsedFlag.first == flagDef.shortName) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "Error: Required flag --" << flagDef.longName << " is missing." << std::endl;
                return false;
            }
        }
    }
    return true;
}

bool Command::validateArguments(const std::vector<std::string>& parsedArgs) const {
    size_t requiredCount = 0;
    for (const auto& arg : arguments) {
        if (arg.isRequired) {
            ++requiredCount;
        }
    }

    if (parsedArgs.size() < requiredCount) {
        std::cerr << "Error: Not enough arguments provided." << std::endl;
        std::cerr << "Expected at least " << requiredCount << ", got " << parsedArgs.size() << std::endl;
        return false;
    }
    if (parsedArgs.size() > arguments.size()) {
        std::cerr << "Error: Too many arguments provided." << std::endl;
        std::cerr << "Expected at most " << arguments.size() << ", got " << parsedArgs.size() << std::endl;
        return false;
    }
    return true;
}

} // namespace NMC::CLI
