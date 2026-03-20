# Compliance Posture (ISO 27001 / SOC 2)

This repository includes controls that support ISO 27001 and SOC 2 alignment. It does not, by itself, confer certification. Compliance depends on organisational policies, audits, and evidence collection.

## Scope
- `nmc_server` API and authentication middleware.
- `nmc_client` CLI and connection storage.
- Documentation and configuration guidance in `docs/`.

## Implemented Control Highlights
- Access control: bearer token and optional OIDC validation.
- Request identity: request IDs and structured logs.
- Input limits: request size limits and logging truncation.
- File permissions: hardened config file permissions for client secrets.

## ISO 27001 Alignment Notes
- Identity and access: auth modes and OIDC validation.
- Logging and monitoring: request IDs and server logs.
- Secure development: configuration-driven controls and redaction guidance.

## SOC 2 Trust Services Alignment Notes
- Security: authentication, authorisation, and auditability.
- Availability: operational guidance for TLS and monitoring.
- Confidentiality: secret handling and permission hardening.

## Operational Requirements (Outside This Repo)
- TLS termination, access reviews, and incident response procedures.
- Dependency scanning and patching.
- Centralized log retention and evidence collection.
