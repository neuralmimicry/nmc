// nmc_client/src/Core/CloudAPIClient.h
#ifndef NMC_CORE_CLOUD_API_CLIENT_H
#define NMC_CORE_CLOUD_API_CLIENT_H

#include <string>
#include <vector>
#include <map>
#include <httplib.h> // Include httplib for client-side HTTP requests
#include "Models/Bucket.h"
#include "Models/K8sCluster.h"
#include "Models/SSHKey.h"
#include "Models/VM.h"
#include "Models/CloudResponse.h" // Assuming a generic response structure

namespace NMC::Core {

// This class will now handle actual HTTP requests to the cloud API server.
    class CloudAPIClient {
    public:
        // Constructor to initialize the HTTP client with server host and port.
        CloudAPIClient(const std::string& host, int port);
        ~CloudAPIClient();

        // Bucket Operations
        Models::CloudResponse createBucket(const std::string& name, const std::string& location, const std::string& type);
        Models::CloudResponse deleteBucket(const std::string& name);
        Models::CloudResponse getBucket(const std::string& name);
        Models::CloudResponse listBuckets(const std::string& filterName = "");
        Models::CloudResponse listBucketLocations();
        Models::CloudResponse listBucketTypes();
        Models::CloudResponse resetBucketKey(const std::string& name);

        // K8s Operations
        Models::CloudResponse createK8sCluster(const std::string& name, const std::string& region);
        Models::CloudResponse deleteK8sCluster(const std::string& id);
        Models::CloudResponse getK8sCluster(const std::string& id);
        Models::CloudResponse getKubeConfig(const std::string& id);
        Models::CloudResponse listK8sClusters(const std::string& filterName = "");
        Models::CloudResponse listK8sLocations(const std::string& filterSku = "");
        Models::CloudResponse resumeK8sCluster(const std::string& id);
        Models::CloudResponse suspendK8sCluster(const std::string& id);

        // Model Operations
        Models::CloudResponse uploadModel(const std::string& filePath, const std::string& sku, const std::string& registryName, const std::string& tag);

        // SSH Key Operations
        Models::CloudResponse createSSHKey(const std::string& name, const std::string& publicKey, const std::string& description);
        Models::CloudResponse deleteSSHKey(const std::string& id);
        Models::CloudResponse listSSHKeys(const std::string& filterName = "");

        // VM Operations
        Models::CloudResponse createVM(const std::string& name, const std::string& sku, const std::string& region, const std::string& osImage, const std::string& publicKeyId, const std::string& initScript);
        Models::CloudResponse deleteVM(const std::string& id);
        Models::CloudResponse getVM(const std::string& id);
        Models::CloudResponse listVMs(const std::string& filterName = "");
        Models::CloudResponse listVMLocations(const std::string& filterSku = "");
        Models::CloudResponse listVMOSImages(const std::string& filterSku = "");
        Models::CloudResponse listVMSKUs(const std::string& filterSku = "");
        Models::CloudResponse restartVM(const std::string& id);
        Models::CloudResponse resumeVM(const std::string& id);
        Models::CloudResponse suspendVM(const std::string& id);

    private:
        std::unique_ptr<httplib::Client> cli; // HTTP client instance
        // Helper function to process HTTP responses into Models::CloudResponse
        Models::CloudResponse processHttpResponse(httplib::Result& res, const std::string& successMessage) const;
    };

} // namespace NMC::Core

#endif // NMC_CORE_CLOUD_API_CLIENT_H