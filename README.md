# NMC CLI Application (NeuralMimicry Continuum)

This is a C++ CLI application named `nmc` (NeuralMimicry Continuum).
It provides a modular and extensible framework for building command-line interfaces, with a focus on best practices for structure, security (conceptual), robustness, resilience, and ease of adding/amending/removing commands.

**Current Features:**

* Command-line argument parsing.
* Support for nested subcommands.
* Support for flags (short and long, with and without values).
* Positional arguments.
* Generated help messages for root, top-level, and subcommands.
* Placeholder for interaction with a cloud API (`CloudAPIClient`).
* implementations for cloud operations (Bucket, K8s, Model, SSH, VM).
* Refiner lifecycle control commands for Kubernetes (`refiner deploy/status/scale/logs/remove`).
* Continuum node recruitment flow (`node recruit`) with script/binary transfer to remote Ubuntu hosts.
* Optional bearer token authentication for the Continuum control plane.

**Project Structure:**

The project is organized into logical directories:

* `src/CLI/`: Core CLI parsing logic, including `Command` definitions and the `CLIParser`.
* `src/Commands/`: Contains all the specific `nmc` commands. Each top-level command (e.g., `bucket`, `k8s`) has its own header and source file, and its subcommands are nested within.
  OpenShift integration commands live in `OpenShiftCommands.*` and proxy to the NeuralMimicry OpenShift portal API.
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

nmc connection status: Displays the name and endpoint of the current active connection.

nmc connection make <NAME> --endpoint <URL> [--default]: Creates a new connection with a specified name and API endpoint. The --default flag allows setting this new connection as the default one immediately.

nmc connection drop <NAME>: Removes an existing connection by its name. If the dropped connection was the active one, the CLI will no longer have an active connection.

nmc connection list (alias ls): Lists all configured connections, indicating whether each is active or inactive.

nmc connection select <NAME>: Sets an existing connection as the default active connection.

nmc connection unset-default: Unsets the current default connection and reverts to the built-in host/port.

* **Set a bearer token for a connection:**
    ```bash
    ./nmc connection make prod --endpoint https://continuum.example --token "$OIDC_ACCESS_TOKEN" --default
    ./nmc connection set-token prod --token "$OIDC_ACCESS_TOKEN"
    ./nmc connection clear-token prod
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

* **OpenShift (Continuum) resources:**
    ```bash
    ./nmc openshift resources
    ```

* **List OpenShift clusters:**
    ```bash
    ./nmc openshift clusters
    ```

* **Request an OpenShift cluster (ROSA example):**
    ```bash
    ./nmc openshift request hpc-example --org neuralmimicry --gpu-count 8 --arch amd64 --region us-east-1 --provider rosa
    ```

* **Request with burst targets (repeatable flag):**
    ```bash
    ./nmc openshift request hpc-burst --org neuralmimicry --gpu-count 16 --arch amd64 --region us-east-1 --provider hybrid-burst --burst-target aro --burst-target gcp
    ```

* **Poll OpenShift cluster status until Ready:**
    ```bash
    ./nmc openshift status hpc-example --watch --until Ready --interval 10 --timeout 900
    ```

* **Deploy and monitor Refiner workload:**
    ```bash
    ./nmc refiner deploy --manifest /path/to/refiner-k8s.yaml --namespace refiner --timeout 300
    ./nmc refiner status --namespace refiner
    ./nmc refiner scale --namespace refiner --replicas 3
    ./nmc refiner logs --namespace refiner --tail 300
    ./nmc refiner remove --namespace refiner --delete-storage
    ```

* **Recruit a remote Ubuntu node through Continuum server (sample host `192.168.1.60`):**
    ```bash
    ./nmc node recruit --host 192.168.1.60 --user ubuntu --script /home/me/recruit-node.sh --ssh-key /home/me/.ssh/id_ed25519
    ```

* **Recruit directly from the CLI host (no server-side SSH execution):**
    ```bash
    ./nmc node recruit --host 192.168.1.60 --user ubuntu --binary /home/me/bin/continuum-agent --binary-arg --join --binary-arg token-123 --direct
    ```

* **Recruit and auto-configure for tenant workloads (API mode):**
    ```bash
    ./nmc node recruit \
      --host 192.168.1.60 \
      --user ubuntu \
      --ssh-key /home/me/.ssh/id_ed25519 \
      --node-type kubernetes \
      --auto-configure \
      --tenant-id acme \
      --tenant-name "Acme Manufacturing" \
      --tenant-environment prod \
      --capability apps \
      --capability podman \
      --capability kubernetes
    ```

* **Recruit and auto-configure directly from CLI host with custom playbook vars:**
    ```bash
    ./nmc node recruit \
      --host 192.168.1.60 \
      --user ubuntu \
      --direct \
      --auto-configure \
      --ansible-playbook /home/me/nmc/ansible/recruited-node.yml \
      --ansible-extra-var nmc_ops_contact=ops@acme.example \
      --ansible-extra-var nmc_backup_window=02:00-03:00
    ```

* **Use non-interactive sudo/become password with file input:**
    ```bash
    ./nmc node recruit \
      --host 192.168.1.60 \
      --user pbisaacs \
      --ssh-key /home/me/.ssh/id_ed25519 \
      --sudo-password-file /home/me/.nmc/sudo.pass \
      --auto-configure
    ```

* **Disable Ansible privilege escalation for auto-configure when not required:**
    ```bash
    ./nmc node recruit --host 192.168.1.60 --auto-configure --no-become
    ```

* **Dry-run the generated commands before execution:**
    ```bash
    ./nmc node recruit --host 192.168.1.60 --node-type kubernetes --dry-run --direct
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

