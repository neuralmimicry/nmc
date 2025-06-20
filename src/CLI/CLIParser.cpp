#include "CLIParser.h"
#include "Utils.h"
#include <iostream>
#include <algorithm> // For std::remove

namespace NMC::CLI {

CLIParser::CLIParser() {
    // Initialize global flags with defaults
    globalFlags.outputFormat = "";
}

void CLIParser::registerCommand(const std::shared_ptr<Command>& command) {
    rootCommands[command->name] = command;
    for (const auto& alias : command->aliases) {
        rootCommands[alias] = command;
    }
}

int CLIParser::parseAndExecute(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc); // Skip executable name

    if (args.empty()) {
        printRootHelp();
        return 0;
    }

    // Check for global flags first
    for (auto it = args.begin(); it != args.end(); ) {
        if (*it == "-x" || *it == "--output") {
            if (std::next(it) != args.end()) {
                globalFlags.outputFormat = *std::next(it);
                it = args.erase(it, std::next(it, 2)); // Erase flag and its value
            } else {
                std::cerr << "Error: --output flag requires a value." << std::endl;
                return 1;
            }
        } else {
            ++it;
        }
    }

    std::string commandName = args[0];
    args.erase(args.begin()); // Remove command name

    if (commandName == "-h" || commandName == "--help") {
        printRootHelp();
        return 0;
    }

    // Find the command
    auto it = rootCommands.find(commandName);
    if (it == rootCommands.end()) {
        std::cerr << "Error: Unknown command \"" << commandName << "\"" << std::endl;
        printRootHelp();
        return 1;
    }

    std::shared_ptr<Command> currentCommand = it->second;
    std::vector<std::string> currentArgs = args;

    // Traverse subcommands
    while (!currentArgs.empty() && currentCommand->subcommands.count(currentArgs[0])) {
        if (currentArgs[0] == "-h" || currentArgs[0] == "--help") {
            currentCommand->printHelp();
            return 0;
        }
        currentCommand = currentCommand->subcommands[currentArgs[0]];
        currentArgs.erase(currentArgs.begin());
    }

    // Check for --help specifically for the final command
    bool helpRequested = false;
    for (const auto& arg : currentArgs) {
        if (arg == "-h" || arg == "--help") {
            helpRequested = true;
            break;
        }
    }

    if (helpRequested) {
        currentCommand->printHelp();
        return 0;
    }

    // Parse flags and arguments for the final command
    std::map<std::string, std::string> parsedFlags = parseFlags(currentArgs, currentCommand.get());
    std::vector<std::string> parsedArguments = parseArguments(currentArgs, currentCommand.get());

    // Validate if any unparsed arguments remain (which would indicate an error)
    if (!currentArgs.empty()) {
        std::cerr << "Error: Unknown argument or flag: " << currentArgs[0] << std::endl;
        currentCommand->printHelp();
        return 1;
    }

    // Execute the command
    return currentCommand->execute(parsedFlags, parsedArguments, globalFlags);
}

std::map<std::string, std::string> CLIParser::parseFlags(std::vector<std::string>& args, Command* cmd) {
    std::map<std::string, std::string> parsedFlags;
    std::vector<std::string> remainingArgs;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        bool foundFlag = false;

        if (arg.rfind("--", 0) == 0) { // Long flag
            std::string flagName = arg.substr(2);
            std::string flagValue;
            size_t eqPos = flagName.find('=');
            if (eqPos != std::string::npos) {
                flagValue = flagName.substr(eqPos + 1);
                flagName = flagName.substr(0, eqPos);
            }

            Flag* flagDef = cmd->getFlag(flagName);
            if (!flagDef) {
                remainingArgs.push_back(arg); // Not a recognized flag for this command
                continue;
            }

            if (flagDef->type == FlagType::Bool) {
                flagDef->setValue("true"); // Presence of the flag means true
            } else {
                if (flagValue.empty()) { // Value not provided with '='
                    if (i + 1 < args.size() && args[i+1].rfind('-', 0) != 0) { // Next arg is not a flag
                        flagValue = args[++i]; // Take next arg as value
                    } else {
                        std::cerr << "Error: Flag --" << flagName << " requires a value." << std::endl;
                        // Return empty map or indicate error
                        return {};
                    }
                }
                flagDef->setValue(flagValue);
            }
            parsedFlags[flagName] = flagDef->stringValue.empty() ? std::to_string(flagDef->boolValue) : flagDef->stringValue; // Store based on the flag's internal parsed value
            foundFlag = true;
        } else if (arg.rfind('-', 0) == 0 && arg.length() > 1 && arg.at(1) != '-') { // Short flag
            std::string flagName = arg.substr(1);

            Flag* flagDef = cmd->getFlag(flagName);
            if (!flagDef) {
                remainingArgs.push_back(arg); // Not a recognized flag for this command
                continue;
            }

            if (flagDef->type == FlagType::Bool) {
                flagDef->setValue("true"); // Presence of the flag means true
            } else {
                if (i + 1 < args.size() && args[i+1].rfind('-', 0) != 0) {
                    flagDef->setValue(args[++i]);
                } else {
                    std::cerr << "Error: Flag -" << flagName << " requires a value." << std::endl;
                    return {};
                }
            }
            parsedFlags[flagName] = flagDef->stringValue.empty() ? std::to_string(flagDef->boolValue) : flagDef->stringValue; // Store based on the flag's internal parsed value
            foundFlag = true;
        }

        if (!foundFlag) {
            remainingArgs.push_back(arg);
        }
    }
    args = remainingArgs; // Update args to only contain unparsed items (positional args)
    return parsedFlags;
}


std::vector<std::string> CLIParser::parseArguments(std::vector<std::string>& args, Command* cmd) {
    std::vector<std::string> parsedArgs;
    // For simplicity, arguments are just the remaining positional arguments in order
    // A more complex parser would handle fixed positions and variadic arguments
    if (args.size() > cmd->arguments.size()) {
        std::cerr << "Error: Too many arguments provided." << std::endl;
        return {}; // Indicate error
    }

    for (size_t i = 0; i < args.size(); ++i) {
        if (i < cmd->arguments.size()) {
            parsedArgs.push_back(args[i]);
        }
    }
    args.clear(); // All remaining args are considered parsed arguments
    return parsedArgs;
}

void CLIParser::printRootHelp() const {
    std::cout << "CLI tool for NeuralMimicry Cloud" << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  nmc [command]" << std::endl;
    std::cout << "Available Commands:" << std::endl;
    for (const auto& pair : rootCommands) {
         // Only print direct command names, not aliases here to avoid duplication
        bool isAlias = false;
        for (const auto& existingCommand : rootCommands) {
            if (existingCommand.second == pair.second && existingCommand.first != pair.first) {
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
            std::cout << "  " << pair.first << Core::Utils::padRight("", 12 - pair.first.length()) << pair.second->description << std::endl;
        }
    }
    std::cout << "Flags:" << std::endl;
    std::cout << "  -h, --help            help for nmc" << std::endl;
    std::cout << "  -x, --output string   Output format. Empty for human-readable, 'json', 'json-line' or 'yaml'" << std::endl;
    std::cout << "Use \"nmc [command] --help\" for more information about a command." << std::endl;
}

} // namespace NMC::CLI
