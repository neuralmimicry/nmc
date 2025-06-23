# NMC CLI Application

This is a C++ CLI application named `nmc`. 
It provides a modular and extensible framework for building command-line interfaces, with a focus on best practices for structure, security (conceptual), robustness, resilience, and ease of adding/amending/removing commands.

**Current Features:**

* Command-line argument parsing.
* Support for nested subcommands.
* Support for flags (short and long, with and without values).
* Positional arguments.
* Generated help messages for root, top-level, and subcommands.
* Placeholder for interaction with a cloud API (`CloudAPIClient`).
* implementations for cloud operations (Bucket, K8s, Model, SSH, VM).

**Project Structure:**

The project is organized into logical directories:

* `src/CLI/`: Core CLI parsing logic, including `Command` definitions and the `CLIParser`.
* `src/Commands/`: Contains all the specific `nmc` commands. Each top-level command (e.g., `bucket`, `k8s`) has its own header and source file, and its subcommands are nested within.
* `src/Core/`: Core functionalities like `CloudAPIClient` (mocked cloud interaction) and `Utils` (utility functions).
* `src/Models/`: Data structures representing cloud resources (e.g., `Bucket`, `K8sCluster`).

**Building the Application:**

To build `nmc`, you will need CMake and a C++17 compatible compiler (e.g., GCC, Clang).

1.  **Navigate to the `nmc` root directory:**
    ```bash
    cd nmc
    ```

2.  **Create a build directory and navigate into it:**
    ```bash
    mkdir build
    cd build
    ```

3.  **Run CMake to configure the project:**
    ```bash
    cmake ..
    ```

4.  **Build the application:**
    ```bash
    cmake --build .
    ```

After a successful build, the `nmc` executable will be located in the `build` directory.

**Running the Application (Examples):**

From the `build` directory:

* **Root help:**
    ```bash
    ./nmc --help
    ./nmc
    ```

* **Bucket command help:**
    ```bash
    ./nmc bucket --help
    ```

* **Create a bucket (mocked):**
    ```bash
    ./nmc bucket create my-new-bucket --location eu-central --type standard
    ```

* **List buckets (mocked) with JSON output:**
    ```bash
    ./nmc bucket list -x json
    ```

* **Get K8s cluster details (mocked):**
    ```bash
    ./nmc k8s get my-k8s-cluster-id
    ```

* **Create a VM (mocked):**
    ```bash
    ./nmc vm create dev-vm --sku nmx-h100-80 --region us-east --os-image ubuntu-22.04 --public-key-id sshkey-123 --init-script "#!/bin/bash echo Hello"
    ```

* **Display version:**
    ```bash
    ./nmc version
    ```

**Extending the Application:**

To add a new top-level command (e.g., `database`):

1.  Create `src/Commands/DatabaseCommands.h` and `src/Commands/DatabaseCommands.cpp`.
2.  Define a `DatabaseCommand` class inheriting from `NMC::Commands::BaseCommand`.
3.  Implement its `execute` method (or leave it to just print help if it has subcommands).
4.  Add subcommands (e.g., `DatabaseCreateCommand`, `DatabaseListCommand`) as needed, following the `BucketCommands` pattern.
5.  In `src/main.cpp`, create an instance of `DatabaseCommand` and register it with the `CLIParser`:
    ```cpp
    auto dbCmd = std::make_shared<NMC::Commands::DatabaseCommand>();
    dbCmd->addSubcommand(std::make_shared<NMC::Commands::DatabaseCreateCommand>());
    // ... add other subcommands
    parser.registerCommand(dbCmd);
    ```
6.  If the new command interacts with a new type of cloud resource, you might need to add a new model in `src/Models/` and extend `CloudAPIClient` with corresponding API calls.

This structure provides a solid foundation for a comprehensive and maintainable C++ CLI application.
Explanation of Components:

CMakeLists.txt: Build configuration for CMake.
src/main.cpp: Entry point of the application. Initializes the CLI parser and executes commands.
src/CLI/:
Command.h / Command.cpp: Defines an abstract Command class and concrete Flag and Argument classes. This provides the fundamental building blocks for defining CLI commands, their flags, and arguments.
CLIParser.h / CLIParser.cpp: Handles parsing of command-line arguments, matching them to registered commands, and dispatching execution. It manages the command tree structure.
GlobalFlags.h: Stores global flags like --output.
src/Commands/:
BaseCommand.h / BaseCommand.cpp: A base class for all nmc commands, inheriting from CLI::Command. It can hold common logic or member variables.
RootCommand.h / RootCommand.cpp: Represents the top-level nmc command.
BucketCommands.h / BucketCommands.cpp: Encapsulates all nmc bucket related commands (create, delete, get, list, etc.). Each subcommand (e.g., create, delete) will be its own class inheriting from BaseCommand.
K8sCommands.h / K8sCommands.cpp: Similar to BucketCommands, but for nmc k8s.
ModelCommands.h / ModelCommands.cpp: For nmc model.
SSHCommands.h / SSHCommands.cpp: For nmc ssh.
VMCommands.h / VMCommands.cpp: For nmc vm.
VersionCommand.h / VersionCommand.cpp: Handles the nmc version command.
src/Core/:
CloudAPIClient.h / CloudAPIClient.cpp: A placeholder for the actual cloud API interaction. This class would contain methods to communicate with the hypothetical Ori Global Cloud API (e.g., using curlpp, cpprestsdk, or similar HTTP client libraries). It would handle authentication, request formatting, and response parsing.
Utils.h / Utils.cpp: Utility functions (e.g., for string manipulation, error reporting, JSON/YAML serialization if not handled by a dedicated library).
src/Models/:
Bucket.h, K8sCluster.h, SSHKey.h, VM.h: Simple Plain Old Data (POD) structures or classes representing the data models for resources managed by the CLI. These would typically mirror the structure of objects returned by the cloud API.
CloudResponse.h: A generic structure to encapsulate responses from the cloud API, including status, error messages, and payload.
Key Design Principles Implemented:

Modularity: Each top-level command (bucket, k8s, vm, etc.) and its subcommands are logically grouped into separate files and classes. New commands can be added by creating new classes and registering them with the CLIParser.
Separation of Concerns:
CLIParser is responsible only for parsing and dispatching.
Command classes are responsible for defining their flags/arguments and their specific logic.
CloudAPIClient handles all communication with the backend.
Models define data structures.
Robustness and Resilience (Conceptual):
Error handling is demonstrated at the CLI parsing level. In a real application, the CloudAPIClient would handle network errors, API errors, timeouts, etc.
Input validation for command arguments (e.g., ensuring required flags are present) is part of each command's execute logic.
Security (Conceptual):
No hardcoded credentials. A real application would use environment variables, configuration files, or a secure credential store.
Input sanitization would be crucial before sending data to a backend (not explicitly shown but assumed in CloudAPIClient).
Best Practices:
Use of C++ standard library features.
Clear class hierarchies and interfaces.
Meaningful variable and function names.
Extensive comments for clarity.
Use of std::shared_ptr for managing command lifetimes if complex ownership is needed, though simple new/delete or unique pointers suffice for this example.



