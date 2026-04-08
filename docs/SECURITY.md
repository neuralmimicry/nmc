# Security Notes

This document describes the security controls that are implemented in the current codebase and the operational controls still required outside the repository.

## 1. Scope

In scope:
- `nmc_client`
- `nmc_server`
- built-in docs served by `nmc_server`
- deployment automation in `deploy.sh` and `ansible/`

Out of scope:
- reverse proxies and TLS termination layers
- external OpenShift and OpenStack portal services
- CI/CD platforms and secret stores
- organization-level access review, monitoring, and incident response processes

## 2. Implemented Technical Controls

### 2.1 Authentication and authorization

Server auth modes:
- `NMC_AUTH_MODE=token`: shared secret validation using `Authorization: Bearer <token>` or `X-NMC-Token`
- `NMC_AUTH_MODE=oidc`: bearer token introspection via `OIDCValidator`
- `NMC_AUTH_MODE=off`: explicit no-auth mode

Additional details:
- OIDC mode fails closed when validation is not configured correctly.
- `/health` remains intentionally unauthenticated for liveness probing.
- docs routes are public when docs are enabled.

### 2.2 Request identity and request sizing

- guarded routes receive `X-Request-ID`
- caller-supplied `X-Request-ID` values are preserved
- `NMC_MAX_BODY_BYTES` limits request-body size
- `NMC_LOG_BODY_BYTES` limits how much non-sensitive body content is written to logs

### 2.3 Sensitive body redaction

Request bodies are intentionally not logged for these endpoint prefixes:
- `/ssh/create`
- `/vm/create`
- `/model/upload`
- `/connections/make`
- `/openshift/clusters/request`
- `/openstack/clusters/request`
- `/node/recruit`

### 2.4 Secret handling

Client-side:
- connection profiles are stored in `~/.nmc/config.json`
- file permissions are reset to owner read/write only on save
- warnings are emitted when the file is readable by group or others
- per-connection tokens can be stored locally and are used in token resolution precedence

Server-side:
- shared auth secrets and OIDC credentials are read from environment variables
- recruitment diagnostics redact sensitive runtime values before being returned

### 2.5 Input validation and execution boundaries

`node recruit` validates:
- host, user, port, node type
- tenant identifiers and environment labels
- script, binary, SSH key, and password-file paths
- Ansible extra-var keys
- binary args and other user-controlled strings for control characters

`tracey` CLI commands validate:
- JSON flags before a network call is attempted
- positive integer analytics-window flags
- required agent identifiers

`refiner` commands validate:
- manifest paths
- namespace/context strings
- rollout timeout and tail values

### 2.6 Tracey network safety

The server applies extra controls around Tracey status and control-plane communication:
- optional managed-resource enforcement via `NMC_TRACEY_ENFORCE_MANAGED_RESOURCES`
- stale-agent windows via `NMC_TRACEY_STALE_SECONDS`
- requirement grace windows via `NMC_TRACEY_REQUIREMENT_GRACE_SECONDS`
- optional discovery disable via `NMC_TRACEY_DISCOVERY_ENABLED`
- local/private address enforcement unless `NMC_TRACEY_ALLOW_PUBLIC_ADDR=true`
- optional TLS verification control via `NMC_TRACEY_TLS_VERIFY`
- optional signature requirement via `NMC_TRACEY_REQUIRE_SIGNATURE`
- optional dedicated bearer token for Tracey status polling via `NMC_TRACEY_STATUS_BEARER_TOKEN`

### 2.7 Local command execution safety

- `refiner` shells out through quoted command arguments rather than interpolating raw command strings.
- `node recruit --direct` validates local paths and generated arguments before executing SSH/SCP.
- `--dry-run` is available for node recruitment to inspect generated commands before execution.

## 3. Public Surfaces

Public by design:
- `GET /health`
- built-in docs routes when `NMC_DOCS_ENABLED=true`

Protected by the shared guard unless auth is disabled:
- `/server/version`
- resource CRUD routes
- provider portal routes
- Tracey routes
- `/node/recruit`

