# NMC Architecture

This document describes the runtime structure and control-plane boundaries of the NeuralMimicry Continuum codebase.

## 1. Runtime Surfaces

### 1.1 `nmc_client`

The CLI is responsible for:
- command discovery and help output
- global flag handling (`--help`, `--version`, `--output`)
- argument and flag validation before network or shell execution
- HTTP transport through `CloudAPIClient`
- local-only operational workflows such as direct `node recruit` and most `refiner` commands

Primary modules:
- `nmc_client/src/CLI/`: parser, command graph, flag normalization
- `nmc_client/src/Commands/`: top-level command families and subcommands
- `nmc_client/src/Core/CloudAPIClient.*`: HTTP client, connection persistence, auth header resolution
- `nmc_client/src/Models/`: shared request/response data structures

### 1.2 `nmc_server`

The server is responsible for:
- HTTP route registration and request guards
- Kubernetes-backed handlers for `k8s` and `vcluster`
- in-memory resource handlers for several CRUD-style surfaces
- OpenShift, OpenStack, and Proxmox portal proxying
- Tracey discovery, status polling, fleet views, adaptive loop synthesis, compromise-assessment brokering, and control forwarding
- built-in operator web docs when `NMC_DOCS_ENABLED=true`

Primary modules:
- `nmc_server/src/Core/APIRoutes.*`: route registration, guards, resource handlers, Tracey state
- `nmc_server/src/Core/K8sHandlers*`: Kubernetes and vcluster handlers
- `nmc_server/src/Core/OpenShiftClient.*`: OpenShift portal transport
- `nmc_server/src/Core/OpenStackClient.*`: OpenStack portal transport
- `nmc_server/src/Core/ProxmoxClient.*`: Proxmox portal transport
- `nmc_server/src/Core/OIDCValidator.*`: RFC 7662 bearer-token introspection
- `nmc_server/src/Core/TraceyCVEIntel.*`: Tracey CVE mirror state
- `nmc_server/src/docs/`: web documentation copied into the server build directory

### 1.3 Deployment and Support Assets

- `deploy.sh`: opinionated Ubuntu/systemd bootstrap for `nmc_server`
- `ansible/`: deployment playbooks, service templates, recruited-node automation
- `tests/`: static contract checks and CLI functional tests

## 2. Control-Plane Matrix

| Surface | Primary entrypoints | Backing implementation | Current state model |
|---|---|---|---|
| Local connection profiles | `nmc connection *` | `CloudAPIClient` + `~/.nmc/config.json` | persisted locally |
| Server-side connection registry | `nmc connection status|make|drop|list|select --server` | `/connections/*` routes | in-memory on the server |
| Bucket | `nmc bucket *` | `/bucket/*` routes | in-memory |
| SSH keys | `nmc ssh *` | `/ssh/*` routes | in-memory |
| VM | `nmc vm *` | `/vm/*` routes | in-memory plus Tracey requirement hooks |
| Model upload | `nmc model upload` | `/model/upload` | request/response only; no durable model registry in this repo |
| Kubernetes clusters | `nmc k8s *` | `/k8s/*` + `K8sHandlers` | Kubernetes-backed |
| Virtual clusters | `nmc vcluster *` | `/vcluster/*` + `K8sHandlers_VCluster_*` | Kubernetes-backed workload plus in-memory config registry |
| OpenShift portal | `nmc openshift *` | `/openshift/*` + `OpenShiftClient` | proxied to external portal |
| OpenStack portal | `nmc openstack *` | `/openstack/*` + `OpenStackClient` | proxied to external portal |
| Proxmox portal | `nmc proxmox *` | `/proxmox/*` + `ProxmoxClient` | proxied to external portal |
| Tracey | `nmc tracey *` | `/tracey/*` routes | in-memory server state with discovery, status polling, adaptive-loop synthesis, and assessment handoff |
| Refiner | `nmc refiner *` | local `kubectl` plus limited `/k8s/refiner/*` routes | local cluster operations or server passthrough |
| Node recruitment | `nmc node recruit` | direct SSH/SCP or `POST /node/recruit` | imperative execution with optional Ansible follow-up |

## 3. Request Lifecycle

### 3.1 CLI to API path

1. `CLIParser` normalizes global flags and resolves the command/subcommand path.
2. The selected command validates required arguments and flags.
3. The command either:
   - calls `CloudAPIClient`, or
   - executes a local workflow (`refiner`, direct `node recruit`).
4. `CloudAPIClient` resolves the active endpoint and bearer token, sends the HTTP request, and normalizes the response to the shared envelope.
5. The CLI prints human-readable output by default, or JSON/YAML when requested.

### 3.2 Server request path

1. `APIRoutes` registers public docs routes, the public `/health` route, and all guarded API routes.
2. The shared guard:
   - assigns or propagates `X-Request-ID`
   - enforces `NMC_MAX_BODY_BYTES`
   - applies token or OIDC auth unless auth is disabled
