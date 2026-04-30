# NeuralMimicry Continuum (NMC)

[![Build and Release](https://github.com/neuralmimicry/nmc/actions/workflows/build-and-release.yml/badge.svg)](https://github.com/neuralmimicry/nmc/actions/workflows/build-and-release.yml)

NeuralMimicry Continuum ships two C++ binaries:
- `nmc`: the operator CLI
- `nmc_server`: the HTTP control plane and built-in operator docs host

Together they provide connection management, Kubernetes and virtual-cluster operations, portal orchestration for OpenShift, OpenStack, and Proxmox, Tracey fleet telemetry/control plus the adaptive plan/ramp/optimise/repeat loop, Refiner deployment workflows, and Continuum node recruitment.

## Feature Inventory

| Area | Surface | Notes |
|---|---|---|
| Connectivity | `connection`, `server`, `version` | Local connection profiles live in `~/.nmc/config.json`; selected commands also support a server-side connection registry via `--server`. |
| Core resources | `bucket`, `ssh`, `vm`, `model` | CRUD-style resource endpoints exposed through the CLI and HTTP API. Several of these server-side resources are currently in-memory and non-persistent. |
| Kubernetes | `k8s` | Cluster create/get/get-config/list/list-locations/health/resume/suspend over the server API and Kubernetes client integration. |
| Virtual clusters | `vcluster` | Create/delete/get/get-config/list plus pause/resume/backup/restore/upgrade/config-get/config-update/metrics/health/resources. Advanced create configuration exists on the server API via a `config` JSON payload; the CLI create command currently exposes `name` and optional `namespace`. |
| Provider portals | `openshift`, `openstack`, `proxmox` | Capacity listing, cluster listing, cluster request, and polling status workflows against external portal APIs. |
| Tracey | `tracey` | Heartbeat ingestion, fleet inventory, analytics, the adaptive plan/ramp/optimise/repeat loop, operator-selectable placement policies, CVE status, compromise assessment, per-agent assessment plan/report flows, rack views, agent telemetry, control, deep-dive diagnostics, debounced snapshot persistence, and optional PostgreSQL history storage. |
| Node onboarding | `node recruit` | API mode through `POST /node/recruit` or direct SSH/SCP execution from the CLI host, with optional post-recruit Ansible auto-configuration. |
| Refiner | `refiner` | Local `kubectl` deploy/status/scale/logs/remove workflows; `status` and `scale` can also use the server API via `--server`. |
| Deployment | `deploy.sh`, `ansible/deploy.yml`, `scripts/install-server.sh`, `scripts/install-client.sh` | Ubuntu/systemd deployment automation plus package-native installers for the private GitHub release assets. |
| Documentation | `docs/`, `nmc_server/src/docs/` | Markdown project docs plus built-in web docs served by `nmc_server` when `NMC_DOCS_ENABLED=true`. |
| Quality gates | `tests/contracts/`, `tests/functional/` | Static client/server contract checks, route safety checks, dedicated adaptive-loop contract checks, CLI functional tests against a mock HTTP server, and a focused server authorisation harness against a real `nmc_server` process. |

## Repository Layout

- `nmc_client/`: CLI parser, commands, transport client, and models.
- `nmc_server/`: HTTP routes, Kubernetes handlers, portal clients, Tracey support, and built-in docs.
- `docs/`: architecture, workflow, security, testing, and compliance notes.
- `ansible/`: deployment playbooks, templates, and recruited-node automation.
- `tests/`: contract and functional tests.
- `deploy.sh`: local Ubuntu/systemd deployment bootstrap script.
- `scripts/install-server.sh` and `scripts/install-client.sh`: package-native Linux installers for private GitHub releases.
- `scripts/package-release.sh` and `scripts/package-release.ps1`: packaging helpers.
- `VERSION`: shared client/server release version source of truth.

## Building

### CLI (`nmc`)

```bash
cmake -S nmc_client -B nmc_client/build
cmake --build nmc_client/build -j4
```

### Server (`nmc_server`)

The server build pulls several dependencies with CMake `FetchContent` and also expects system packages such as OpenSSL, `spdlog`, `libwebsockets`, `libpq`/`libpqxx` for PostgreSQL-backed Tracey persistence, autotools, and standard build tooling.

```bash
cmake -S nmc_server -B nmc_server/build
cmake --build nmc_server/build -j4
```

If you want the host prepared end-to-end, prefer `deploy.sh` or the [Ansible deployment guide](./ansible/README.md).

## Running

### CLI basics

```bash
./nmc_client/build/nmc --help
./nmc_client/build/nmc --version
./nmc_client/build/nmc -x json connection list
./nmc_client/build/nmc -x yaml tracey fleet
```

Global output formats currently implemented are:
- default human-readable output
- `json`
- `yaml`

### Connection and server checks

```bash
./nmc_client/build/nmc connection make local http://127.0.0.1:8080 --default
./nmc_client/build/nmc connection status
./nmc_client/build/nmc server health
./nmc_client/build/nmc server version
```

### Kubernetes and vcluster workflows

```bash
./nmc_client/build/nmc k8s list
./nmc_client/build/nmc k8s health
./nmc_client/build/nmc vcluster create demo --namespace vcluster-demo
./nmc_client/build/nmc vcluster health demo
./nmc_client/build/nmc vcluster config-get demo
./nmc_client/build/nmc vcluster upgrade demo --version 0.20.0
```

### Provider portal workflows

```bash
./nmc_client/build/nmc openshift resources
./nmc_client/build/nmc openshift request hpc-example --org neuralmimicry --gpu-count 8 --arch amd64 --region us-east-1 --provider rosa
./nmc_client/build/nmc openshift status hpc-example --watch --until Ready --interval 10 --timeout 900

./nmc_client/build/nmc openstack resources
./nmc_client/build/nmc openstack request hpc-os --org neuralmimicry --gpu-count 8 --arch amd64 --region uk1 --provider openstack
./nmc_client/build/nmc openstack status hpc-os --watch --until Ready --interval 10 --timeout 900

./nmc_client/build/nmc proxmox resources
./nmc_client/build/nmc proxmox request hpc-pve --org neuralmimicry --gpu-count 8 --arch amd64 --region edge-1 --provider proxmox
./nmc_client/build/nmc proxmox status hpc-pve --watch --until Ready --interval 10 --timeout 900
```

### Tracey workflows

```bash
./nmc_client/build/nmc tracey fleet
./nmc_client/build/nmc tracey adaptive
./nmc_client/build/nmc tracey adaptive --policy risk
./nmc_client/build/nmc tracey agents
./nmc_client/build/nmc tracey assessment
./nmc_client/build/nmc tracey assessment-plan tracey-1
./nmc_client/build/nmc tracey assessment-report tracey-1 --payload '{"slice_id":0,"result":"ok"}'
./nmc_client/build/nmc tracey analytics --window-seconds 3600 --bucket-seconds 60
./nmc_client/build/nmc tracey analysis tracey-1 --window-seconds 7200
./nmc_client/build/nmc tracey server tracey-1
./nmc_client/build/nmc tracey gpu tracey-1 nvidia:0
./nmc_client/build/nmc tracey control tracey-1 --action clear_quarantine --reason maintenance
./nmc_client/build/nmc tracey deepdive tracey-1
```

### Node recruitment and Refiner workflows

```bash
./nmc_client/build/nmc node recruit \
  --host 192.168.1.60 \
  --user ubuntu \
  --ssh-key /home/me/.ssh/id_ed25519 \
  --auto-configure \
  --tenant-id acme \
  --capability apps \
  --capability kubernetes

./nmc_client/build/nmc refiner deploy --manifest ./refiner-k8s.yaml --namespace refiner
./nmc_client/build/nmc refiner status --namespace refiner
./nmc_client/build/nmc refiner scale --replicas 3 --namespace refiner
```

### Server

```bash
./nmc_server/build/nmc_server --port 8080 -k /etc/nmc/kubeconfig
```

When docs are enabled, `nmc_server` serves the built-in operator site from its configured docs directory (`./docs` during local builds, `/usr/share/nmc-server/docs` for packaged installs) at:
- `/`
- `/docs`
- `/login`
- `/services/health/monitoring`

The lightweight unauthenticated liveness route is `GET /health`.

## Authentication and Configuration Notes

- Server auth modes: `NMC_AUTH_MODE=token` (default), `oidc`, or `off`.
- Token mode accepts `Authorization: Bearer <token>` and `X-NMC-Token`.
- In token mode, `nmc_server` can resolve the shared Customers session payload from `service_access` and apply route-level authorisation for `continuum`, `tracey`, and `aarnn`.
- Customers-issued service-account bearer tokens are supported for backend-to-backend calls. Those principals keep only their explicit groups and do not inherit any human fallback grants.
- Current server-side service gates are:
  - `continuum:observe` for read-only Continuum routes
  - `continuum:use` for mutating Continuum routes
  - `continuum:control` for `/connections*`, `/node/recruit`, and `/k8s/refiner/scale`
  - `tracey:observe` for Tracey read routes, `tracey:use` for heartbeat and assessment report ingest, and `tracey:control` for other Tracey mutations
  - `aarnn:observe` for AARNN read routes, `aarnn:use` for `/aarnn/proxy/runtime`, and `aarnn:control` for `/aarnn/proxy/control`
- `GET /auth/session` returns the normalised identity payload consumed by the built-in docs dashboard, including `service_access`, `visible_services`, and group visibility metadata.
- Client bearer token precedence is:
  1. `NMC_OIDC_ACCESS_TOKEN`
  2. `NM_OIDC_ACCESS_TOKEN`
  3. `NMC_BEARER_TOKEN`
  4. `NM_BEARER_TOKEN`
  5. active connection token from `~/.nmc/config.json`
  6. `NMC_AUTH_TOKEN`
  7. `NM_AUTH_TOKEN`
- Connection config permissions are hardened to owner read/write only.
- Optional node recruitment hardening:
  - `NMC_RECRUIT_TOKEN` on the server to require a second shared secret.
  - `NMC_RECRUIT_ANSIBLE_PLAYBOOK` on recruiter hosts to override the default post-recruit playbook.
  - `NMC_SUDO_PASSWORD` or `NMC_BECOME_PASSWORD` on the CLI host for non-interactive `sudo -S` / Ansible become.

## Deployment and Release

- Linux releases now publish `nmc-client_<version>_<arch>.deb` and `nmc-server_<version>_<arch>.deb` for `amd64` and `arm64`.
- `.github/workflows/build-and-release.yml` builds those Linux client/server artifacts automatically on push using self-hosted Ubuntu runners labelled `self-hosted`, `Linux`, and either `X64` or `ARM64`; those runners must provide passwordless `sudo` for dependency installation.
- `scripts/install-client.sh` installs the private-release CLI package on Linux.
- `scripts/install-server.sh` installs the private-release server package, writes the systemd unit, and configures packaged docs/log paths.
- `deploy.sh` remains the source-build/bootstrap path for full host provisioning.
- `ansible/deploy.yml` provides the same deployment path in Ansible form.
- `VERSION` drives the shared semantic version for client and server builds.
- `scripts/package-release.sh` and `scripts/package-release.ps1` build versioned release archives and checksum files, and Linux builds also emit Debian packages.

## Documentation Map

- [`docs/ARCHITECTURE.md`](./docs/ARCHITECTURE.md)
- [`docs/WORKFLOWS.md`](./docs/WORKFLOWS.md)
- [`docs/SECURITY.md`](./docs/SECURITY.md)
- [`docs/TESTING.md`](./docs/TESTING.md)
- [`docs/COMPLIANCE.md`](./docs/COMPLIANCE.md)
- [`IMPLEMENTATION_COMPLETE_SUMMARY.md`](./IMPLEMENTATION_COMPLETE_SUMMARY.md)
- [`VCLUSTER_IMPLEMENTATION_STATUS.md`](./VCLUSTER_IMPLEMENTATION_STATUS.md)
- [`ansible/README.md`](./ansible/README.md)
