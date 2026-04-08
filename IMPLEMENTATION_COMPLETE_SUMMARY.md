# VCluster Advanced Implementation Summary

This document summarizes the vcluster implementation that is currently compiled into `nmc_server` and exposed through the CLI/API surface.

## Status

The advanced vcluster implementation is present in the current build and is compiled from:
- `nmc_server/src/Core/K8sHandlers_VCluster_Advanced.cpp`
- `nmc_server/src/Core/K8sHandlers_VCluster_Lifecycle.cpp`
- `nmc_server/src/Core/K8sHandlers_VCluster_Monitoring.cpp`
- `nmc_server/src/Models/VClusterConfig.h`

These files are included directly in `nmc_server/CMakeLists.txt` and their routes are registered in `nmc_server/src/Core/APIRoutes.cpp`.

## Implemented Server Capabilities

### 1. Advanced create path

`POST /vcluster/create` supports a nested `config` JSON payload that maps to `Models::VClusterConfig`.

Config sections currently modeled in code:
- `resources`
- `placement`
- `ha`
- `networking`
- `security`
- `sync`
- `monitoring`
- `tracey`

The create handler can:
- generate and store a `config_id`
- create the target namespace
- create a ServiceAccount when enabled
- create a Role and RoleBinding
- create the backing StatefulSet
- create a Service
- optionally create an Ingress
- optionally create a PodDisruptionBudget

### 2. Lifecycle operations

Implemented routes:
- `POST /vcluster/pause/{id}`
- `POST /vcluster/resume/{id}`
- `POST /vcluster/backup/{id}`
- `POST /vcluster/restore`
- `POST /vcluster/upgrade/{id}`

Behavior currently implemented:
- pause stores the original replica count in annotations and scales the StatefulSet to zero
- resume restores the stored replica count
- backup stores config/metadata in a `ConfigMap` under the `vcluster-backups` namespace
- restore recreates a vcluster from the saved config
- upgrade patches the StatefulSet image and records previous-version metadata in annotations

### 3. Monitoring and configuration operations

Implemented routes:
- `GET /vcluster/config/{id}`
- `PUT /vcluster/config/{id}`
- `GET /vcluster/metrics/{id}`
- `GET /vcluster/health/{id}`
- `GET /vcluster/resources/{id}`

Behavior currently implemented:
- config get reads stored `VClusterConfig` objects from the in-memory registry
- config update replaces stored config metadata
- metrics reports pod/state summaries and container resource declarations
- health reports desired/current/ready/updated replica state and derived health checks
- resources enumerates namespace-level pods, services, PVCs, and related objects

## Implemented CLI Capabilities

The CLI currently exposes:
- `vcluster create`
- `vcluster delete`
- `vcluster get`
- `vcluster get-config`
- `vcluster list`
- `vcluster pause`
- `vcluster resume`
- `vcluster backup`
- `vcluster restore`
- `vcluster upgrade`
- `vcluster config-get`
- `vcluster config-update`
- `vcluster metrics`
- `vcluster health`
- `vcluster resources`

Important nuance:
- the CLI create path currently sends only `name` and optional `namespace`
- the advanced server-side `config` payload is available through the API surface but does not yet have first-class CLI flags or file-based config ingestion

## Current Limitations

The implementation is present, but these limitations are also present in code:
- vcluster configuration is stored in the server's in-memory `vclusterConfigsRef` map, so it is not durable across server restarts
- backup stores configuration and metadata only; PVC snapshot integration is not implemented
- `config-update` does not hot-apply most changes to the running workload
- metrics are pod/state summaries rather than live Prometheus or metrics-server resource usage
- Tracey metadata can be embedded in vcluster config/create responses, but the vcluster create path does not currently register the same managed-resource Tracey requirement hook used by other create flows such as `vm`, `k8s`, `openshift`, `openstack`, and `node recruit`

## Where To Look In Code

- route registration: `nmc_server/src/Core/APIRoutes.cpp`
- config model: `nmc_server/src/Models/VClusterConfig.h`
- server handlers: `nmc_server/src/Core/K8sHandlers_VCluster_*.cpp`
- CLI commands: `nmc_client/src/Commands/VClusterCommands.cpp`
- client transport: `nmc_client/src/Core/CloudAPIClient.cpp`
