# NMC Testing Strategy

This repository uses a mix of static contract tests and executable CLI/server tests to protect the command surface, server safety invariants, and release workflow assumptions.

## 1. Goals

The current test suite focuses on:
- client/server interface drift
- command-to-route coverage drift
- adaptive loop contract drift
- Tracey network simulation and CLI simulation drift
- recruited-node capacity contract drift
- server guard, redaction, and route authorisation regressions
- CLI payload serialisation and failure behaviour

## 2. Test Layers

### 2.1 Contract tests

Location: `tests/contracts/`

Current tests:
- `api_route_contract_test.py`
  - extracts client endpoints from `CloudAPIClient.cpp`
  - extracts server routes from `APIRoutes.cpp`
  - fails if either side exposes routes the other side cannot address
- `command_coverage_contract_test.py`
  - walks the command graph from `nmc_client/src/main.cpp`
  - maps command implementations to `CloudAPIClient` methods
  - verifies that addressable server routes remain reachable from at least one registered CLI path
- `tracey_adaptive_contract_test.py`
  - ensures `GET /tracey/adaptive` stays registered on the server
  - checks that fallback loop synthesis, placement-policy selection, and the stable response envelope remain wired in
  - verifies that the adaptive summary still exposes the keys required by the CLI and dashboard surfaces
- `tracey_cli_simulation_contract_test.py`
  - ensures Tracey CLI simulation flags, validation, and client query wiring remain aligned
- `tracey_network_simulation_contract_test.py`
  - ensures Tracey network expansion payloads and dashboard simulation controls remain wired end to end
- `node_recruit_capacity_contract_test.py`
  - ensures recruited-node capacity and Tracey metadata remain represented in the node recruitment flow
- `server_safety_contract_test.py`
  - ensures guarded routes use the shared request guard
  - ensures sensitive endpoint prefixes remain excluded from body logging

### 2.2 Functional CLI tests

Location: `tests/functional/`

Current test file:
- `client_cli_integration_test.py`

Covered behaviours:
- Tracey analytics query serialisation
- Tracey adaptive query serialisation
- Tracey adaptive policy query serialisation
- Tracey agent analysis query serialisation
- Tracey control payload serialisation
- Tracey heartbeat payload serialisation
- invalid adaptive policy rejection before network execution
- invalid JSON rejection before network execution
- invalid flag rejection before network execution
- malformed upstream response handling
- AARNN runtime resource reads use a dedicated Continuum observe route and reach only the fixed upstream `GET /api/runtime/resources` endpoint

The functional suite runs the real `nmc` binary against a local mock HTTP server and inspects the emitted HTTP method, path, and body.

### 2.3 Functional server authorisation tests

Location: `tests/functional/`

Current test file:
- `server_authz_integration_test.py`

Covered behaviours:
- `/auth/session` central-session identity normalisation
- static admin-token fallback for protected server routes
- Continuum observe/control route authorisation
- real-server IPMI diagnostics and controller actions against a fake `ipmitool` executable, including password/argv separation, exact target and command selection, preflight state mismatch, symlink rejection, and writable-directory rejection
- real-server Turing Pi diagnostics and controller actions against a local HTTPS BMC mock, including certificate verification, slot scoping, unsupported warm-restart rejection, explicit action acknowledgement, and post-action power-state verification
- Tracey observe/use/control route authorisation
- AARNN observe/use/control route authorisation
- denied Tracey and AARNN control/runtime requests do not reach guarded upstream targets

This harness starts a real `nmc_server` process, points it at local mock HTTP dependencies, and validates the shared `service_access` contract end to end without requiring a live Kubernetes cluster.

### 2.4 Continuum dashboard browser safety tests

Location: `tests/functional/`

Run `node tests/functional/dashboard_device_safety_browser_test.mjs` to exercise the monitoring dashboard in headless Firefox through geckodriver. The local fixture server supplies authenticated inventory and Tracey responses, then covers stale inventory, inventory HTTP 503, Tracey HTTP 503, the combined network graph/table with an exact Tracey match, and ambiguous Tracey identity handling. The tests check visible warning/empty states, graph and table drilldown, device-specific DHCP evidence, mapped controller diagnostics, and that no controller mutation request is sent without server-issued preflight.

The canonical preflight runs these browser tests when Node.js, Firefox and geckodriver are available; other build hosts report a clear skip. Set `FIREFOX_BIN` or `WEBDRIVER_BIN` to override executable discovery. Set `NMC_BROWSER_PROFILE_ROOT` when a confined browser such as Firefox Snap requires profiles to live in a specific writable directory. These tests use mock API responses and do not validate production credentials or live inventory contents.

## 3. Build Validation

### 3.1 CLI build

The functional suite requires a built CLI binary.

```bash
cmake -S nmc_client -B nmc_client/build
cmake --build nmc_client/build -j4
```

Binary resolution order in the functional test is:
1. `NMC_BIN`
2. `nmc_client/build/nmc`

### 3.2 Server build

The functional server-authorisation harness requires a built server binary:

```bash
cmake -S nmc_server -B nmc_server/build
cmake --build nmc_server/build -j4
```

Binary resolution order in the server functional test is:
1. `NMC_SERVER_BIN`
2. `nmc_server/build/nmc_server`

The harness runs without a live Kubernetes cluster, but the build path is still heavier than the CLI because `nmc_server` embeds the Kubernetes C client and fetches several dependencies at configure/build time.

## 4. How To Run

From the repository root:

```bash
bash scripts/preflight.sh
```

For faster client-only local checks:

```bash
bash scripts/preflight.sh --skip-server
```

## 5. Recommended CI Gate

Minimum CI gate for this repository:
1. derive version metadata with `scripts/derive-version.sh`
2. run `scripts/preflight.sh --ci` on self-hosted Linux `X64` and `ARM64`
3. package client artifacts for Linux, Windows, and macOS across `amd64` and `arm64`
4. package server artifacts for Linux `amd64` and `arm64`
5. create the immutable release tag only after all packaging jobs succeed

## 6. What These Tests Protect

- API route additions cannot silently drift between client and server.
- Registered commands cannot stop reaching the routes they are expected to cover.
- The Tracey adaptive loop cannot quietly lose its placement-policy controls, fallback synthesis, or dashboard-facing response keys.
- Sensitive endpoints cannot quietly lose request-body redaction.
- CLI query strings and JSON payloads are checked against real process execution rather than mocked command objects.
- Shared `service_access` authorisation is validated end to end for Continuum, Tracey, and AARNN routes.
- Denied Tracey and AARNN guarded requests are checked to ensure they do not reach protected upstream targets.
- Release tags are generated from the same build version compiled into the client and server binaries.

## 7. Current Gaps

- No automated end-to-end server-runtime test harness around a real Kubernetes cluster; the current server harness uses a real `nmc_server` process with mock upstreams.
- No property/fuzz testing for CLI parsing.
- No automated coverage today for the local `kubectl` Refiner workflows or the SSH/SCP recruitment execution path.
- No executable end-to-end test today for the full Tracey assessment plan/report cycle against a live multi-agent deployment; the current server harness covers route authorisation and guarded forwarding only.
- Dashboard browser behaviour is exercised against local mock APIs, including its combined inventory/Tracey overview; no automated browser test currently authenticates to production or exercises live device management.
- No persistence-focused tests for server restart behaviour because several server-side stores are intentionally in-memory.
