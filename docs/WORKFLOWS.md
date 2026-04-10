# NMC Workflows

This document summarizes the end-to-end workflows implemented by the CLI, the server, and the deployment automation.

## 1. CLI Parsing and Output

1. `CLIParser` removes global flags from the argument stream before command dispatch.
2. Global flags currently implemented are:
   - `--help` / `-h`
   - `--version` / `-V`
   - `--output` / `-x` with default human-readable output, `json`, or `yaml`
3. Root commands are resolved from the registry built in `nmc_client/src/main.cpp`.
4. Subcommands are traversed until no deeper match exists.
5. Final-command flags are normalized to long names, so short and long flags map to the same lookup key.
6. Commands validate arguments before network or shell execution.

## 2. Connection Management

### 2.1 Local profile workflow

1. `CloudAPIClient` loads `~/.nmc/config.json` on startup.
2. `connection make` adds a local endpoint and optional token.
3. `connection select` changes the default endpoint.
4. `connection unset-default` clears the default and falls back to `127.0.0.1:8080`.
5. `connection set-token` and `connection clear-token` mutate the persisted per-connection token.
6. Any change is written back to disk and the file permissions are reset to owner read/write only.

### 2.2 Server-side registry workflow

The following commands accept `--server` and operate on `/connections/*` instead of the local config file:
- `connection status`
- `connection make`
- `connection drop`
- `connection list`
- `connection select`

The server-side registry is in-memory and does not currently expose token persistence or `unset-default`.

### 2.3 Connection health probe workflow

1. `nmc connection status` probes `GET /health` first.
2. If `/health` is unavailable, it falls back to `GET /server/version`.
3. If that also fails for compatibility reasons, it falls back to `GET /connections/status`.
4. `401` and `403` responses are reported as reachable endpoints that require authentication.

## 3. Server Guard Workflow

All guarded routes follow the same sequence:
1. ensure or generate `X-Request-ID`
2. reject over-sized bodies with `413`
3. apply token or OIDC auth unless auth is disabled
4. dispatch to the route handler
5. return the standard JSON envelope

Public routes:
- `/health`
- docs routes (`/`, `/docs`, `/login`, legacy `/services/health/monitoring*`) when docs are enabled

## 4. Kubernetes and VCluster Workflows

### 4.1 Kubernetes cluster workflow

1. `nmc k8s *` commands call `/k8s/*` routes.
2. `K8sHandlers` reads cluster state through the Kubernetes C client.
3. `k8s health` maps to `/k8s/healthz`.
4. `k8s suspend` and `k8s resume` drive server-side lifecycle operations.
5. `k8s get-config` retrieves kubeconfig material for a cluster.

### 4.2 VCluster workflow

Basic CLI workflow:
1. `vcluster create` sends `name` and optional `namespace`.
2. `vcluster list/get/get-config` inspect the created virtual cluster.
3. Lifecycle commands (`pause`, `resume`, `backup`, `restore`, `upgrade`) operate over dedicated `/vcluster/*` routes.
4. Monitoring/config commands (`config-get`, `config-update`, `metrics`, `health`, `resources`) query or update vcluster metadata.

Server-side advanced workflow:
1. `POST /vcluster/create` accepts an optional nested `config` JSON payload.
2. The server stores a `VClusterConfig` object in memory.
3. The handler creates:
   - a namespace
   - a ServiceAccount and Role/RoleBinding when enabled
   - a StatefulSet
   - a Service
   - an optional Ingress
   - an optional PodDisruptionBudget
4. `backup` writes the stored config into a `ConfigMap` in `vcluster-backups`.
5. `restore` recreates a vcluster from that saved config.
6. `config-update` replaces stored config metadata but does not fully hot-apply the change set to a running vcluster.

## 5. Provider Portal Workflows

### 5.1 OpenShift

1. The server uses `NMC_OSHIFT_API_URL` for portal communication.
2. `openshift resources` and `openshift clusters` proxy read operations.
3. `openshift request` validates the provisioning payload and forwards it.
4. `openshift status` polls cluster state until the requested target status or timeout.
5. Portal statuses are normalized to `Pending`, `Provisioning`, `Ready`, `Failed`, or `Unknown`.

### 5.2 OpenStack

1. The server uses `NMC_OPENSTACK_API_URL` or falls back to `NMC_OSHIFT_API_URL`.
2. `openstack resources` and `openstack clusters` proxy read operations.
3. `openstack request` validates and forwards the provisioning payload.
4. `openstack status` performs the same poll/until workflow as OpenShift.
5. Status values are normalized to the same common state set.

### 5.3 Proxmox

1. The server uses `NMC_PROXMOX_API_URL` or falls back to `NMC_OPENSTACK_API_URL`.
2. `proxmox resources` and `proxmox clusters` proxy read operations.
3. `proxmox request` validates and forwards the provisioning payload.
4. `proxmox status` performs the same poll/until workflow as OpenShift and OpenStack.
5. Status values are normalized to the same common state set.

## 6. Tracey Workflows

### 6.1 Ingestion and inventory

