# VCluster Implementation Status

This is the current-state status for the vcluster surface in this repository.

## Capability Matrix

| Capability | Server API | CLI | Notes |
|---|---|---|---|
| Basic create | yes | yes | CLI create currently supports `name` and optional `namespace`. |
| Advanced create config payload | yes | partial | Available through `POST /vcluster/create` with nested `config` JSON; not exposed through dedicated CLI flags. |
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

Use the raw server API when you need advanced create-time config sections such as:
- placement
- HA
- ingress/service settings
- security/RBAC options
- sync configuration
- monitoring configuration
- embedded Tracey metadata

## Known Follow-On Improvements

The most natural next implementation steps would be:
1. expose advanced create-time config through CLI flags or a config file input
2. make `vclusterConfigsRef` durable across server restarts
3. hot-apply more config changes in `config-update`
4. integrate real metrics backends for richer `metrics` output
5. add the same Tracey managed-resource hook that exists for other server-side create flows