**Workflows and Behavior Notes:**

Detailed end-to-end workflows, including CLI parsing, connection management, and server request lifecycles, are documented in:

- `docs/WORKFLOWS.md`
- `docs/SECURITY.md` (ISO 27001 / SOC 2 readiness and operational controls)

**OpenShift Integration Notes:**

The server proxies OpenShift provisioning and visibility requests to the NeuralMimicry OpenShift portal API (oshift).
Set the OpenShift portal base URL with the `NMC_OSHIFT_API_URL` environment variable when starting `nmc_server`.
The default is `http://127.0.0.1:8000`.

**Refiner Command Notes:**

The `refiner` command group executes `kubectl` locally from the `nmc` client process to manage the Refiner deployment lifecycle.
Ensure:
- `kubectl` is installed on the host running `nmc`.
- the current kubeconfig/context has permissions for `deployments`, `services`, `pods`, and optional `persistentvolumeclaims`.

**Authentication notes:**
- The server supports `NMC_AUTH_MODE=token` (shared secret) and `NMC_AUTH_MODE=oidc` (bearer token introspection).
- The CLI sends bearer tokens when provided. It checks `NMC_OIDC_ACCESS_TOKEN`, `NMC_BEARER_TOKEN`, the active connection token, then `NMC_AUTH_TOKEN`.
- Connection tokens are stored in `~/.nmc/config.json` with hardened permissions.
- For OIDC audiences, use `NMC_OIDC_AUDIENCE` (single or comma-separated) or `NMC_OIDC_ALLOWED_AUDIENCES` to allow multiple client IDs (e.g., SPA + server).
- Compatibility aliases: `NM_AUTH_MODE`, `NM_AUTH_TOKEN`, and `NM_OIDC_*` are also accepted for shared SSO configuration across NeuralMimicry services.
- Optional hardening for node onboarding: set `NMC_RECRUIT_TOKEN` on the server, then supply `--recruit-token` (or `recruit_token`) when calling `node recruit`.
- Optional playbook override for post-recruit configuration: set `NMC_RECRUIT_ANSIBLE_PLAYBOOK` on recruiter hosts to define the default auto-configure playbook.
- Optional non-interactive privilege escalation for recruitment: set `NMC_SUDO_PASSWORD` (or alias `NMC_BECOME_PASSWORD`) on the CLI host when using `node recruit`.
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
ConnectionCommands.h / ConnectionCommands.cpp: For nmc connection related commands (e.g., connect, disconnect).
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
