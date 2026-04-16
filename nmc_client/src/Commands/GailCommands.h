#ifndef NMC_COMMANDS_GAIL_COMMANDS_H
#define NMC_COMMANDS_GAIL_COMMANDS_H

#include <memory>

namespace NMC::CLI {
class Command;
}

namespace NMC::Commands {

std::shared_ptr<NMC::CLI::Command> buildGailCommandTree();

} // namespace NMC::Commands

#endif // NMC_COMMANDS_GAIL_COMMANDS_H
