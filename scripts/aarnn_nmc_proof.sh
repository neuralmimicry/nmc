#!/usr/bin/env bash
set -euo pipefail

# Proof flow:
# 1. Discover the AARNN runtime/control/orchestrator endpoints exposed through Continuum.
# 2. Create/start/stop/delete a runtime workspace on the internal control plane.
# 3. Update/start/stop the shared distributed startup network and exercise AER over HTTP.
# 4. Record the orchestrator gRPC endpoint published by Continuum for follow-on checks.

NMC_BIN="${NMC_BIN:-nmc}"
CONTROL_RUNTIME_PLANE="${CONTROL_RUNTIME_PLANE:-control}"
NETWORK_ID="${NETWORK_ID:-tenant-aarnn}"
WORKSPACE_ID="${WORKSPACE_ID:-nmc-proof-$(date +%s)}"
WORK_DIR="${WORK_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/nmc-aarnn-proof.XXXXXX")}"
KEEP_ARTIFACTS="${KEEP_ARTIFACTS:-1}"
KEEP_WORKSPACE="${KEEP_WORKSPACE:-0}"
KEEP_NETWORK_RUNNING="${KEEP_NETWORK_RUNNING:-0}"

CONFIG_FILE="${WORK_DIR}/network.json"
NDJSON_FILE="${WORK_DIR}/aer.ndjson"
ENDPOINTS_JSON="${WORK_DIR}/endpoints.json"

cleanup() {
  local status=$?
  set +e
  if [[ "${KEEP_WORKSPACE}" != "1" ]]; then
    "${NMC_BIN}" aarnn runtime control "${WORKSPACE_ID}" --plane "${CONTROL_RUNTIME_PLANE}" --action stop >/dev/null 2>&1
    "${NMC_BIN}" aarnn runtime delete "${WORKSPACE_ID}" --plane "${CONTROL_RUNTIME_PLANE}" >/dev/null 2>&1
  fi
  if [[ "${KEEP_NETWORK_RUNNING}" != "1" ]]; then
    "${NMC_BIN}" aarnn network control "${NETWORK_ID}" --action stop >/dev/null 2>&1
  fi
  if [[ "${KEEP_ARTIFACTS}" != "1" ]]; then
    rm -rf "${WORK_DIR}"
  fi
  exit "${status}"
}
trap cleanup EXIT

mkdir -p "${WORK_DIR}"

cat >"${CONFIG_FILE}" <<'JSON'
{
  "num_sensory_neurons": 8,
  "num_hidden_layers": 1,
  "num_hidden_per_layer_initial": 16,
  "num_output_neurons": 4,
  "growth_enabled": false,
  "use_morphology": false,
  "use_aarnn_delays": true,
  "aarnn_layer_depth": 2,
  "clumping_design": "HumanBrain",
  "ui_target_fps": 60.0
}
JSON

cat >"${NDJSON_FILE}" <<'NDJSON'
{"spike_indices":[0,2,5],"time_ms":0.0,"dt_ms":1.0}
{"input_values":[1.0,0.25,0.75,0.0],"aer_base":4096,"time_ms":1.0,"dt_ms":1.0}
NDJSON

run_json() {
  local name="$1"
  shift
  echo
  echo "== ${name} =="
  "${NMC_BIN}" "$@" -x json | tee "${WORK_DIR}/${name}.json"
}

echo "Artifacts: ${WORK_DIR}"
echo "Workspace ID: ${WORKSPACE_ID}"
echo "Distributed network ID: ${NETWORK_ID}"

run_json endpoints aarnn endpoints

python3 - "${ENDPOINTS_JSON}" <<'PY'
import json
import pathlib
import sys

payload = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
control = payload.get("control", {})
runtime = payload.get("runtime", {})
orchestrator = payload.get("orchestrator", {})

if not control.get("found"):
    raise SystemExit("control plane endpoint was not discovered")
if not orchestrator.get("grpc_url"):
    raise SystemExit("orchestrator grpc_url is missing from endpoint discovery")

print(f"Discovered control API: {control.get('api_base_url', '<missing>')}")
print(f"Discovered runtime API: {runtime.get('api_base_url', '<missing>')}")
print(f"Discovered orchestrator gRPC endpoint: {orchestrator.get('grpc_url')}")
PY

run_json runtime-create \
  aarnn runtime create \
  --plane "${CONTROL_RUNTIME_PLANE}" \
  --workspace-id "${WORKSPACE_ID}" \
  --name "${WORKSPACE_ID}" \
  --config-file "${CONFIG_FILE}"

run_json runtime-start \
  aarnn runtime control "${WORKSPACE_ID}" \
  --plane "${CONTROL_RUNTIME_PLANE}" \
  --action start

run_json runtime-get \
  aarnn runtime get "${WORKSPACE_ID}" \
  --plane "${CONTROL_RUNTIME_PLANE}"

run_json runtime-activity \
  aarnn runtime activity "${WORKSPACE_ID}" \
  --plane "${CONTROL_RUNTIME_PLANE}"

run_json runtime-snapshot \
  aarnn runtime snapshot "${WORKSPACE_ID}" \
  --plane "${CONTROL_RUNTIME_PLANE}"

run_json network-update \
  aarnn network update "${NETWORK_ID}" \
  --config-file "${CONFIG_FILE}" \
  --neuron-model aarnn \
  --learning-rule aarnn

run_json network-start \
  aarnn network control "${NETWORK_ID}" \
  --action start

run_json network-status \
  aarnn network status

run_json aer-inject \
  aarnn network aer-inject "${NETWORK_ID}" \
  --aer-base 4096 \
  --spike-indices 0,2,5 \
  --dt-ms 1.0

run_json aer-stream \
  aarnn network aer-stream "${NETWORK_ID}" \
  --aer-base 4096 \
  --body-file "${NDJSON_FILE}"

run_json network-activity \
  aarnn network activity "${NETWORK_ID}"

run_json network-snapshot \
  aarnn network snapshot "${NETWORK_ID}"

if [[ "${KEEP_WORKSPACE}" != "1" ]]; then
  run_json runtime-stop \
    aarnn runtime control "${WORKSPACE_ID}" \
    --plane "${CONTROL_RUNTIME_PLANE}" \
    --action stop

  run_json runtime-delete \
    aarnn runtime delete "${WORKSPACE_ID}" \
    --plane "${CONTROL_RUNTIME_PLANE}"
fi

if [[ "${KEEP_NETWORK_RUNNING}" != "1" ]]; then
  run_json network-stop \
    aarnn network control "${NETWORK_ID}" \
    --action stop
fi

echo
echo "Proof script completed."
