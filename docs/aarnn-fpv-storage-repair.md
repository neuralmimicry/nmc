# Scoped AARNN FPV storage repair

`POST /k8s/aarnn/fpv-storage` and `nmc k8s configure-aarnn-fpv` repair the
producer/consumer path mismatch that makes FPV submission fail when
`NM_FPV_JOB_ROOT=/data/fpv-jobs` but the shared claim is mounted at
`/var/lib/aarnn`. The server derives `<observed-mount>/fpv-jobs`; a caller
cannot supply an arbitrary path, environment variable, namespace or workload.

The operation requires authenticated `continuum:control`, explicitly enabled
image-rollout policy, an active authenticated Kubernetes context and the owning
`aarnn-web-ui` Deployment's `neuralmimicry.ai/continuum-rollout=enabled` label.
Only `aarnn-web-ui/web-ui` and `aarnn-fpv-workers/fpv-worker` in namespace
`aarnn` are accepted. The target must reference the owning web UI's same
`web-ui-data` persistent claim, mounted writable without subpaths. Missing,
ambiguous or secret-backed root settings fail closed.

```bash
nmc k8s configure-aarnn-fpv --cluster-id rk1 \
  --deployment aarnn-fpv-workers --expected-image CURRENT_IMAGE --image RUNNING_DIGEST \
  --request-id fpv-workers-storage-20261012-01 --change-id FPV-STORAGE-20261012
nmc k8s configure-aarnn-fpv --cluster-id rk1 \
  --deployment aarnn-web-ui --expected-image CURRENT_IMAGE --image RUNNING_DIGEST \
  --request-id fpv-ui-storage-20261012-01 --change-id FPV-STORAGE-20261012
```

Obtain each declared expected image and each Pod container's `image_id` from a
fresh typed Deployment status read. Use the canonical running digest for `--image`
(remove `docker-pullable://` if present). Every target Pod must be Running and its
target container Ready with identical image content; incomplete or conflicting
observations fail closed. The server pins this existing content during the repair
so restarting an Always-pulled mutable tag cannot silently promote newer code.
It rechecks the declared image and patches against the observed resourceVersion.
Request/change identity and the resulting root are recorded as Deployment
annotations. A repeated matching intent is deduplicated, including after a
server restart; a conflicting reuse is rejected. Running image content, authentication,
unrelated environment, PVC contents and volume declarations are preserved.

An environment change causes a normal workload rollout. Verify worker readiness
first, then web-UI readiness, projection GET, FPV POST, completed-job status
and video retrieval. Use the existing availability, single-writer, resource,
dependency and rollback gates before changing the live singleton. API success
does not establish end-user readiness. A no-op path repair records its intent
without changing the pod template.

The Ansible default must also derive the job root from its shared data mount,
so later releases do not reintroduce the mismatch. This operation supports the
documented current AARNN workload names; it is not a general Deployment patch API.