1. `tracey heartbeat` submits an agent heartbeat payload to `/tracey/agents/heartbeat`.
2. `tracey agents`, `tracey fleet`, `tracey racks`, and `tracey rack` inspect the server's fleet view.
3. The server can bootstrap a required local Tracey sidecar entry using `NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT`, `NMC_TRACEY_LOCAL_AGENT_ID`, and `NMC_TRACEY_LOCAL_STATUS_ADDR`.

### 6.2 Adaptive plan/ramp/optimize/repeat workflow

- `tracey adaptive` queries `/tracey/adaptive` and returns the always-on fleet control loop used by Continuum dashboards and operator workflows.
- `tracey adaptive --policy balanced|throughput|risk|energy` applies an operator-selected placement bias before the server ranks placement candidates and recommendations.
- The server merges per-agent `continuum_loop` snapshots when agents expose them directly.
- For older agents, the server synthesizes a fallback loop from the available telemetry, assessment, TraceyGuard, loader-threat, and Slurm state so the response contract stays stable.
- The response is organised around:
  - `summary` for overall mode, scores, policy, counts, and next action
  - `plan`, `ramp`, `optimize`, and `repeat` for normalized phase state
  - `recommendations`, `placement_candidates`, `gpu_candidates`, and `recent_actions` for the next scheduling cycle

### 6.3 Analytics and compromise-assessment views

- `tracey analytics` queries fleet-wide analytics with optional `window-seconds`, `bucket-seconds`, and `log-limit` parameters.
- `tracey analysis` does the same for a single agent.
- `tracey cve` exposes CVE mirror status.
- `tracey assessment` exposes fleet-wide compromise posture and assessment progress.
- `tracey assessment-plan AGENT_ID` fetches the server-generated work assignment for one agent.
- `tracey assessment-report AGENT_ID --payload JSON` submits a slice report back to the same assessment cycle.
- `tracey compromise` exposes compromise posture summaries for one agent.
- `tracey server` and `tracey gpu` inspect deeper telemetry views.

### 6.4 Control and deep-dive

1. `tracey control` submits a JSON control payload or `action`/`reason` fields.
2. `tracey deepdive` requests detailed diagnostics from the selected agent.
3. The server validates agent endpoints before proxying status, control, or deep-dive requests.
4. Tracey networking can be hardened with settings such as `NMC_TRACEY_ALLOW_PUBLIC_ADDR`, `NMC_TRACEY_TLS_VERIFY`, and `NMC_TRACEY_REQUIRE_SIGNATURE`.

### 6.5 Managed-resource tracking

The server records Tracey requirements for several create/recruit flows so fleet compliance can be compared against expected managed resources. This is wired for:
- `k8s`
- `vm`
- `openshift`
- `openstack`
- `proxmox`
- `node recruit`
- local server bootstrap sidecar requirement

The vcluster server create path currently stores Tracey metadata in its config/response but does not register the same managed-resource requirement.

## 7. Node Recruitment Workflow

### 7.1 Shared validation

Before execution, `node recruit` validates:
- host, user, port, node type
- local file paths for script, binary, SSH key, and password files
- tenant metadata
- requested capabilities
- Ansible extra-var keys and values
- password-file consistency and environment fallback rules

### 7.2 API mode

1. The CLI builds a JSON payload and submits `POST /node/recruit`.
2. In script mode, the CLI uploads the script contents.
3. In binary mode, the binary path is interpreted on the server host.
4. The server performs SSH/SCP steps and optional Ansible follow-up.
5. Sensitive values are redacted from diagnostics before they are returned.

### 7.3 Direct mode

1. `--direct` keeps execution on the CLI host.
2. The CLI performs SSH/SCP directly to the target host.
3. `--dry-run` prints the generated commands without executing them.
4. `--no-sudo` disables `sudo` for remote artifact execution.

### 7.4 Auto-configure workflow

When `--auto-configure` is enabled:
1. Ansible playbook resolution order is:
   - explicit `--ansible-playbook`
   - `NMC_RECRUIT_ANSIBLE_PLAYBOOK`
   - `ansible/recruited-node.yml`
2. Capabilities are mapped into `nmc_enable_*` Ansible vars.
3. `--no-become` disables Ansible privilege escalation.
4. `--sudo-password`, `--sudo-password-file`, `NMC_SUDO_PASSWORD`, or `NMC_BECOME_PASSWORD` can provide a non-interactive become password.
5. If Tracey metadata is supplied, the server registers a managed-node requirement for later compliance checks.

## 8. Refiner Workflow

### 8.1 Local mode

1. `refiner deploy` validates the manifest path, ensures the namespace exists, runs `kubectl apply`, and waits for rollout completion.
2. `refiner status` fetches deployment, pod, and service state and summarizes replica health.
3. `refiner scale` applies a scale change and waits for rollout completion.
4. `refiner logs` fetches recent logs from `deployment/refiner`.
5. `refiner remove` deletes deployment resources and optionally the `refiner-job-data` PVC.

### 8.2 Server-backed mode

`refiner status --server` and `refiner scale --server` call dedicated `/k8s/refiner/*` routes instead of running local `kubectl`.

## 9. Version Check Workflow

- `nmc version` reports the local client version and, unless `--no-check` is used, checks the latest GitHub release.
- `nmc_server` performs the same release check at startup.
- `GET /server/version` exposes the server version, release-check status, and latest-version metadata through the API.
