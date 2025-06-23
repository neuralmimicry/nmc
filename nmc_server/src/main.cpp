// server/main.cpp
#include <iostream>
#include <httplib.h>
#include "APIRoutes.h" // Our route handler class

int main() {
    httplib::Server svr; // Create an HTTP server instance

    // Set up API routes
    NMC::Server::APIRoutes apiRoutes(svr);

    // Set server connection timeout in seconds (optional)
    svr.set_read_timeout(5);  // Read timeout for request
    svr.set_write_timeout(5); // Write timeout for response
    // svr.set_idle_timeout(10); // Idle timeout for connection - REMOVED: Not available in httplib v0.15.3

    // Listen on port 8080 (you can change this)
    const char* host = "0.0.0.0"; // Listen on all available network interfaces
    int port = 8080;

    std::cout << "NMC Server listening on http://" << host << ":" << port << std::endl;
    std::cout << "Press Ctrl+C to stop the server." << std::endl;

    // Start the server (blocking call)
    // For production, you might want to run this in a separate thread
    // or use a more sophisticated daemonization method.
    if (!svr.listen(host, port)) {
        std::cerr << "Error: Could not start server on " << host << ":" << port << std::endl;
        return 1;
    }

    return 0;
}
