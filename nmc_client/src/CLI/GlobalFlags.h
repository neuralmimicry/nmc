#ifndef NMC_GLOBAL_FLAGS_H
#define NMC_GLOBAL_FLAGS_H

#include <string>

namespace NMC::CLI {

struct GlobalFlags {
    std::string outputFormat; // "json", "json-line", "yaml", "" (human-readable)
};

} // namespace NMC::CLI


#endif // NMC_GLOBAL_FLAGS_H