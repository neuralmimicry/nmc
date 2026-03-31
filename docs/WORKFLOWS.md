# NMC Workflows

This document describes the end-to-end workflows that the CLI and server follow, plus where resilience and best-practice behaviours are enforced.

**Top-Level Architecture**
- The `nmc_client` binary parses CLI input, validates required flags and arguments, and delegates to a shared `CloudAPIClient` instance.
- The `nmc_server` binary serves HTTP routes with in-memory models plus a Kubernetes-backed K8s integration.
- Client and server exchange JSON responses with a consistent `CloudResponse` envelope (`success`, `message`, `data`).

**CLI Parsing Workflow**
1. The CLI parser normalizes global flags like `--output` before command routing.
2. The parser resolves the first token to a registered root command or alias.
3. The parser walks subcommands until no further subcommand matches.
4. Flags are parsed and canonicalized to long names so `-r` and `--region` map to the same key.
5. Positional arguments are parsed in order and validated against required counts.
6. If validation passes, the command executes with parsed flags, arguments, and global flags.

**Command Execution Workflow**
1. Each command declares arguments, flags, and usage examples in its constructor.
2. `execute` validates required args and flags, then constructs the API request.
3. The command invokes `CloudAPIClient`, receives a `CloudResponse`, and prints output using the selected format.

**Version Update Check Workflow**
1. `nmc version` returns the local client version.
2. Unless `--no-check` is used, the CLI queries GitHub Releases (`neuralmimicry/nmc`) for the latest tag and reports whether an update is available.
3. `nmc_server` performs the same release check at startup and logs whether a newer release exists.
4. `GET /server/version` exposes server version and release-check results through the API.

**Connection Management Workflow**
1. The client loads `~/.nmc/config.json` at startup and keeps an in-memory list of connections.
2. `connection make` writes a new connection to disk and optionally sets it as default.
3. `connection select` marks one connection as active and updates the default in the config file.
4. The API client rebuilds its internal HTTP client when a default connection is set or changed.
5. `connection status` performs a lightweight health check on the active endpoint.
6. Health probing uses `GET /health` first, then falls back to `GET /server/version` and `GET /connections/status` for compatibility with older server builds when routes are unavailable or transport failures occur.
7. If the endpoint responds with `401` or `403`, the client reports the connection as reachable with authentication required.
8. Connection-level bearer tokens are persisted in `~/.nmc/config.json` and can be updated with `connection set-token` or removed with `connection clear-token`.
9. The API client resolves the active token in this order: `NMC_OIDC_ACCESS_TOKEN`, `NMC_BEARER_TOKEN`, the active connection token, then `NMC_AUTH_TOKEN`.
10. When a token is present, the client sends `Authorization: Bearer <token>` on all requests.

**Server Request Workflow**
1. HTTP routes are registered in `APIRoutes` and dispatch to handler functions.
2. Each handler validates request body or path parameters and returns a JSON envelope.
3. Shared in-memory resources are guarded by a mutex for thread safety.
4. Errors return structured responses with appropriate HTTP status codes.
5. Optional auth, request IDs, and payload limits are enforced before handlers run.
6. When `NMC_AUTH_MODE=oidc`, bearer tokens are validated via the configured introspection endpoint.

**OpenShift Portal Workflow**
1. The NMC server uses `NMC_OSHIFT_API_URL` (default `http://127.0.0.1:8000`) to connect to the oshift portal API.
2. `/openshift/resources` and `/openshift/clusters` proxy read-only queries to the portal.
3. `/openshift/clusters/request` validates required fields and forwards provisioning requests.
4. The portal API orchestrates provider-specific flows (ROSA, ARO, GCP, OpenStack, on-prem, hybrid-burst).
5. The CLI `openshift status` command can poll cluster status by repeatedly calling the proxy until the `--until` target or timeout.
6. OpenShift cluster statuses are normalized to common NMC states: `Pending`, `Provisioning`, `Ready`, `Failed`, `Unknown`.

**OpenStack Portal Workflow**
1. The NMC server uses `NMC_OPENSTACK_API_URL` (default: inherits `NMC_OSHIFT_API_URL`) to connect to the OpenStack orchestration API.
2. `/openstack/resources` and `/openstack/clusters` proxy read-only queries to the backend.
3. `/openstack/clusters/request` validates required request fields and forwards provisioning requests.
4. The CLI `openstack status` command polls cluster status until the requested `--until` state or timeout.
5. OpenStack status values are normalized to common NMC states: `Pending`, `Provisioning`, `Ready`, `Failed`, `Unknown`.

**Kubernetes Integration Workflow**
1. Server startup loads K8s configuration from `--kubeconfig`, `--config`, environment, or `~/.nmc/config.json`.
2. A background thread periodically checks K8s health and logs changes.
3. K8s handlers use the C client to list nodes and custom resources.
4. K8s API responses are normalized into `K8sCluster` models before returning to clients.

**Refiner Lifecycle Workflow (Continuum CLI)**
1. `nmc refiner deploy` validates namespace/context/manifest inputs and creates the target namespace if needed.
2. The command applies the Kubernetes manifest with `kubectl apply -f` and waits for deployment rollout completion.
3. `nmc refiner status` fetches deployment/service/pod state and normalizes replica health (`desired`, `ready`, `available`, `unavailable`).
4. `nmc refiner scale` updates deployment replicas and waits for rollout convergence.
5. `nmc refiner logs` provides operational observability from `deployment/refiner`.
6. `nmc refiner remove` deletes deployment resources, optionally including persistent storage PVC cleanup.

**Node Recruitment Workflow**
1. `nmc node recruit` validates host/user/path inputs and builds a recruitment payload.
2. In API mode, the CLI sends `POST /node/recruit` to `nmc_server`; script content is uploaded and binary/script paths are interpreted on the server host.
3. In direct mode (`--direct`), the CLI executes `ssh/scp` locally to transfer and run the script/binary on the target.
4. The recruiter host prepares the remote path, transfers the artifact, then executes with environment variables for node type, region, tenant metadata, optional Continuum token propagation, and optional Tracey metadata.
   - If `sudo_password` (or `--sudo-password`/`--sudo-password-file`) is supplied, execution uses non-interactive `sudo -S -p ''`; otherwise it uses `sudo -E` or no sudo depending on configuration.
5. If `--auto-configure`/`auto_configure=true` is enabled, recruiter hosts run `ansible-playbook` after execution:
   - Playbook path resolution order: explicit request path, `NMC_RECRUIT_ANSIBLE_PLAYBOOK`, then `ansible/recruited-node.yml`.
   - Capability flags (`apps`, `vm`, `podman`, `kubernetes`, `openstack`) are merged into Ansible vars (`nmc_enable_*`) with tenant/node context.
   - Ansible privilege escalation defaults to enabled (`ansible_become=true` / `--no-become` to disable), and when a sudo/become password is supplied it is mapped into `ansible_become_password`.
6. When `tracey.agent_id` is supplied, the server records a managed node requirement and marks compliance as pending until Tracey reporting is observed.

**Resilience Practices Implemented**
- 2xx responses are treated as successful; non-2xx responses flow through the error path.
- Connection state is persisted to disk after any mutation to avoid drift.
- Response parsing falls back to raw body data when JSON parsing fails.
- Default behaviours are explicit when configuration files are missing.
