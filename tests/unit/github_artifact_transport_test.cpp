#include "../../nmc_server/src/Core/GitHubArtifactTransport.h"
#include <httplib.h>

#include <iostream>
#include <string>
#include <thread>

int main() {
    // Azure signs decoded content-disposition fields. A literal '+' in this
    // supplied target represents a space; changing it to %2B breaks the SAS.
    const std::string target = "/runner/artifact.zip?rscd=attachment%3B+filename%3Dresult.zip&sig=example%2Bvalue";
    httplib::Server storage;
    storage.Get("/runner/artifact.zip", [&target](const httplib::Request& request, httplib::Response& response) {
        response.status = request.target == target ? 200 : 403;
        response.set_content(response.status == 200 ? "verified archive" : "signature mismatch", "application/zip");
    });
    const int port = storage.bind_to_any_port("127.0.0.1");
    if (port <= 0) return 1;
    std::thread listener([&storage] { storage.listen_after_bind(); });
    storage.wait_until_ready();

    httplib::Client ordinary("127.0.0.1", port);
    const auto altered = ordinary.Get(target);
    httplib::Client artifact("127.0.0.1", port);
    NMC::Server::configureGitHubArtifactDownload(artifact);
    const auto preserved = artifact.Get(target);
    storage.stop();
    listener.join();

    if (!altered || altered->status != 403 || !preserved || preserved->status != 200
        || preserved->body != "verified archive") {
        std::cerr << "The signed storage request target was changed or its archive was not returned\n";
        return 1;
    }
    std::cout << "Signed artifact query preserved over a real HTTP connection\n";
}
