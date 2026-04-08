# NMC Testing Strategy

This repository uses a mix of static contract tests and executable CLI tests to protect the command surface and server safety invariants.

## 1. Goals

The current test suite focuses on:
- client/server interface drift
- command-to-route coverage drift
- adaptive loop contract drift
- server guard and redaction regressions
- CLI payload serialization and failure behavior

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
- `server_safety_contract_test.py`
  - ensures guarded routes use the shared request guard
  - ensures sensitive endpoint prefixes remain excluded from body logging

### 2.2 Functional CLI tests

Location: `tests/functional/`

Current test file:
- `client_cli_integration_test.py`

Covered behaviors:
- Tracey analytics query serialization
- Tracey adaptive query serialization
- Tracey adaptive policy query serialization
- Tracey agent analysis query serialization
- Tracey control payload serialization
- Tracey heartbeat payload serialization
- invalid adaptive policy rejection before network execution
- invalid JSON rejection before network execution
- invalid flag rejection before network execution
- malformed upstream response handling

The functional suite runs the real `nmc` binary against a local mock HTTP server and inspects the emitted HTTP method, path, and body.

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

There is no lightweight server-runtime integration harness in this repo today. The server can still be built directly when you need compile coverage:

```bash
cmake -S nmc_server -B nmc_server/build
cmake --build nmc_server/build -j4
```

Because the server embeds the Kubernetes C client and fetches several dependencies at configure/build time, this path is heavier than the CLI build and is not currently part of the lightweight Python test suite.

## 4. How To Run

From the repository root:

```bash
python3 tests/contracts/api_route_contract_test.py
python3 tests/contracts/command_coverage_contract_test.py
python3 tests/contracts/tracey_adaptive_contract_test.py
python3 tests/contracts/server_safety_contract_test.py
python3 tests/functional/client_cli_integration_test.py
```

## 5. Recommended CI Gate

Minimum CI gate for this repository:
1. build `nmc_client`
2. run all contract tests
3. run the functional CLI mock-server test

Stronger gate when server dependencies are available:
4. build `nmc_server`

## 6. What These Tests Protect

- API route additions cannot silently drift between client and server.
- Registered commands cannot stop reaching the routes they are expected to cover.
- The Tracey adaptive loop cannot quietly lose its placement-policy controls, fallback synthesis, or dashboard-facing response keys.
- Sensitive endpoints cannot quietly lose request-body redaction.
- CLI query strings and JSON payloads are checked against real process execution rather than mocked command objects.

## 7. Current Gaps

- No automated end-to-end server-runtime test harness around a real Kubernetes cluster.
- No property/fuzz testing for CLI parsing.
- No automated coverage today for the local `kubectl` Refiner workflows or the SSH/SCP recruitment execution path.
- No executable end-to-end test today for the full Tracey assessment plan/report cycle against a live server.
- No persistence-focused tests for server restart behavior because several server-side stores are intentionally in-memory.
