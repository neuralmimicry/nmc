# VCluster Implementation Status

This is the current-state status for the vcluster surface in this repository.

## Capability Matrix

| Capability | Server API | CLI | Notes |
|---|---|---|---|
| Basic create | yes | yes | CLI requires `name` and accepts optional `namespace` and advanced JSON configuration. |
| Advanced create config payload | yes | yes | Available through `POST /vcluster/create` and `nmc vcluster create --config-file PATH`; the CLI accepts a JSON object up to 1 MiB and validates it before network access. |
| Delete / get / list / kubeconfig | yes | yes | Fully wired through `CloudAPIClient` and `VClusterCommands.cpp`. |
| Pause / resume | yes | yes | Implemented with StatefulSet replica patching and annotations. |
| Backup / restore | yes | yes | Backup uses `ConfigMap` storage in `vcluster-backups`. |
| Upgrade | yes | yes | Patches the vcluster image tag on the StatefulSet. |
| Config get / update | yes | yes | Update replaces stored config metadata but does not fully hot-apply changes. |
| Metrics / health / resources | yes | yes | Metrics are pod/state summaries, not full usage telemetry. |
| Config persistence across server restart | no | no | Config registry is in-memory. |
| PVC snapshot backup | no | no | Not implemented. |
| Managed-resource Tracey hook on vcluster create | no | no | Other resource families have this; vcluster create currently does not. |

## Route Inventory

Registered vcluster routes:
- `POST /vcluster/create`
- `DELETE /vcluster/delete/{id}`
- `GET /vcluster/get/{id}`
- `GET /vcluster/list`
- `GET /vcluster/get-config/{id}`
- `POST /vcluster/pause/{id}`
- `POST /vcluster/resume/{id}`
- `POST /vcluster/backup/{id}`
- `POST /vcluster/restore`
- `POST /vcluster/upgrade/{id}`
- `GET /vcluster/config/{id}`
- `PUT /vcluster/config/{id}`
- `GET /vcluster/metrics/{id}`
- `GET /vcluster/health/{id}`
- `GET /vcluster/resources/{id}`

## CLI Inventory

Registered CLI subcommands:
- `create`
- `delete`
- `get`
- `get-config`
- `list`
- `pause`
- `resume`
- `backup`
- `restore`
- `upgrade`
- `config-get`
- `config-update`
- `metrics`
- `health`
- `resources`

## Current Operational Guidance

Use the CLI for day-to-day vcluster lifecycle, inspection, and configuration metadata workflows.

Use `nmc vcluster create NAME --config-file PATH` for advanced create-time settings such as:
- placement
- HA
- ingress/service settings
- security/RBAC options
- sync configuration
- monitoring configuration
- embedded Tracey metadata

Use the raw server API only when integrating a caller that already constructs the nested configuration request.

## Known Follow-On Improvements

The most natural next implementation steps would be:
1. make `vclusterConfigsRef` durable across server restarts
2. hot-apply more config changes in `config-update`
3. integrate real metrics backends for richer `metrics` output
4. add the same Tracey managed-resource hook that exists for other server-side create flows
