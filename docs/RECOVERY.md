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
selected container and recording rollout annotations. For `octobot/octobot`
with container `octobot`, it also updates the three named application bootstrap
containers in that same conditional patch; their existing images must match the
application before mutation. BusyBox and unrelated containers are preserved.
Otherwise the Gail guard would still run the old code and the service would
remain unavailable after a main-container upgrade. The operation is
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

## AARNN FPV storage configuration

The authenticated `continuum:control` operation `POST /k8s/aarnn/fpv-storage`
and CLI `nmc k8s configure-aarnn-fpv` reconcile `NM_FPV_JOB_ROOT` inside
an observed writable shared PVC and pin the existing running image digest.
All target containers must be Ready with that exact content, preventing an
implicit code upgrade when a mutable tag restarts. The owning UI must be opted into Continuum
rollout, the rollout policy must be enabled, and expected-image/resourceVersion
checks and recorded request/change identities preserve scope and idempotency.
See [the FPV storage runbook](aarnn-fpv-storage-repair.md) for prerequisites,
worker-first ordering, availability checks and full projection/render acceptance.

## Independent availability recovery

The packaged `nmc-availability-watchdog.service` runs on Continuum's host,
independently of Conductor, Refiner, Gail and PostgreSQL. It invokes authenticated
typed NMC operations; it does not execute arbitrary remediation commands or cold
power cycles. It starts only with `/etc/nmc/availability-watchdog.json`. Copy and
review the packaged example against fresh inventory, use an owner-only local NMC
credential/config for the service user, and begin with `enabled: false`. The
package does not create credentials, enable workload recovery or start mutations.
Node.js 18 or newer, matching nmc-client and util-linux are package dependencies.

Repeated independently observed Conductor unavailability permits only the
explicitly listed service/host targets. Healthy hosts receive an eligible typed
service restart first; `NMC_RECOVERY_ENABLED` and deployment opt-in remain
mandatory. Warm restart additionally requires a well-formed expired NodeLease,
NotReady node, independent TCP timeout, exact pinned controller endpoint/MAC,
power-on/fault preflight and every registered upstream dependency healthy.
Connection refusal proves host reachability and never authorizes a reset.
Inventory/evidence drift, unknown health, partial readiness or stale observations
hold the action. A fresh final collection cancels recovery if Conductor returns.

The OS process lock, owner-only fsynced local journal, persisted intent before
mutation and unresolved-intent circuit breaker survive process restart. Lost
responses are reconciled against live state; they are never resubmitted. Typed
controller actions also enforce a one-hour cross-request target cooldown, no
second action for the same outage, and at most two independent host actions per
day across the server audit. The observer further permits at most one host
restart per day and bounds all recovery actions with its configured budget.
Failed recovery leaves the circuit open for operator reconciliation. Preserve
both audit files when investigating; do not erase them to retry an outage.

Create the configured inhibit file for maintenance/rolling delivery and remove it
only after the owner of the operation releases the target. Deploy the observer
outside qc01's NFS/PostgreSQL failure domain, using local Spirit storage. Recovery
requires fresh host/NodeLease, metrics, dependencies, configured service readiness
and public/authentication-route observations over repeated samples. A 302 or 401
checks the authentication boundary only; authenticated product acceptance and
PostgreSQL/NFS data integrity remain separate incident evidence.

## Qualified production image transitions

`POST /k8s/deployment/image-rollout` accepts optional boolean
`require_zero_unavailable`. When true, the live compare-and-swap preflight requires
`neuralmimicry.ai/surge-safe=enabled`, RollingUpdate, explicit maxUnavailable zero,
positive integer maxSurge, minReadySeconds >= 10 and readiness probes on every
container. Shared PVC/NFS/CSI/hostPath, host ports/networking and other unsupported
stateful overlap are rejected without a Kubernetes patch. The status field
`zero_unavailable_rollout_ready` reports this strategy contract, not immunity to
hardware or software failures. The original rollout opt-in, digest, scope and
request-identity checks still apply. Stateful applications such as OctoBot need a
qualified single-writer/traffic-cutover adapter; relabelling them is insufficient.
