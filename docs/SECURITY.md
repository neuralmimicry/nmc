# Security, ISO 27001, and SOC 2 Readiness

This document describes security controls implemented in the NMC client/server codebase and the operational practices required to satisfy ISO 27001 and SOC 2 expectations. It is not a certification report, but a mapping of controls to code and deployment behaviours.

## Scope
- **In scope**: `nmc_client`, `nmc_server`, and the OpenShift proxy integration to the NeuralMimicry OpenShift portal (oshift).
- **Out of scope**: Cloud provider infrastructure, CI/CD systems, and third-party hosted services unless explicitly integrated.

## Implemented Technical Controls

### Access Control (ISO 27001 A.5/A.8, SOC 2 CC6)
- **Optional API authentication** on the server via bearer token or `X-NMC-Token` header.
- Controlled by `NMC_AUTH_TOKEN` environment variable.
- If unset, the server runs in unauthenticated mode for dev/test environments.
- **OIDC bearer token validation** is available when `NMC_AUTH_MODE=oidc`, using RFC 7662 introspection.

### Request Identity & Auditability (ISO 27001 A.5/A.8, SOC 2 CC7)
- **Request IDs** are added to responses as `X-Request-ID`.
- IDs are logged for traceability across services.

### Input Limits & Error Handling (ISO 27001 A.8, SOC 2 CC7)
- **Payload size limit** enforced by `NMC_MAX_BODY_BYTES` (default 1 MiB).
- Consistent JSON error responses and status codes for all handlers.
- `/node/recruit` validates SSH targets, paths, capability values, tenant metadata, and `ansible_extra_vars` keys to reduce command injection and malformed automation payloads.
- `GET /health` exposes a minimal unauthenticated liveness response (`status`, `service`) with no sensitive payload data.

### Log Hygiene (ISO 27001 A.8, SOC 2 CC7)
- Request bodies are **redacted** for sensitive endpoints:
  - `/ssh/create`
  - `/vm/create`
  - `/model/upload`
  - `/connections/make`
  - `/openshift/clusters/request`
  - `/node/recruit`
- Body logging is **truncated** to `NMC_LOG_BODY_BYTES` (default 2048).
- `/node/recruit` command diagnostics redact sensitive runtime values (for example `continuum_auth_token` and `sudo_password`) before returning logs/output.

### Secrets Handling (ISO 27001 A.8, SOC 2 CC6)
- Client connection config file permissions are hardened to `0600` on save.
- Warnings are emitted if config file permissions are overly permissive.
- Optional bearer tokens stored per connection are persisted in `~/.nmc/config.json`.

### Common Interface Enforcement
- OpenShift cluster statuses are **normalized** to common NMC states:
  - `Pending`, `Provisioning`, `Ready`, `Failed`, `Unknown`
- Provider-specific status mappings are hidden behind the NMC interface.

## Operational Controls Required for Compliance
These items must be enforced outside code to meet ISO 27001 and SOC 2 expectations:

### Encryption In Transit (ISO 27001 A.8, SOC 2 CC6)
- Terminate TLS with a reverse proxy (or deploy HTTPS server) for:
  - `nmc_server`
  - oshift portal API
- Ensure modern TLS configurations and certificate rotation.

### Identity & Access Management (ISO 27001 A.5, SOC 2 CC6)
- Rotate `NMC_AUTH_TOKEN` regularly and store it in a secrets manager.
- Restrict access to server networks using firewall rules or Kubernetes NetworkPolicies.

### Change Management & SDLC (ISO 27001 A.8, SOC 2 CC8)
- Require code reviews and CI validation for all changes.
- Maintain a release log and approval records.

### Vulnerability Management (ISO 27001 A.8, SOC 2 CC7)
- Track dependencies with SCA tooling and update regularly.
- Pin dependency versions in CMake and document upgrade cadence.

### Incident Response (ISO 27001 A.5, SOC 2 CC7)
- Maintain an incident response plan with on-call escalation.
- Ensure log retention and secure archival.

### Backup & Recovery (ISO 27001 A.8, SOC 2 CC7)
- Back up configuration and state directories (`~/.nmc`, logs, GitOps configs).
- Test restoration procedures regularly.

### Monitoring & Alerting (ISO 27001 A.8, SOC 2 CC7)
- Forward logs to a central SIEM or logging stack.
- Alert on authentication failures and error rate spikes.

### Refiner Command Hardening (Operational)
- The `nmc refiner` command family executes `kubectl` locally; enforce RBAC least privilege for the kubeconfig used by operators.
- Prefer dedicated service accounts/namespaces for Refiner (`refiner`) and avoid broad cluster-admin credentials.
- Log `kubectl` execution outputs in CI/CD or jump-host audit trails for deployment traceability.

## Configuration Reference

### Environment Variables (Server)
- `NMC_AUTH_MODE`: `token` (default), `oidc`, or `off`.
- `NMC_AUTH_TOKEN`: Require bearer token authentication when `NMC_AUTH_MODE=token` (if unset, auth is disabled).
- `NMC_MAX_BODY_BYTES`: Maximum request body size in bytes (default 1048576).
- `NMC_LOG_BODY_BYTES`: Maximum logged body size before truncation (default 2048).
- `NMC_OSHIFT_API_URL`: Base URL for the OpenShift portal API.
- `NMC_DOCS_ENABLED`: Enable/disable built-in documentation routes (default true).
- `NMC_OIDC_INTROSPECTION_URL`: RFC 7662 introspection endpoint (required for `oidc` mode).
- `NMC_OIDC_CLIENT_ID`: OIDC client ID (optional but recommended).
- `NMC_OIDC_CLIENT_SECRET`: OIDC client secret (optional but recommended).
- `NMC_OIDC_ISSUER`: Expected `iss` claim (optional but recommended).
- `NMC_OIDC_AUDIENCE`: Expected `aud` claim (single or comma-separated list).
- `NMC_OIDC_ALLOWED_AUDIENCES`: Comma-separated list of acceptable audiences (optional).
- `NMC_OIDC_REQUIRED_SCOPE`: Comma-separated scopes that must be present (optional).
- `NMC_RECRUIT_TOKEN`: Optional secondary secret required by `/node/recruit` for node onboarding authorisation.
- `NMC_RECRUIT_ANSIBLE_PLAYBOOK`: Optional default Ansible playbook path used when `/node/recruit` is called with `auto_configure=true` and no explicit `ansible_playbook`.
- `NMC_RELEASE_CHECK_URL`: Optional override for the GitHub releases endpoint used by version update checks.
- Compatibility aliases: `NM_AUTH_MODE`, `NM_AUTH_TOKEN`, and `NM_OIDC_*` are also honored for cross-project SSO setups.

### Environment Variables (Client)
- `NMC_OIDC_ACCESS_TOKEN`: Preferred bearer token for OIDC-protected endpoints.
- `NMC_BEARER_TOKEN`: Alternate bearer token variable (fallback).
- `NMC_AUTH_TOKEN`: Legacy bearer token variable (last-resort fallback).
- `NMC_SUDO_PASSWORD`: Optional password used by `node recruit` for remote `sudo -S` and Ansible become.
- `NMC_BECOME_PASSWORD`: Alias for `NMC_SUDO_PASSWORD`.

### Files
- Client connection config: `~/.nmc/config.json` (permissions hardened to `0600`).

## Recommendations (Next Steps)
1. Enable TLS termination for all deployments.
2. Integrate centralized audit logging with retention policies.
3. If required, extend OIDC validation to offline JWT verification or SSO-specific policies.
4. Implement persistent storage for server-side objects with access controls.
5. Add automated dependency scanning and security testing in CI.
