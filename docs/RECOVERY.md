# Guarded Kubernetes workload recovery

Continuum can inspect a Deployment, its owned ReplicaSets and Pods, and the
health of the Nodes hosting those Pods through the read-only
`GET /k8s/deployment/recovery-status` route. The request must name the active
cluster, namespace and Deployment explicitly. Missing or mismatched cluster
identity, unavailable Kubernetes data, stale or unhealthy Node state, an
unobserved Deployment generation, an active template rollout, missing,
terminating, unscheduled or non-Running Pods, unsafe rollout strategy, or a
Deployment without `neuralmimicry.ai/recovery=enabled` makes the preflight
ineligible. A stable Deployment whose desired replicas all run the current
template can remain eligible when Pods are Running but not Ready; the response
reports `readiness_degraded` and the affected `degraded_pods` separately. A
failed availability condition is accepted only for Kubernetes'
`MinimumReplicasUnavailable` reason, and a progress deadline is recoverable
only after the observed current template is on every desired replica.

Host liveness freshness is verified from the matching NodeLease in
`kube-node-lease`, using `spec.renewTime` and its declared lease duration. The
Node `Ready.lastHeartbeatTime` is not used as a short liveness deadline:
Kubernetes updates Node status less frequently when the condition is unchanged,
while kubelet renews its separate Lease for normal heartbeat traffic. A missing,
stale, malformed or unavailable Lease blocks recovery. This prevents a healthy
node from being classified as unsafe solely because its Node status has not
changed recently, while retaining a fail-closed gate when actual heartbeat
evidence is absent.

Restart requests use `POST /k8s/deployment/restart`. They are disabled by
default. Operators must explicitly set `NMC_RECOVERY_ENABLED=true` in the
protected NMC environment file to permit requests. Any unset, false, malformed
or unrecognised value leaves the gate closed. The request must still use the
cluster identity bound to the server's authenticated kubeconfig and pass the
live dependency, Pod, Node, label, rollout and resource-version checks. The
restart requires positive surge and limits `maxUnavailable` to less than the
desired replica count (or at most 25 percent), then uses the Deployment's
resource version as a conditional write and a stable request ID for
idempotency. This permits Kubernetes to add replacement capacity before
replacing an unhealthy Pod while still refusing an overlapping rollout or an
unhealthy/ambiguous host placement. The accepted response reports the observed
readiness degradation; acceptance is not proof that the replacement Pods
become ready.

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

Stateful workloads use the read-only `GET /k8s/statefulset/status` route and
`nmc k8s statefulset-status --cluster-id <id> --namespace <name> --statefulset
<name>`. It reports the StatefulSet's observed generation, current/update
revisions, desired/current/updated/ready replicas, and at most 64 Pods selected
by the StatefulSet's validated `matchLabels` and verified by owner UID. Pod
conditions and container state text are bounded; an incomplete or truncated
Pod list cannot produce a healthy result. This is observation only: it does
not restart or otherwise modify a StatefulSet.

OctoBot's live-execution and optional-service policy is managed by the typed
`POST /k8s/octobot/configuration` route and
`nmc k8s configure-octobot`. It is separately disabled by default behind
`NMC_K8S_OCTOBOT_CONFIGURATION_ENABLED=true`, requires `continuum:control`,
the authenticated active `rk1` kubeconfig, and the OctoBot Deployment labels
`neuralmimicry.ai/continuum-configuration=enabled` and
`neuralmimicry.ai/continuum-rollout=enabled`. The request accepts only the
documented cluster, namespace, Deployment, request ID, expected current
request ID, change ID and two explicit boolean settings. Read the current
`octobot_config_request_id` from `/k8s/deployment/status` and pass it as
`expected_request_id` (empty only before the first managed policy change).
Continuum changes only the corresponding environment values in the named
OctoBot and bootstrap containers, uses the Deployment resource version and
expected request ID as conditional-write guards, and deduplicates repeated
request IDs. A replay after a newer policy change fails with conflict instead
of restoring the older policy. It never accepts arbitrary commands, file
paths, secret values, or a generic environment-variable map. A changed policy
starts a Deployment rollout; callers must still inspect
`/k8s/deployment/status` and application logs before treating OctoBot as
ready.

Ansible exposes this as `nmc_recovery_enabled`, which defaults to `false` in
`ansible/deploy.yml`. Do not enable it as part of a routine package rollout.
This endpoint is a guarded request surface, not the Continuum-owned automatic
recovery controller. A durable supervisor, validated service dependency graph,
failure thresholds, cooldowns and restart reconciliation remain required before
Continuum can act independently when Conductor, Refiner or Gail is unavailable.
Conductor also defaults to dry-run with no recovery targets; both controls must
be configured deliberately before an automated recovery can be used.
If recovery is enabled, Ansible requires `nmc_k8s_cluster_id` to name the
stable identity of the single active kubeconfig context. The deployment
automation rejects an empty or invalid cluster ID when the opt-in is enabled.
Ansible exposes the independent image mutation switch as
`nmc_k8s_image_rollout_enabled`, also defaulting to `false`; enable it only
after the target Deployment is labelled and the control token and active
cluster context have been verified.
