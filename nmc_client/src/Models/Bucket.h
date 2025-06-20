#ifndef NMC_MODELS_BUCKET_H
#define NMC_MODELS_BUCKET_H

#include <string>

namespace NMC::Models {

struct Bucket {
    std::string name;
    std::string location;
    std::string type;
    std::string status;
    // Add other relevant fields for a bucket
};

} // namespace NMC::Models


#endif // NMC_MODELS_BUCKET_H