## 4. Operational Controls Required Outside The Repo

These controls are still required in deployment:
- TLS termination for `nmc_server` and any proxied upstreams
- secret storage and rotation for shared tokens and OIDC credentials
- network segmentation and firewall policy
- centralized log collection and retention
- dependency scanning and patch management
- incident response procedures and evidence collection
- backup and restore testing for any persisted deployment state

## 5. Configuration Reference

### 5.1 Server auth and HTTP behavior

- `NMC_AUTH_MODE`: `token`, `oidc`, or `off`
- `NMC_AUTH_TOKEN`: shared secret for token mode
- `NMC_DOCS_ENABLED`: enable/disable built-in docs routes
- `NMC_MAX_BODY_BYTES`: request body size cap
- `NMC_LOG_BODY_BYTES`: max logged body length before truncation
- `NMC_RELEASE_CHECK_URL`: override release-check endpoint
- Compatibility aliases: `NM_AUTH_MODE`, `NM_AUTH_TOKEN`, and `NM_OIDC_*`

### 5.2 OIDC validation

- `NMC_OIDC_INTROSPECTION_URL`
- `NMC_OIDC_CLIENT_ID`
- `NMC_OIDC_CLIENT_SECRET`
- `NMC_OIDC_ISSUER`
- `NMC_OIDC_AUDIENCE`
- `NMC_OIDC_ALLOWED_AUDIENCES`
- `NMC_OIDC_AUDIENCES`
- `NMC_OIDC_REQUIRED_SCOPE`
- `NMC_OIDC_REQUIRED_SCOPES`

### 5.3 Kubernetes and provider integrations

- `KUBECONFIG` or `NMC_KUBECONFIG`
- `NMC_K8S_API_URL`
- `NMC_OSHIFT_API_URL`
- `NMC_OPENSTACK_API_URL`

### 5.4 Tracey behavior

- `NMC_TRACEY_STALE_SECONDS`
- `NMC_TRACEY_ENFORCE_MANAGED_RESOURCES`
- `NMC_TRACEY_DISCOVERY_ENABLED`
- `NMC_TRACEY_ALLOW_PUBLIC_ADDR`
- `NMC_TRACEY_TLS_VERIFY`
- `NMC_TRACEY_REQUIRE_SIGNATURE`
- `NMC_TRACEY_REQUIREMENT_GRACE_SECONDS`
- `NMC_TRACEY_DISCOVERY_BIND_ADDR`
- `NMC_TRACEY_DISCOVERY_PORT`
- `NMC_TRACEY_DISCOVERY_MAX_AGE_MS`
- `NMC_TRACEY_STATUS_POLL_MS`
- `NMC_TRACEY_STATUS_TIMEOUT_MS`
- `NMC_TRACEY_STATUS_MAX_BACKOFF_MS`
- `NMC_TRACEY_HISTORY_MAX_SAMPLES`
- `NMC_TRACEY_AGENT_LOG_MAX_ENTRIES`
- `NMC_TRACEY_STATUS_BEARER_TOKEN`
- `NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT`
- `NMC_TRACEY_LOCAL_AGENT_ID`
- `NMC_TRACEY_LOCAL_STATUS_ADDR`
- `NMC_TRACEY_CVE_ROOT`
- Compatibility aliases: `NM_TRACEY_*`

### 5.5 Recruitment and automation

- `NMC_RECRUIT_TOKEN`
- `NMC_RECRUIT_ANSIBLE_PLAYBOOK`
- `NMC_SUDO_PASSWORD`
- `NMC_BECOME_PASSWORD`

### 5.6 Client token resolution

- `NMC_OIDC_ACCESS_TOKEN`
- `NM_OIDC_ACCESS_TOKEN`
- `NMC_BEARER_TOKEN`
- `NM_BEARER_TOKEN`
- active connection token in `~/.nmc/config.json`
- `NMC_AUTH_TOKEN`
- `NM_AUTH_TOKEN`
