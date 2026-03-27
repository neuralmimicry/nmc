# NMC Testing Strategy and Quality Verification

This document maps tests to the quality attributes required for Continuum operations:
- precision
- accuracy
- functional correctness
- safety
- resilience
- robustness

## 1. Test Layers

## 1.1 Contract Tests (`tests/contracts/`)

Purpose:
- ensure client/server API route parity and prevent integration drift
- enforce static security guard invariants in route registration

Current tests:
- `api_route_contract_test.py`
- `command_coverage_contract_test.py`
- `server_safety_contract_test.py`

## 1.2 Functional CLI Tests (`tests/functional/`)

Purpose:
- validate real CLI behavior against a controlled mock HTTP service
- verify request paths, query serialization, payload formation, and failure handling

Current tests:
- `client_cli_integration_test.py`

## 1.3 Build Validation

Purpose:
- ensure command and transport surface compiles end-to-end

Commands:
- `cmake -S nmc_client -B nmc_client/build`
- `cmake --build nmc_client/build -j4`

## 2. Quality Attribute Coverage Matrix

| Quality Attribute | Verification Mechanism | Notes |
|---|---|---|
| Precision | Route contract parity test | Detects any client/server path divergence |
| Accuracy | Functional query + payload assertions | Checks exact path/query/body emitted by CLI |
| Function | Functional command execution against mock server | Confirms command workflows succeed/fail correctly |
| Safety | Static safety contract checks | Ensures guarded routes + sensitive log redaction policy |
| Resilience | Negative-path functional tests | Invalid JSON/flags fail safely before unsafe calls |
| Robustness | Error-shape handling in integration tests | Verifies non-JSON and upstream failures are surfaced predictably |

## 3. How to Run

From repository root:

```bash
python3 tests/contracts/api_route_contract_test.py
python3 tests/contracts/command_coverage_contract_test.py
python3 tests/contracts/server_safety_contract_test.py
python3 tests/functional/client_cli_integration_test.py
```

If client binary is not already built:

```bash
cmake -S nmc_client -B nmc_client/build
cmake --build nmc_client/build -j4
```

Functional test binary resolution order:
1. `NMC_BIN` environment variable
2. `nmc_client/build/nmc`

## 4. CI Recommendation

Minimum CI gate:
1. Build `nmc_client`
2. Run all contract tests
3. Run functional CLI mock-server test

This catches:
- interface regressions
- safety control regressions
- payload serialization regressions

## 5. Future Hardening Opportunities

- Add server-runtime integration tests once a stable test harness for Kubernetes C dependencies is available.
- Add fuzz/property tests for CLI flag parsing and recruitment payload normalization.
- Add mutation tests around auth guard and redaction logic.
