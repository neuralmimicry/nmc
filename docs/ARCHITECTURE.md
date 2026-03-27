# NMC Architecture, Intent, and Workflow

This document describes the intent, goals, runtime architecture, and critical workflows of the NeuralMimicry Continuum (`nmc`) codebase.

## 1. Product Intent and Goals

The project delivers a Continuum control-plane and operator CLI for managing:
- storage resources (`bucket`)
- Kubernetes and virtual clusters (`k8s`, `vcluster`)
- virtual machines (`vm`)
- SSH keys (`ssh`)
- model registry uploads (`model`)
- OpenShift portal orchestration (`openshift`)
- Tracey fleet telemetry/control (`tracey`)
- node onboarding and post-provisioning (`node recruit`)

Primary goals:
- provide a single, operator-friendly CLI surface
- keep transport contracts deterministic between client and server
- enforce strong validation and safety controls on privileged operations
- keep stateful behavior observable (structured responses, logging, health probes)

## 2. Runtime Components

### 2.1 `nmc_client` (CLI)

Key modules:
- `src/CLI/`: parser, command tree, global flags
- `src/Commands/`: command handlers per domain
- `src/Core/CloudAPIClient.*`: HTTP transport + auth header management
- `src/Models/`: canonical response/data structures

Execution model:
1. Parse root command, subcommands, flags, positional args.
2. Validate command inputs.
3. Build API payloads or execute local operational logic (`refiner`, direct `node recruit`).
4. Print normalized output in human/json/yaml format.

### 2.2 `nmc_server` (HTTP API)

Key modules:
- `src/Core/APIRoutes.*`: route registration, auth/body guards, resource handlers
- `src/Core/K8sHandlers*`: Kubernetes-backed cluster/vcluster logic
- `src/Core/OpenShiftClient.*`: portal proxy client
- `src/Core/OIDCValidator.*`: bearer token introspection
- `src/Models/`: API envelope and resource models

Execution model:
1. Initialize auth mode, version checks, K8s client state, route table.
2. For guarded routes, enforce:
   - request id propagation
   - payload size limits
   - token/OIDC auth
3. Dispatch to domain handler.
4. Return normalized JSON envelope (`success`, `message`, `data`).

### 2.3 Supporting Assets

- `ansible/`: post-recruit automation playbooks and templates
- `nmc_server/src/docs/`: built-in operator web documentation
- `tests/`: contract and functional validation scripts

## 3. Core Control-Flow Workflows

## 3.1 CLI to API Request Path

1. User invokes `nmc <command> ...`.
2. `CLIParser` resolves command path and normalizes flags.
3. Command class builds payload and calls `CloudAPIClient`.
4. `CloudAPIClient` applies bearer headers and executes HTTP request.
5. Server validates request through route guard, executes handler, returns JSON.
6. CLI prints response or an actionable error message.

## 3.2 Authentication Workflow

Server supports:
- `token` mode (`NMC_AUTH_MODE=token`)
- `oidc` mode (`NMC_AUTH_MODE=oidc`)
- `off` mode (explicitly disables auth)

Client token resolution order:
1. `NMC_OIDC_ACCESS_TOKEN`
2. `NM_OIDC_ACCESS_TOKEN`
3. `NMC_BEARER_TOKEN`
4. `NM_BEARER_TOKEN`
5. active connection token (if configured)
6. `NMC_AUTH_TOKEN`
7. `NM_AUTH_TOKEN`

## 3.3 Connection Management Workflow

The client supports two independent connection planes:
- local CLI profile store (`~/.nmc/config.json`)
- server-side in-memory connection routes (`/connections/*`) via `--server`

This split allows:
- local endpoint switching per operator workstation
- remote server connection simulation/inspection

## 3.4 Node Recruitment Workflow

Two operating modes:
- API mode: CLI posts to `POST /node/recruit`, server executes SSH/SCP + optional Ansible
- direct mode: CLI performs SSH/SCP and optional Ansible locally

Safety-critical controls:
- host/user/path validation
- strict path constraints
- capability normalization and allow-list checks
- optional recruit token enforcement
- diagnostics redaction for sensitive values

## 3.5 Tracey Workflow

Tracey endpoints provide:
- liveness ingestion (`heartbeat`)
- fleet inventory and compliance summaries (`agents`)
- analytics timelines (`analytics`)
- per-agent analysis (`analysis`)
- remote control plane forwarding (`control`)
- deep inspection snapshots (`deepdive`)

Server applies network and endpoint validation before control/deep-dive proxying.

## 4. Data and Contract Model

All API operations are returned as:
- `success: bool`
- `message: string`
- `data: object|array|string|null`

Design effects:
- consistent UX across commands
- easier automation consumers (stable envelope)
- contract testability via static route mapping

## 5. Reliability and Safety Posture

Implemented robustness patterns:
- request size caps
- structured error responses
- route-level auth guarding
- selective body redaction in logs
- connection status fallback probing (`/health` -> `/server/version` -> `/connections/status`)
- CLI validation before network calls for many error classes

## 6. Current Boundaries and Known Constraints

- Server state for many resources is in-memory and non-persistent.
- Full server integration tests are dependency-heavy due Kubernetes C client toolchain.
- OpenShift behavior depends on an external portal API contract.

## 7. High-Confidence Operational Workflow

Recommended operator sequence:
1. Configure connection + token (`nmc connection make ...`).
2. Check server liveness/metadata (`nmc server health`, `nmc server version`).
3. Manage resources (`bucket/k8s/vcluster/vm/ssh/model`).
4. Onboard nodes (`nmc node recruit`), then inspect Tracey health.
5. Use analytics/controls for Tracey-managed fleet operations.
