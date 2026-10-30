# Product discovery and runtime observations

Continuum recognises eleven public-facing product identities, including Cora,
Foresight, Verity and Relay. `nmc_server/src/docs/public/products.json` is the
catalogue of declared names, repository identities, relationships and owners.
It contains no tokens, source records or live operational payloads. Registration
is not evidence of health, forecast qualification or execution permission.

New server builds expose the catalogue at the guarded observer endpoint
`GET /platform/products`. The dashboard supports older reviewed servers through
the existing `/services/health/monitoring/public/products.json` asset route when
that endpoint returns 404, without falling back after an authorization failure.
A docs-only update can therefore preserve the currently patched server binary.

The four shared-role products have exact Deployment targets. Their read-only
observations use the existing guarded deployment status API and the configured
`NMC_K8S_CLUSTER_ID`. Identity, desired/ready/available replicas and observed
specification generation determine availability. Recovery eligibility is a
separate control and is never treated as health. Missing permissions, unknown
identity, stale generation, suspended workloads and unavailable observations do
not become healthy defaults. No new restart or write executor is introduced.

The deploy workflow copies the catalogue/module before the dashboard and HTML,
uses atomic file replacement/backups, verifies live file hashes, and asserts the
running server PID and binary checksum are unchanged. Only the existing
non-secret cluster ID is added to the deployed catalogue. The ordinary package
upgrade is inappropriate for the current modified recovery binary and mixed
client/server package versions; this compatibility update does not invoke it.

Run `node --test tests/functional/product-catalog.test.cjs` and the existing
`dashboard_device_safety_browser_test.mjs`. The former checks exact product
coordinates, identity/generation/capacity semantics and credential-safe relative
status paths. Existing recovery source changes are retained alongside this
catalogue in the repository; production binary replacement remains a separately
verified native release.
