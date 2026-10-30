#pragma once

namespace NMC::Server {
    // Apply only after validating GitHub's signed HTTPS storage destination.
    // A presigned request target must travel byte for byte: encoding its '+',
    // comma or semicolon again changes signed query fields and produces 403.
    // The separate storage client never receives the GitHub API credential.
    template <typename Client>
    inline void configureGitHubArtifactDownload(Client& client) {
        client.set_url_encode(false);
        client.set_connection_timeout(5, 0);
        client.set_read_timeout(60, 0);
        client.set_write_timeout(10, 0);
        client.set_follow_location(false);
    }
}
