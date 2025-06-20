#ifndef NMC_CORE_CLOUD_API_CLIENT_H
#define NMC_CORE_CLOUD_API_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include "Models/Bucket.h"
#include "Models/K8sCluster.h"
#include "Models/SSHKey.h"
#include "Models/VM.h"
#include "Models/CloudResponse.h" // Assuming a generic response structure


namespace NMC::Core {

// This class would handle all actual HTTP requests to the cloud API.
// For this example, these are just mock implementations.
class CloudAPIClient {
public:
    CloudAPIClient();
    ~CloudAPIClient();

    // Bucket Operations
    static Models::CloudResponse createBucket(const std::string& name, const std::string& location, const std::string& type);
    static Models::CloudResponse deleteBucket(const std::string& name);
    static Models::CloudResponse getBucket(const std::string& name);
    static Models::CloudResponse listBuckets(const std::string& filterName = "");
    static Models::CloudResponse listBucketLocations();
    static Models::CloudResponse listBucketTypes();
    static Models::CloudResponse resetBucketKey(const std::string& name);

    // K8s Operations
    static Models::CloudResponse createK8sCluster(const std::string& name, const std::string& region);
    static Models::CloudResponse deleteK8sCluster(const std::string& id);
    static Models::CloudResponse getK8sCluster(const std::string& id);
    static Models::CloudResponse getKubeConfig(const std::string& id);
    static Models::CloudResponse listK8sClusters(const std::string& filterName = "");
    static Models::CloudResponse listK8sLocations(const std::string& filterSku = "");
    static Models::CloudResponse resumeK8sCluster(const std::string& id);
    static Models::CloudResponse suspendK8sCluster(const std::string& id);

    // Model Operations
    static Models::CloudResponse uploadModel(const std::string& filePath, const std::string& sku, const std::string& registryName, const std::string& tag);

    // SSH Key Operations
    static Models::CloudResponse createSSHKey(const std::string& name, const std::string& publicKey, const std::string& description);
    static Models::CloudResponse deleteSSHKey(const std::string& id);
    static Models::CloudResponse listSSHKeys(const std::string& filterName = "");

    // VM Operations
    static Models::CloudResponse createVM(const std::string& name, const std::string& sku, const std::string& region, const std::string& osImage, const std::string& publicKeyId, const std::string& initScript);
    static Models::CloudResponse deleteVM(const std::string& id);
    static Models::CloudResponse getVM(const std::string& id);
    static Models::CloudResponse listVMs(const std::string& filterName = "");
    static Models::CloudResponse listVMLocations(const std::string& filterSku = "");
    static Models::CloudResponse listVMOSImages(const std::string& filterSku = "");
    static Models::CloudResponse listVMSKUs(const std::string& filterSku = "");
    static Models::CloudResponse restartVM(const std::string& id);
    static Models::CloudResponse resumeVM(const std::string& id);
    static Models::CloudResponse suspendVM(const std::string& id);

private:
    // Helper function to simulate API calls
    static Models::CloudResponse simulateApiResponse(bool success, const std::string& message, const std::string& data = "");
};

} // namespace NMC::Core


#endif // NMC_CORE_CLOUD_API_CLIENT_H