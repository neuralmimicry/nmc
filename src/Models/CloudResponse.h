#ifndef NMC_MODELS_CLOUD_RESPONSE_H
#define NMC_MODELS_CLOUD_RESPONSE_H

#include <string>

namespace NMC::Models {

// Generic structure for responses from the simulated cloud API.
struct CloudResponse {
    bool success;
    std::string message;
    std::string data; // This would typically be a JSON string or parsed object
};

} // namespace NMC::Models


#endif // NMC_MODELS_CLOUD_RESPONSE_H