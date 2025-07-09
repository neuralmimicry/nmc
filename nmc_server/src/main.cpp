#include <iostream>
#include <httplib.h>
#include "APIRoutes.h"   // Our route handler class
#include "K8sHandlers.h"
#include <string>
#include <vector>        // Required for std::vector
#include <algorithm>     // Required for std::find
#include <exception>
#include <cstdlib>
#include <regex>         // For catching std::regex_error

// Custom terminate handler to catch any uncaught std::regex_error
void myTerminate() {
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::regex_error& ex) {
            std::cerr << "Fatal regex_error: " << ex.what() << std::endl;
        } catch (const std::exception& ex) {
            std::cerr << "Fatal exception: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "Fatal unknown exception" << std::endl;
        }
    } else {
        std::cerr << "Terminated without an active exception" << std::endl;
    }
    std::_Exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    // Install custom terminate handler
    std::set_terminate(myTerminate);

    try {
        // 1. Kubernetes C Client Global Environment Setup
        // Must be called ONCE before any worker threads are created.
        apiClient_setupGlobalEnv();
        std::cout << "Kubernetes C client global environment set up." << std::endl;

        httplib::Server svr; // Create an HTTP server instance

        // Set up API routes
        NMC::Server::APIRoutes apiRoutes(svr);

        // Set server connection timeout in seconds (optional)
        svr.set_read_timeout(5);  // Read timeout for request
        svr.set_write_timeout(5); // Write timeout for response

        // Listen on port 8080 (you can change this)
        const char* host = "0.0.0.0"; // Listen on all available network interfaces
        int port = 8080;

        // --- Parse command-line arguments for port ---
        std::vector<std::string> args(argv + 1, argv + argc); // Convert argv to vector for easier parsing

        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--port" || args[i] == "-p") {
                if (i + 1 < args.size()) {
                    try {
                        port = std::stoi(args[i + 1]);
                        // Basic port validation
                        if (port <= 0 || port > 65535) {
                            std::cerr << "Error: Invalid port number. Port must be between 1 and 65535." << std::endl;
                            return EXIT_FAILURE;
                        }
                        i++; // Skip the next argument as it's the port value
                    } catch (const std::exception& e) {
                        std::cerr << "Error: Invalid value for -port argument. Please provide an integer." << std::endl;
                        return EXIT_FAILURE;
                    }
                } else {
                    std::cerr << "Error: -port argument requires a value." << std::endl;
                    return EXIT_FAILURE;
                }
            }
            // Add other command-line arguments parsing here if needed in the future
        }

        std::cout << "NMC Server listening on http://" << host << ":" << port << std::endl;
        std::cout << "Press Ctrl+C to stop the server." << std::endl;

        // Start the server (blocking call)
        if (!svr.listen(host, port)) {
            std::cerr << "Error: Could not start server on " << host << ":" << port << std::endl;
            return EXIT_FAILURE;
        }

        // 2. Kubernetes C Client Global Environment Teardown
        // Must be called ONCE after all worker threads (including HTTP server threads) have finished.
        apiClient_unsetupGlobalEnv();
        std::cout << "Kubernetes C client global environment torn down." << std::endl;

        return EXIT_SUCCESS;

    } catch (const std::regex_error& ex) {
        std::cerr << "Caught a regex_error during runtime: " << ex.what() << std::endl;
        std::cerr << "Shutting down due to invalid regex usage in an imported library." << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "Unhandled exception: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unknown fatal error encountered." << std::endl;
        return EXIT_FAILURE;
    }
}