3. The route handler validates payload and path parameters.
4. The handler returns the common envelope: `success`, `message`, `data`.

## 4. Authentication and Identity

Server auth modes:
- `token`: shared secret via `Authorization: Bearer` or `X-NMC-Token`
- `oidc`: bearer token introspection through `OIDCValidator`
- `off`: explicit no-auth mode

Client token precedence:
1. `NMC_OIDC_ACCESS_TOKEN`
2. `NM_OIDC_ACCESS_TOKEN`
3. `NMC_BEARER_TOKEN`
4. `NM_BEARER_TOKEN`
5. active connection token in `~/.nmc/config.json`
6. `NMC_AUTH_TOKEN`
7. `NM_AUTH_TOKEN`

Request identity:
- every guarded route gets an `X-Request-ID`
- the server preserves a caller-supplied `X-Request-ID` when present

## 5. Feature-Specific Architecture Notes

### 5.1 VCluster

The vcluster implementation is split across:
- `K8sHandlers_VCluster_Advanced.cpp`
- `K8sHandlers_VCluster_Lifecycle.cpp`
- `K8sHandlers_VCluster_Monitoring.cpp`
- `Models/VClusterConfig.h`

Important distinctions:
- the server API supports advanced create-time configuration through a nested `config` JSON payload
- the CLI exposes lifecycle, config, and monitoring subcommands, but `nmc vcluster create` currently only sends `name` and optional `namespace`
- config metadata is stored in the server's in-memory `vclusterConfigsRef` map
- backups are stored as `ConfigMap` objects in the `vcluster-backups` namespace

### 5.2 Tracey

Tracey combines several mechanisms in `APIRoutes`:
- heartbeat ingestion (`/tracey/agents/heartbeat`)
- fleet summaries and analytics (`/tracey/agents`, `/tracey/fleet`, `/tracey/analytics`)
- adaptive plan/ramp/optimize/repeat synthesis (`/tracey/adaptive`)
- rack, server, GPU, and compromise views
- compromise-assessment fleet and per-agent handoff (`/tracey/assessment/fleet`, `/tracey/agents/{id}/assessment/plan`, `/tracey/agents/{id}/assessment/report`)
- control forwarding and deep-dive retrieval for individual agents
- optional local bootstrap requirement for a sidecar agent using `NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT`

The adaptive loop is built server-side from cached Tracey status snapshots. When newer agents expose `continuum_loop` data directly, Continuum consumes that state. When older agents do not, `APIRoutes` synthesizes a fallback loop from the available telemetry, assessment, TraceyGuard, loader-threat, and Slurm fields so the CLI and dashboards still get a stable control-plane contract.

Placement intent is operator-tunable through the `policy` query on `/tracey/adaptive` and `nmc tracey adaptive --policy ...`. The current built-in modes are:
- `balanced`
- `throughput`
- `risk`
- `energy`

That same adaptive contract drives the CLI, the built-in docs, and dashboard-facing Continuum operator views, so closed-loop behaviour is native to the existing control plane rather than a separate service.

Managed-resource enforcement is route-driven for several server-side create flows (`vm`, `k8s`, `openshift`, `openstack`, `proxmox`, `node`). The vcluster server path stores Tracey metadata in config/create responses but does not currently register the same managed-resource requirement hook.

### 5.3 Refiner

Refiner is intentionally hybrid:
- `deploy`, `logs`, and `remove` are local CLI workflows that shell out to `kubectl`
- `status` and `scale` can run locally or call server routes with `--server`

### 5.4 Built-In Docs

When enabled, `nmc_server` serves static docs from `./docs` in the server build directory. Public docs routes are intentionally unauthenticated.

## 6. State and Persistence

Persisted:
- CLI connection profiles and connection-scoped tokens in `~/.nmc/config.json`
- Kubernetes resources created in the target cluster
- vcluster backup ConfigMaps in the cluster

Non-persistent inside this repo's server process:
- bucket, SSH, VM, and several other CRUD-style resource collections
- server-side connection registry
- Tracey in-memory agent/requirement caches
- vcluster configuration registry (`vclusterConfigsRef`)

This means a server restart can drop some control-plane metadata even when underlying Kubernetes resources continue to exist.

## 7. Safety and Reliability Patterns

Implemented patterns include:
- request size limits
- structured error responses
- request-id propagation
- route-level auth guards
- body redaction for sensitive endpoints
- local CLI validation before network or shell execution
- connection health fallbacks (`/health`, then `/server/version`, then `/connections/status`)
- server startup release check and CLI `nmc version` release check

## 8. Current Boundaries

- Several resource families are demo-style or in-memory rather than durable services.
- The server build has a heavier dependency chain than the CLI because it embeds the Kubernetes C client.
- OpenShift, OpenStack, and Proxmox flows depend on external portal API contracts.
- Vcluster `config-update` stores new configuration metadata but does not hot-apply most changes to running workloads.
- Vcluster metrics are pod/state summaries, not full Prometheus resource telemetry.
