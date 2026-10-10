# Guarded Kubernetes workload recovery

Continuum can inspect a Deployment, its owned ReplicaSets and Pods, and the
health of the Nodes hosting those Pods through the read-only
`GET /k8s/deployment/recovery-status` route. The request must name the active
cluster, namespace and Deployment explicitly. Missing or mismatched cluster
identity, unavailable Kubernetes data, stale Node heartbeats, incomplete
rollouts, unsafe rollout strategy, or a Deployment without
`neuralmimicry.ai/recovery=enabled` makes the preflight ineligible.

Restart requests use `POST /k8s/deployment/restart`. They are disabled by
default. Operators must explicitly set `NMC_RECOVERY_ENABLED=true` in the
protected NMC environment file to permit requests. Any unset, false, malformed
or unrecognised value leaves the gate closed. The request must still use the
cluster identity bound to the server's authenticated kubeconfig and pass the
live dependency, Pod, Node, label, rollout and resource-version checks. The
restart uses the Deployment's resource version as a conditional write and a
stable request ID for idempotency.

Immutable Deployment image changes use the separate
`POST /k8s/deployment/image-rollout` control route. It only accepts an
`ghcr.io/neuralmimicry/...@sha256:<digest>` image for a Deployment explicitly
labelled `neuralmimicry.ai/continuum-rollout=enabled`. Callers provide the
expected current image plus stable request and change IDs; Continuum compares
the current image and Deployment resource version before patching only the
selected container and recording rollout annotations. The operation is
disabled by default behind `NMC_K8S_IMAGE_ROLLOUT_ENABLED=true`, requires
`continuum:control`, the active cluster identity and the authenticated
kubeconfig, and returns acceptance separately from rollout readiness.
`GET /k8s/deployment/status` reports observed generation, replica readiness,
container images and the last rollout IDs through the same authenticated
cluster context. Repeating the same request and intent is deduplicated.

Ansible exposes this as `nmc_recovery_enabled`, which defaults to `false` in
`ansible/deploy.yml`. Do not enable it as part of a routine package rollout.
Conductor also defaults to dry-run with no recovery targets; both controls
must be configured deliberately before an automated recovery can be used.
If recovery is enabled, Ansible requires `nmc_k8s_cluster_id` to name the
stable identity of the single active kubeconfig context. The deployment
automation rejects an empty or invalid cluster ID when the opt-in is enabled.
Ansible exposes the independent image mutation switch as
`nmc_k8s_image_rollout_enabled`, also defaulting to `false`; enable it only
after the target Deployment is labelled and the control token and active
cluster context have been verified.
