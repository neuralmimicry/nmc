# NMC CLI Application (NeuralMimicry Continuum)

[![Build and Release](https://github.com/neuralmimicry/nmc/actions/workflows/build-and-release.yml/badge.svg)](https://github.com/neuralmimicry/nmc/actions/workflows/build-and-release.yml)

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
* Built-in GitHub release update checks for both client (`nmc version`) and server (`GET /server/version`).
* Connection health probing via `GET /health` with compatibility fallbacks for older server versions.
* CI contract tests to ensure route parity and command-level coverage (`server route -> CloudAPI -> CLI command`) remain aligned.
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

`nmc connection status`: Reports the active connection, probe path, HTTP status, and diagnostic state (`healthy`, `reachable_auth_required`, `reachable_unknown_health`, `unhealthy`).

`nmc connection make <NAME> <URL> [--default] [--token <token>]`: Creates a connection profile and can set it as default immediately.

`nmc connection drop <NAME>`: Removes a connection profile. If it was active, no default connection remains.

`nmc connection list`: Lists configured connections and active/default state.

`nmc connection select <NAME>`: Sets an existing connection as the active default.

`nmc connection unset-default`: Clears the default connection and reverts to built-in host/port.

* **Set a bearer token for a connection:**
    ```bash
    ./nmc connection make prod https://continuum.example --token "$OIDC_ACCESS_TOKEN" --default
    ./nmc connection set-token prod --token "$OIDC_ACCESS_TOKEN"
    ./nmc connection clear-token prod
    ```

* **Inspect server health and version metadata:**
    ```bash
    ./nmc server health
    ./nmc server version
    ```

* **Query server-side connection state from the API control plane:**
    ```bash
    ./nmc connection status --server
    ./nmc connection list --server
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

* **Display version without remote release check (offline-safe):**
    ```bash
    ./nmc version --no-check
    ```

* **Tracey telemetry and control workflows:**
    ```bash
    ./nmc tracey agents
    ./nmc tracey analytics --window-seconds 3600 --bucket-seconds 60
    ./nmc tracey analysis tracey-1 --window-seconds 7200
    ./nmc tracey control tracey-1 --action clear_quarantine --reason maintenance
    ./nmc tracey heartbeat --agent-id tracey-1 --status healthy --metrics '{"gpu_util":0.91}'
    ./nmc tracey deepdive tracey-1
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

**Workflows and Behaviour Notes:**

Detailed end-to-end workflows, including CLI parsing, connection management, and server request lifecycles, are documented in:

- `docs/WORKFLOWS.md`
- `docs/ARCHITECTURE.md`
- `docs/SECURITY.md` (ISO 27001 / SOC 2 readiness and operational controls)
- `docs/TESTING.md`
- `tests/contracts/api_route_contract_test.py` (client/server API route contract check)
- `tests/contracts/command_coverage_contract_test.py` (server route -> CloudAPI -> CLI command coverage check)
- `tests/contracts/server_safety_contract_test.py` (guard/redaction safety invariants)
- `tests/functional/client_cli_integration_test.py` (CLI behavior vs mock server)

**OpenShift Integration Notes:**

The server proxies OpenShift provisioning and visibility requests to the NeuralMimicry OpenShift portal API (oshift).
Set the OpenShift portal base URL with the `NMC_OSHIFT_API_URL` environment variable when starting `nmc_server`.
The default is `http://127.0.0.1:8000`.
Server release status is available at `GET /server/version` and includes current version, latest release tag, and update availability.
Server health status is available at `GET /health` for lightweight connectivity checks.

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
**Codebase Components:**
- `nmc_client/src/main.cpp`: CLI entry point and command registration.
- `nmc_client/src/CLI/`: command tree, parser, and global flag handling.
- `nmc_client/src/Commands/`: domain commands (`bucket`, `k8s`, `vcluster`, `vm`, `ssh`, `model`, `node`, `openshift`, `tracey`, `server`, `refiner`, `connection`, `version`).
- `nmc_client/src/Core/CloudAPIClient.*`: HTTP transport, response normalisation, connection profile management, and token resolution.
- `nmc_client/src/Models/`: request/response models used by command handlers.
- `nmc_server/src/Core/APIRoutes.*`: route registration, auth/payload guardrails, and handler implementations.
- `nmc_server/src/Core/K8sHandlers.*`: Kubernetes integration and cluster-oriented handlers.
- `nmc_server/src/docs/`: built-in API documentation pages served by `nmc_server` when docs are enabled.

**Engineering Principles in This Repository:**
- Modular command structure with a clear separation between parsing, command orchestration, and transport.
- Shared JSON response envelope (`success`, `message`, `data`) across client and server.
- Defensive request validation for security-sensitive flows (for example `node recruit` and token-bearing endpoints).
- Deterministic connection behaviour with persisted profiles and explicit health probe diagnostics.
