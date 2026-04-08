# Compliance Posture

This repository contains technical controls that support ISO 27001 and SOC 2 style control objectives, but the repository alone does not provide certification or audit evidence.

## 1. What The Codebase Already Provides

Implemented controls in the current codebase include:
- configurable API authentication (`token`, `oidc`, `off`)
- request-id propagation for traceability
- request body size limits
- sensitive request-body redaction for high-risk endpoints
- hardened permissions for local CLI connection storage
- static contract tests that protect route parity and route-guard coverage
- input validation for security-sensitive workflows such as node recruitment and Tracey control payloads

## 2. Evidence Available In Repo

The repository already contains auditable implementation evidence in:
- [`docs/SECURITY.md`](./SECURITY.md) for control descriptions and configuration references
- [`docs/TESTING.md`](./TESTING.md) for quality and safety verification coverage
- `tests/contracts/server_safety_contract_test.py` for route-guard and redaction invariants
- `tests/contracts/api_route_contract_test.py` and `tests/contracts/command_coverage_contract_test.py` for interface integrity
- `deploy.sh` and `ansible/` for repeatable deployment automation

## 3. Control Areas Mapped By The Current Implementation

| Control area | Current support in repo |
|---|---|
| Access control | shared-token mode, OIDC introspection mode, optional no-auth mode |
| Auditability | `X-Request-ID`, structured route handling, deterministic CLI/server envelopes |
| Change integrity | contract tests for route drift and command coverage |
| Confidentiality | request-body redaction, local config permission hardening |
| Secure operations | documented deployment variables, optional recruit token, Tracey network hardening knobs |

## 4. Controls Still Required Outside The Repo

These must be provided by deployment and organizational process:
- TLS termination and certificate lifecycle management
- production secret storage and rotation
- access reviews and least-privilege network policy
- centralized logging, retention, and alerting
- vulnerability scanning and dependency patching
- incident response, backup, and evidence collection procedures

## 5. Practical Reading Order

For the current implementation, use this order:
1. [`docs/SECURITY.md`](./SECURITY.md)
2. [`docs/TESTING.md`](./TESTING.md)
3. [`docs/ARCHITECTURE.md`](./ARCHITECTURE.md)
4. [`docs/WORKFLOWS.md`](./WORKFLOWS.md)
