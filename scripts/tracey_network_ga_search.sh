#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/tracey_network_ga_search.sh [options]

Run a genetic-algorithm search against Tracey network simulation endpoints through the
existing `nmc tracey` CLI surface to find high-value node/GPU/core configurations.

Scopes:
  --scope fleet|rack|server|gpu    Target Tracey scope. Default: fleet
  --rack RACK_ID                   Required for --scope rack
  --agent AGENT_ID                 Required for --scope server and --scope gpu
  --gpu GPU_ID                     Required for --scope gpu

Search controls:
  --objective balanced|throughput|efficiency|resilience
                                   Fitness objective. Default: balanced
  --population N                   Population size. Default: 18
  --generations N                  Generation count. Default: 10
  --elite N                        Elite survivors per generation. Default: 4
  --immigrants N                   Fresh random candidates per generation. Default: 3
  --mutation-rate FLOAT            Per-gene mutation probability in [0,1]. Default: 0.28
  --stall-limit N                  Stop early after N stagnant generations. Default: 4
  --strategies CSV                 Allowed sim strategies. Default: balanced,throughput,collective
  --top-k N                        Top result count to report. Default: 8
  --seed N                         Seed bash RANDOM for reproducible search

Bounds:
  Defaults are derived from the live Tracey `resource_forecast.continuum_expansion`
  baseline/recommended/maximum targets for the selected scope.

  --min-nodes N
  --max-nodes N
  --min-gpus N
  --max-gpus N
  --min-cores N
  --max-cores N

Runtime:
  --nmc-bin PATH                   Path to `nmc`. Default: ./nmc_client/build/nmc if present, else nmc
  --work-dir DIR                   Persist artifacts in DIR
  --keep-artifacts                 Keep the generated work directory
  --no-keep-artifacts              Delete the temp work directory on exit
  --quiet                          Reduce progress output
  --help                           Show this help

Examples:
  scripts/tracey_network_ga_search.sh --scope fleet
  scripts/tracey_network_ga_search.sh --scope rack --rack R09 --objective throughput
  scripts/tracey_network_ga_search.sh --scope server --agent tracey-1 --population 24 --generations 14
  scripts/tracey_network_ga_search.sh --scope gpu --agent tracey-1 --gpu nvidia:0 --objective resilience

Notes:
  - The script uses the current live Tracey status as the baseline.
  - Candidate fitness is computed from `simulation_forecast` and, when present,
    `gpu_simulation_forecast`.
  - The search is heuristic, not exhaustive. Results depend on the objective and the
    server-side Continuum heuristic simulation model.
EOF
}

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

note() {
  if [[ "${QUIET}" != "1" ]]; then
    printf '%s\n' "$*" >&2
  fi
}

note_duplicate_fallback() {
  if [[ "${DUPLICATE_FALLBACK_NOTED:-0}" != "1" ]]; then
    note "Candidate diversity is constrained by the current bounds; allowing duplicate candidates to keep the GA progressing."
    DUPLICATE_FALLBACK_NOTED=1
  fi
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

int_ceil_div() {
  local a="$1"
  local b="$2"
  if (( b <= 0 )); then
    echo 0
    return
  fi
  echo $(( (a + b - 1) / b ))
}

min_int() {
  if (( $1 < $2 )); then
    echo "$1"
  else
    echo "$2"
  fi
}

max_int() {
  if (( $1 > $2 )); then
    echo "$1"
  else
    echo "$2"
  fi
}

clamp_int() {
  local value="$1"
  local lo="$2"
  local hi="$3"
  if (( value < lo )); then
    echo "$lo"
  elif (( value > hi )); then
    echo "$hi"
  else
    echo "$value"
  fi
}

float_gt() {
  awk -v a="$1" -v b="$2" 'BEGIN { exit (a > b ? 0 : 1) }'
}

rand_u31() {
  echo $(( ((RANDOM << 16) ^ RANDOM) & 2147483647 ))
}

rand_int() {
  local upper="$1"
  if (( upper <= 1 )); then
    echo 0
    return
  fi
  echo $(( $(rand_u31) % upper ))
}

rand_between() {
  local lo="$1"
  local hi="$2"
  if (( hi <= lo )); then
    echo "$lo"
    return
  fi
  local span=$(( hi - lo + 1 ))
  echo $(( lo + $(rand_int "$span") ))
}

chance_ppm() {
  local ppm="$1"
  (( $(rand_int 1000000) < ppm ))
}

unique_attempt_limit() {
  local target="$1"
  local limit=$(( target * 64 ))
  if (( limit < 32 )); then
    limit=32
  fi
  echo "${limit}"
}

normalize_strategy() {
  local strategy="${1,,}"
  case "$strategy" in
    balanced|throughput|collective) printf '%s\n' "$strategy" ;;
    *) printf 'balanced\n' ;;
  esac
}

choose_random_strategy() {
  local idx
  idx="$(rand_int "${#STRATEGIES[@]}")"
  printf '%s\n' "${STRATEGIES[$idx]}"
}

extract_json_line() {
  awk '
    /^[[:space:]]*\{/ { line = $0 }
    END {
      if (line != "") {
        print line
      }
    }
  '
}

unwrap_cli_json() {
  jq -cer '
    if (type == "object") and (.format? == "json") and ((.data? | type) == "string") then
      (.data | fromjson)
    else
      .
    end
  '
}

build_tracey_cmd() {
  TRACEY_CMD=(tracey)
  case "$SCOPE" in
    fleet)
      TRACEY_CMD+=(fleet)
      ;;
    rack)
      [[ -n "${RACK_ID}" ]] || die "--rack is required for --scope rack"
      TRACEY_CMD+=(rack "$RACK_ID")
      ;;
    server)
      [[ -n "${AGENT_ID}" ]] || die "--agent is required for --scope server"
      TRACEY_CMD+=(server "$AGENT_ID")
      ;;
    gpu)
      [[ -n "${AGENT_ID}" ]] || die "--agent is required for --scope gpu"
      [[ -n "${GPU_ID}" ]] || die "--gpu is required for --scope gpu"
      TRACEY_CMD+=(gpu "$AGENT_ID" "$GPU_ID")
      ;;
    *)
      die "Unsupported scope: $SCOPE"
      ;;
  esac
}

run_tracey_payload() {
  local stdout_file stderr_file stdout_raw json_line
  stdout_file="$(mktemp "${WORK_DIR}/stdout.XXXXXX")"
  stderr_file="$(mktemp "${WORK_DIR}/stderr.XXXXXX")"

  set +e
  "${NMC_BIN}" "${TRACEY_CMD[@]}" "$@" -x json >"${stdout_file}" 2>"${stderr_file}"
  local status=$?
  set -e

  if [[ -s "${stderr_file}" ]]; then
    cat "${stderr_file}" >>"${CLI_STDERR_LOG}"
  fi

  stdout_raw="$(cat "${stdout_file}")"
  rm -f "${stdout_file}" "${stderr_file}"

  if (( status != 0 )); then
    if [[ -n "${stdout_raw}" ]]; then
      printf '%s\n' "${stdout_raw}" >>"${CLI_STDERR_LOG}"
    fi
    return "${status}"
  fi

  json_line="$(printf '%s\n' "${stdout_raw}" | extract_json_line)"
  [[ -n "${json_line}" ]] || return 1
  printf '%s\n' "${json_line}" | unwrap_cli_json
}

build_baseline_context() {
  jq '
    {
      continuum: (.resource_forecast.continuum_expansion // {}),
      baseline: (.resource_forecast.continuum_expansion.baseline // {}),
      recommended: (.resource_forecast.continuum_expansion.recommended_targets // {}),
      maximum: (.resource_forecast.continuum_expansion.maximum_targets // {}),
      ai_advice: (.resource_forecast.ai_advice // {}),
      route_metadata: {
        rack: (.rack // null),
        zone: (.zone // null),
        host: (.host // null),
        agent_id: (.agent_id // null),
        gpu_id: (.gpu_id // null)
      },
      gpu_summary: (if (.gpu_id? != null) then (.summary // {}) else {} end)
    }
  ' "${BASELINE_RAW_FILE}" >"${BASELINE_CONTEXT_FILE}"
}

derive_bounds() {
  local extracted
  extracted="$(
    jq -r '
      [
        (.baseline.node_count // 1),
        (.baseline.gpu_count // 1),
        ((.baseline.cpu_core_estimate // 1) | ceil),
        (.recommended.node_count // (.baseline.node_count // 1)),
        (.recommended.gpu_count // (.baseline.gpu_count // 1)),
        ((.recommended.cpu_core_estimate // (.baseline.cpu_core_estimate // 1)) | ceil),
        (.maximum.node_count // (.recommended.node_count // (.baseline.node_count // 1))),
        (.maximum.gpu_count // (.recommended.gpu_count // (.baseline.gpu_count // 1))),
        ((.maximum.cpu_core_estimate // (.recommended.cpu_core_estimate // (.baseline.cpu_core_estimate // 1))) | ceil)
      ] | @tsv
    ' "${BASELINE_CONTEXT_FILE}"
  )"

  IFS=$'\t' read -r BASELINE_NODES BASELINE_GPUS BASELINE_CORES \
    RECOMMENDED_NODES RECOMMENDED_GPUS RECOMMENDED_CORES \
    DEFAULT_MAX_NODES DEFAULT_MAX_GPUS DEFAULT_MAX_CORES <<<"${extracted}"

  SEARCH_MIN_NODES="${MIN_NODES_OVERRIDE:-$BASELINE_NODES}"
  SEARCH_MIN_GPUS="${MIN_GPUS_OVERRIDE:-$BASELINE_GPUS}"
  SEARCH_MIN_CORES="${MIN_CORES_OVERRIDE:-$BASELINE_CORES}"
  SEARCH_MAX_NODES="${MAX_NODES_OVERRIDE:-$DEFAULT_MAX_NODES}"
  SEARCH_MAX_GPUS="${MAX_GPUS_OVERRIDE:-$DEFAULT_MAX_GPUS}"
  SEARCH_MAX_CORES="${MAX_CORES_OVERRIDE:-$DEFAULT_MAX_CORES}"

  (( SEARCH_MIN_NODES >= 1 )) || die "min nodes must be >= 1"
  (( SEARCH_MIN_GPUS >= 1 )) || die "min gpus must be >= 1"
  (( SEARCH_MIN_CORES >= 1 )) || die "min cores must be >= 1"

  (( SEARCH_MAX_NODES >= SEARCH_MIN_NODES )) || die "max nodes must be >= min nodes"
  (( SEARCH_MAX_GPUS >= SEARCH_MIN_GPUS )) || die "max gpus must be >= min gpus"
  (( SEARCH_MAX_CORES >= SEARCH_MIN_CORES )) || die "max cores must be >= min cores"

  BASE_GPU_PER_NODE="$(int_ceil_div "$BASELINE_GPUS" "$BASELINE_NODES")"
  REC_GPU_PER_NODE="$(int_ceil_div "$RECOMMENDED_GPUS" "$RECOMMENDED_NODES")"
  BASE_CORES_PER_GPU="$(int_ceil_div "$BASELINE_CORES" "$BASELINE_GPUS")"
  REC_CORES_PER_GPU="$(int_ceil_div "$RECOMMENDED_CORES" "$RECOMMENDED_GPUS")"

  GPU_PER_NODE_REF="$(max_int "$BASE_GPU_PER_NODE" "$REC_GPU_PER_NODE")"
  CORES_PER_GPU_REF="$(max_int "$BASE_CORES_PER_GPU" "$REC_CORES_PER_GPU")"

  GPU_PER_NODE_MIN="$(max_int 1 "$(( GPU_PER_NODE_REF / 2 ))")"
  GPU_PER_NODE_MAX="$(max_int "$GPU_PER_NODE_MIN" "$(( GPU_PER_NODE_REF * 2 ))")"
  CORES_PER_GPU_MIN="$(max_int 1 "$(( (CORES_PER_GPU_REF * 3) / 4 ))")"
  CORES_PER_GPU_MAX="$(max_int "$CORES_PER_GPU_MIN" "$(( CORES_PER_GPU_REF * 2 + 1 ))")"
}

repair_candidate() {
  local nodes="$1"
  local gpus="$2"
  local cores="$3"
  local strategy
  strategy="$(normalize_strategy "$4")"

  nodes="$(clamp_int "$nodes" "$SEARCH_MIN_NODES" "$SEARCH_MAX_NODES")"
  gpus="$(clamp_int "$gpus" "$SEARCH_MIN_GPUS" "$SEARCH_MAX_GPUS")"
  cores="$(clamp_int "$cores" "$SEARCH_MIN_CORES" "$SEARCH_MAX_CORES")"

  if (( SEARCH_MAX_GPUS >= nodes )); then
    gpus="$(max_int "$gpus" "$nodes")"
  fi

  local density_min_gpus density_max_gpus
  density_min_gpus=$(( nodes * GPU_PER_NODE_MIN ))
  density_max_gpus=$(( nodes * GPU_PER_NODE_MAX ))
  if (( density_min_gpus <= SEARCH_MAX_GPUS )); then
    gpus="$(max_int "$gpus" "$density_min_gpus")"
  fi
  if (( density_max_gpus >= SEARCH_MIN_GPUS )); then
    gpus="$(min_int "$gpus" "$density_max_gpus")"
  fi
  gpus="$(clamp_int "$gpus" "$SEARCH_MIN_GPUS" "$SEARCH_MAX_GPUS")"

  local density_min_cores density_max_cores
  density_min_cores=$(( gpus * CORES_PER_GPU_MIN ))
  density_max_cores=$(( gpus * CORES_PER_GPU_MAX ))
  if (( density_min_cores <= SEARCH_MAX_CORES )); then
    cores="$(max_int "$cores" "$density_min_cores")"
  fi
  if (( density_max_cores >= SEARCH_MIN_CORES )); then
    cores="$(min_int "$cores" "$density_max_cores")"
  fi
  cores="$(clamp_int "$cores" "$SEARCH_MIN_CORES" "$SEARCH_MAX_CORES")"

  printf '%s|%s|%s|%s\n' "$nodes" "$gpus" "$cores" "$strategy"
}

random_candidate() {
  local nodes gpus cores strategy
  nodes="$(rand_between "$SEARCH_MIN_NODES" "$SEARCH_MAX_NODES")"

  local gpu_lo gpu_hi
  gpu_lo="$(max_int "$SEARCH_MIN_GPUS" "$(( nodes * GPU_PER_NODE_MIN ))")"
  gpu_hi="$(min_int "$SEARCH_MAX_GPUS" "$(( nodes * GPU_PER_NODE_MAX ))")"
  if (( gpu_hi < gpu_lo )); then
    gpu_lo="$SEARCH_MIN_GPUS"
    gpu_hi="$SEARCH_MAX_GPUS"
  fi
  gpus="$(rand_between "$gpu_lo" "$gpu_hi")"

  local core_lo core_hi
  core_lo="$(max_int "$SEARCH_MIN_CORES" "$(( gpus * CORES_PER_GPU_MIN ))")"
  core_hi="$(min_int "$SEARCH_MAX_CORES" "$(( gpus * CORES_PER_GPU_MAX ))")"
  if (( core_hi < core_lo )); then
    core_lo="$SEARCH_MIN_CORES"
    core_hi="$SEARCH_MAX_CORES"
  fi
  cores="$(rand_between "$core_lo" "$core_hi")"
  strategy="$(choose_random_strategy)"

  repair_candidate "$nodes" "$gpus" "$cores" "$strategy"
}

mix_gene() {
  local left="$1"
  local right="$2"
  case "$(rand_int 3)" in
    0) echo "$left" ;;
    1) echo "$right" ;;
    *) echo $(( (left + right + 1) / 2 )) ;;
  esac
}

mutate_gene() {
  local value="$1"
  local lo="$2"
  local hi="$3"
  local generation="$4"
  local span step delta next

  span=$(( hi - lo ))
  if (( span <= 0 )); then
    echo "$value"
    return
  fi

  step=$(( span / (generation + 3) ))
  if (( step < 1 )); then
    step=1
  fi
  delta=$(( $(rand_int "$(( step * 2 + 1 ))") - step ))
  next=$(( value + delta ))
  clamp_int "$next" "$lo" "$hi"
}

make_child() {
  local left="$1"
  local right="$2"
  local generation="$3"
  local n1 g1 c1 s1 n2 g2 c2 s2 nodes gpus cores strategy

  IFS='|' read -r n1 g1 c1 s1 <<<"${left}"
  IFS='|' read -r n2 g2 c2 s2 <<<"${right}"

  nodes="$(mix_gene "$n1" "$n2")"
  gpus="$(mix_gene "$g1" "$g2")"
  cores="$(mix_gene "$c1" "$c2")"
  if (( $(rand_int 2) == 0 )); then
    strategy="$s1"
  else
    strategy="$s2"
  fi

  if chance_ppm "${MUTATION_RATE_PPM}"; then
    nodes="$(mutate_gene "$nodes" "$SEARCH_MIN_NODES" "$SEARCH_MAX_NODES" "$generation")"
  fi
  if chance_ppm "${MUTATION_RATE_PPM}"; then
    gpus="$(mutate_gene "$gpus" "$SEARCH_MIN_GPUS" "$SEARCH_MAX_GPUS" "$generation")"
  fi
  if chance_ppm "${MUTATION_RATE_PPM}"; then
    cores="$(mutate_gene "$cores" "$SEARCH_MIN_CORES" "$SEARCH_MAX_CORES" "$generation")"
  fi
  if chance_ppm "${MUTATION_RATE_PPM}"; then
    strategy="$(choose_random_strategy)"
  fi

  repair_candidate "$nodes" "$gpus" "$cores" "$strategy"
}

build_summary_from_payload() {
  local raw_file="$1"
  local summary_file="$2"
  local nodes="$3"
  local gpus="$4"
  local cores="$5"
  local strategy="$6"

  jq \
    --slurpfile ctx "${BASELINE_CONTEXT_FILE}" \
    --arg objective "${OBJECTIVE}" \
    --arg scope "${SCOPE}" \
    --arg raw_payload_file "${raw_file}" \
    --arg strategy "${strategy}" \
    --argjson nodes "${nodes}" \
    --argjson gpus "${gpus}" \
    --argjson cores "${cores}" '
      def clip($lo; $hi):
        if . < $lo then $lo
        elif . > $hi then $hi
        else .
        end;

      def ratio($n; $d; $fallback):
        if ($d // 0) > 0 then ($n / $d) else $fallback end;

      def closeness($value; $target; $radius):
        (1 - (($value - $target) | abs / $radius)) | clip(0; 1);

      ($ctx[0]) as $ctx
      | . as $payload
      | ($ctx.baseline // {}) as $base
      | ($ctx.gpu_summary // {}) as $gpu_base
      | ($payload.simulation_request // {}) as $request
      | ($payload.simulation_forecast // {}) as $forecast
      | ($payload.gpu_simulation_forecast // {}) as $gpu
      | ($forecast.estimated_network_bps // 0) as $network
      | ($forecast.estimated_active_flows // 0) as $flows
      | ($forecast.estimated_power_w // 0) as $power
      | ($forecast.estimated_gpu_utilization_avg_pct // ($gpu.estimated_util_pct // 0)) as $gpu_util
      | ($forecast.estimated_cpu_usage_pct // 0) as $cpu_util
      | ($forecast.estimated_latency_pressure // 1) as $latency
      | ($forecast.estimated_queue_pressure // 1) as $queue
      | ($forecast.collective_penalty // 0) as $collective_penalty
      | ($forecast.cross_network_share // 0) as $cross_network_share
      | ($forecast.confidence // 0) as $confidence
      | ($forecast.extra_nodes // (($nodes - ($base.node_count // 1)) | if . > 0 then . else 0 end)) as $extra_nodes
      | ($forecast.extra_gpus // (($gpus - ($base.gpu_count // 1)) | if . > 0 then . else 0 end)) as $extra_gpus
      | ($forecast.extra_cpu_cores // (($cores - ($base.cpu_core_estimate // 1)) | if . > 0 then . else 0 end)) as $extra_cores
      | ($gpu.estimated_guard_risk // $gpu_base.last_guard_risk // 0) as $guard_risk
      | ($gpu.estimated_reliability_score // $gpu_base.reliability_score // ($confidence | clip(0.2; 1))) as $reliability
      | ($gpu.estimated_guard_confidence // $confidence) as $gpu_confidence
      | ratio($network; ($base.attributed_total_bps // 0); (if $network > 0 then 1 else 0 end)) as $network_gain
      | ratio($flows; ($base.active_flows // 0); (if $flows > 0 then 1 else 0 end)) as $flow_gain
      | (1
          + (0.70 * ratio($extra_nodes; ($base.node_count // 1); 0))
          + (0.55 * ratio($extra_gpus; ($base.gpu_count // 1); 0))
          + (0.35 * ratio($extra_cores; ($base.cpu_core_estimate // 1); 0))
        ) as $resource_spend
      | ($network_gain / $resource_spend) as $throughput_efficiency
      | (
          if ($power > 0) and (($base.power_w // 0) > 0) then
            ($network_gain / ($power / $base.power_w))
          else
            $network_gain
          end
        ) as $power_efficiency
      | closeness($gpu_util; 82; 40) as $gpu_util_score
      | closeness($cpu_util; 72; 45) as $cpu_util_score
      | ((1 - $latency) | clip(0; 1)) as $latency_headroom
      | ((1 - $queue) | clip(0; 1)) as $queue_headroom
      | ((1 - $guard_risk) | clip(0; 1)) as $risk_headroom
      | (closeness(($gpu.estimated_temp_c // 55); 60; 35)) as $temperature_score
      | (
          if $objective == "throughput" then
            (3.00 * $network_gain)
            + (1.90 * $flow_gain)
            + (0.95 * $gpu_util_score)
            + (0.45 * $cpu_util_score)
            + (0.35 * $power_efficiency)
            + (0.45 * $confidence)
            + (0.25 * $gpu_confidence)
            + (0.20 * $latency_headroom)
            + (0.15 * $queue_headroom)
            - (0.55 * $collective_penalty)
            - (0.35 * $cross_network_share)
            - (0.40 * $guard_risk)
          elif $objective == "efficiency" then
            (2.60 * $throughput_efficiency)
            + (1.40 * $power_efficiency)
            + (0.85 * $gpu_util_score)
            + (0.70 * $cpu_util_score)
            + (0.90 * $latency_headroom)
            + (0.85 * $queue_headroom)
            + (0.55 * $confidence)
            + (0.35 * $risk_headroom)
            + (0.20 * $temperature_score)
            - (0.45 * $collective_penalty)
            - (0.35 * $cross_network_share)
          elif $objective == "resilience" then
            (1.40 * $throughput_efficiency)
            + (0.80 * $network_gain)
            + (1.25 * $latency_headroom)
            + (1.20 * $queue_headroom)
            + (0.95 * $confidence)
            + (0.80 * $gpu_confidence)
            + (1.20 * $risk_headroom)
            + (0.90 * $reliability)
            + (0.45 * $temperature_score)
            + (0.35 * $gpu_util_score)
            - (0.55 * $collective_penalty)
            - (0.45 * $cross_network_share)
          else
            (2.05 * $throughput_efficiency)
            + (1.10 * $network_gain)
            + (0.80 * $flow_gain)
            + (0.95 * $gpu_util_score)
            + (0.75 * $cpu_util_score)
            + (0.95 * $latency_headroom)
            + (0.90 * $queue_headroom)
            + (0.85 * $power_efficiency)
            + (0.60 * $confidence)
            + (0.40 * $gpu_confidence)
            + (0.55 * $risk_headroom)
            + (0.40 * $reliability)
            + (0.20 * $temperature_score)
            - (0.55 * $collective_penalty)
            - (0.40 * $cross_network_share)
            - (0.20 * (ratio($extra_nodes; ($base.node_count // 1); 0)))
          end
        ) as $base_score
      | (
          $base_score
          + (if ($forecast.within_heuristic_limits // true) then 0.45 else -0.90 end)
          + (if ($forecast.clamped // false) then -1.50 else 0 end)
        ) as $fitness
      | {
          fitness: (($fitness * 1000) | round / 1000),
          objective: $objective,
          scope: $scope,
          candidate: {
            nodes: $nodes,
            gpus: $gpus,
            cores: $cores,
            strategy: $strategy
          },
          derived: {
            network_gain: (($network_gain * 1000) | round / 1000),
            flow_gain: (($flow_gain * 1000) | round / 1000),
            throughput_efficiency: (($throughput_efficiency * 1000) | round / 1000),
            power_efficiency: (($power_efficiency * 1000) | round / 1000),
            gpu_util_score: (($gpu_util_score * 1000) | round / 1000),
            cpu_util_score: (($cpu_util_score * 1000) | round / 1000),
            latency_headroom: (($latency_headroom * 1000) | round / 1000),
            queue_headroom: (($queue_headroom * 1000) | round / 1000),
            risk_headroom: (($risk_headroom * 1000) | round / 1000),
            temperature_score: (($temperature_score * 1000) | round / 1000)
          },
          simulation_request: $request,
          simulation_forecast: $forecast,
          gpu_simulation_forecast: $gpu,
          ai_advice: ($payload.resource_forecast.ai_advice // {}),
          route_metadata: {
            rack: ($payload.rack // null),
            zone: ($payload.zone // null),
            host: ($payload.host // null),
            agent_id: ($payload.agent_id // null),
            gpu_id: ($payload.gpu_id // null)
          },
          raw_payload_file: $raw_payload_file
        }
    ' "${raw_file}" >"${summary_file}"
}

record_failure_summary() {
  local summary_file="$1"
  local raw_file="$2"
  local nodes="$3"
  local gpus="$4"
  local cores="$5"
  local strategy="$6"
  local error_text="$7"

  jq -n \
    --arg objective "${OBJECTIVE}" \
    --arg scope "${SCOPE}" \
    --arg raw_payload_file "${raw_file}" \
    --arg error_text "${error_text}" \
    --arg strategy "${strategy}" \
    --argjson nodes "${nodes}" \
    --argjson gpus "${gpus}" \
    --argjson cores "${cores}" '
      {
        fitness: -1000000,
        objective: $objective,
        scope: $scope,
        candidate: {
          nodes: $nodes,
          gpus: $gpus,
          cores: $cores,
          strategy: $strategy
        },
        error: $error_text,
        simulation_request: {},
        simulation_forecast: {},
        gpu_simulation_forecast: {},
        ai_advice: {},
        route_metadata: {},
        raw_payload_file: $raw_payload_file
      }
    ' >"${summary_file}"
}

evaluate_candidate() {
  local candidate="$1"
  local nodes gpus cores strategy raw_file summary_file score raw_payload

  if [[ -n "${FITNESS_CACHE[$candidate]+x}" ]]; then
    EVALUATE_SCORE="${FITNESS_CACHE[$candidate]}"
    return
  fi

  IFS='|' read -r nodes gpus cores strategy <<<"${candidate}"
  raw_file="${RAW_DIR}/${nodes}n_${gpus}g_${cores}c_${strategy}.json"
  summary_file="${SUMMARY_DIR}/${nodes}n_${gpus}g_${cores}c_${strategy}.json"

  if raw_payload="$(run_tracey_payload --sim-nodes "${nodes}" --sim-gpus "${gpus}" --sim-cores "${cores}" --sim-strategy "${strategy}")"; then
    printf '%s\n' "${raw_payload}" >"${raw_file}"
    build_summary_from_payload "${raw_file}" "${summary_file}" "${nodes}" "${gpus}" "${cores}" "${strategy}"
  else
    : >"${raw_file}"
    record_failure_summary \
      "${summary_file}" \
      "${raw_file}" \
      "${nodes}" \
      "${gpus}" \
      "${cores}" \
      "${strategy}" \
      "CLI invocation failed for ${candidate}; see ${CLI_STDERR_LOG}"
  fi

  score="$(jq -r '.fitness' "${summary_file}")"
  FITNESS_CACHE["${candidate}"]="${score}"
  SUMMARY_FILE_CACHE["${candidate}"]="${summary_file}"
  EVALUATE_SCORE="${score}"
}

tournament_pick() {
  local -n ranked_ref="$1"
  local pool_limit="$2"
  local best_index candidate_index best_line best_key

  pool_limit="$(max_int 1 "${pool_limit}")"
  best_index="$(rand_int "${pool_limit}")"
  for _ in 1 2; do
    candidate_index="$(rand_int "${pool_limit}")"
    if (( candidate_index < best_index )); then
      best_index="${candidate_index}"
    fi
  done

  best_line="${ranked_ref[$best_index]}"
  best_key="${best_line#*$'\t'}"
  printf '%s\n' "${best_key}"
}

init_population() {
  local -A seen=()
  POPULATION=()

  add_population_candidate() {
    local repaired="$1"
    if [[ -z "${seen[$repaired]+x}" ]]; then
      seen["$repaired"]=1
      POPULATION+=("$repaired")
    fi
  }

  local strategy mid_nodes mid_gpus mid_cores
  mid_nodes=$(( (BASELINE_NODES + RECOMMENDED_NODES + 1) / 2 ))
  mid_gpus=$(( (BASELINE_GPUS + RECOMMENDED_GPUS + 1) / 2 ))
  mid_cores=$(( (BASELINE_CORES + RECOMMENDED_CORES + 1) / 2 ))

  for strategy in "${STRATEGIES[@]}"; do
    add_population_candidate "$(repair_candidate "$BASELINE_NODES" "$BASELINE_GPUS" "$BASELINE_CORES" "$strategy")"
    add_population_candidate "$(repair_candidate "$RECOMMENDED_NODES" "$RECOMMENDED_GPUS" "$RECOMMENDED_CORES" "$strategy")"
    add_population_candidate "$(repair_candidate "$DEFAULT_MAX_NODES" "$DEFAULT_MAX_GPUS" "$DEFAULT_MAX_CORES" "$strategy")"
    add_population_candidate "$(repair_candidate "$mid_nodes" "$mid_gpus" "$mid_cores" "$strategy")"
  done

  local attempts=0
  local max_attempts
  max_attempts="$(unique_attempt_limit "${POPULATION_SIZE}")"
  while (( ${#POPULATION[@]} < POPULATION_SIZE && attempts < max_attempts )); do
    add_population_candidate "$(random_candidate)"
    attempts=$(( attempts + 1 ))
  done

  if (( ${#POPULATION[@]} < POPULATION_SIZE )); then
    note_duplicate_fallback
    while (( ${#POPULATION[@]} < POPULATION_SIZE )); do
      POPULATION+=("$(random_candidate)")
    done
  fi
}

emit_generation_progress() {
  local generation="$1"
  local summary_file="$2"

  jq -r --argjson generation "${generation}" '
    "generation=\($generation)"
    + " best_fitness=\(.fitness)"
    + " nodes=\(.candidate.nodes)"
    + " gpus=\(.candidate.gpus)"
    + " cores=\(.candidate.cores)"
    + " strategy=\(.candidate.strategy)"
    + " topology=\(.simulation_forecast.topology // "n/a")"
    + " collective=\(.simulation_forecast.recommended_collective // "n/a")"
  ' "${summary_file}" >&2

  jq -r --argjson generation "${generation}" '
    [
      $generation,
      .fitness,
      .candidate.nodes,
      .candidate.gpus,
      .candidate.cores,
      .candidate.strategy,
      (.simulation_forecast.topology // "n/a"),
      (.simulation_forecast.recommended_collective // "n/a")
    ] | @tsv
  ' "${summary_file}" >>"${PROGRESS_FILE}"
}

build_best_command() {
  local summary_file="$1"
  jq -r \
    --arg nmc_bin "${NMC_BIN}" \
    --arg base_cmd "${TRACEY_CMD[*]}" '
      "\($nmc_bin) \($base_cmd)"
      + " --sim-nodes \(.candidate.nodes)"
      + " --sim-gpus \(.candidate.gpus)"
      + " --sim-cores \(.candidate.cores)"
      + " --sim-strategy \(.candidate.strategy)"
      + " -x json"
    ' "${summary_file}"
}

write_leaderboard() {
  {
    printf 'fitness\tnodes\tgpus\tcores\tstrategy\ttopology\trecommended_collective\tnetwork_bps\tgpu_util_pct\tcpu_util_pct\tlatency_pressure\tqueue_pressure\tpower_w\tconfidence\twithin_limits\tclamped\traw_payload_file\tsummary_file\n'
    local candidate summary_file
    for candidate in "${!SUMMARY_FILE_CACHE[@]}"; do
      summary_file="${SUMMARY_FILE_CACHE[$candidate]}"
      jq -r \
        --arg summary_file "${summary_file}" '
          [
            .fitness,
            .candidate.nodes,
            .candidate.gpus,
            .candidate.cores,
            .candidate.strategy,
            (.simulation_forecast.topology // "n/a"),
            (.simulation_forecast.recommended_collective // "n/a"),
            (.simulation_forecast.estimated_network_bps // 0),
            (.gpu_simulation_forecast.estimated_util_pct // .simulation_forecast.estimated_gpu_utilization_avg_pct // 0),
            (.simulation_forecast.estimated_cpu_usage_pct // 0),
            (.simulation_forecast.estimated_latency_pressure // 0),
            (.simulation_forecast.estimated_queue_pressure // 0),
            (.simulation_forecast.estimated_power_w // 0),
            (.simulation_forecast.confidence // 0),
            (.simulation_forecast.within_heuristic_limits // false),
            (.simulation_forecast.clamped // false),
            (.raw_payload_file // ""),
            $summary_file
          ] | @tsv
        ' "${summary_file}"
    done | sort -t$'\t' -k1,1nr -k2,2n -k3,3n -k4,4n
  } >"${LEADERBOARD_FILE}"

  {
    head -n 1 "${LEADERBOARD_FILE}"
    tail -n +2 "${LEADERBOARD_FILE}" | head -n "${TOP_K}"
  } >"${TOP_FILE}"
}

write_summary_json() {
  local best_command="$1"
  local summary_files=()
  local candidate
  for candidate in "${!SUMMARY_FILE_CACHE[@]}"; do
    summary_files+=("${SUMMARY_FILE_CACHE[$candidate]}")
  done

  jq -s \
    --slurpfile baseline "${BASELINE_CONTEXT_FILE}" \
    --arg scope "${SCOPE}" \
    --arg objective "${OBJECTIVE}" \
    --arg best_command "${best_command}" \
    --arg artifact_dir "${WORK_DIR}" \
    --argjson population "${POPULATION_SIZE}" \
    --argjson generations "${GENERATIONS}" \
    --argjson elite "${ELITE_COUNT}" \
    --argjson immigrants "${IMMIGRANTS}" \
    --argjson top_k "${TOP_K}" \
    --arg mutation_rate "${MUTATION_RATE}" '
      sort_by(-.fitness) as $items
      | {
          scope: $scope,
          objective: $objective,
          artifact_dir: $artifact_dir,
          best_command: $best_command,
          ga: {
            population: $population,
            generations: $generations,
            elite: $elite,
            immigrants: $immigrants,
            mutation_rate: ($mutation_rate | tonumber)
          },
          baseline: $baseline[0],
          best: $items[0],
          top_candidates: $items[:$top_k]
        }
    ' "${summary_files[@]}" >"${SUMMARY_JSON_FILE}"
}

print_final_report() {
  local summary_file="$1"
  local best_command="$2"

  printf '\nArtifacts: %s\n' "${WORK_DIR}"
  printf 'Scope: %s\n' "${SCOPE}"
  printf 'Objective: %s\n' "${OBJECTIVE}"
  printf 'Population: %s | Generations: %s | Elite: %s | Immigrants: %s | Mutation rate: %s\n' \
    "${POPULATION_SIZE}" "${GENERATIONS}" "${ELITE_COUNT}" "${IMMIGRANTS}" "${MUTATION_RATE}"
  printf 'Bounds: nodes=%s..%s gpus=%s..%s cores=%s..%s\n' \
    "${SEARCH_MIN_NODES}" "${SEARCH_MAX_NODES}" \
    "${SEARCH_MIN_GPUS}" "${SEARCH_MAX_GPUS}" \
    "${SEARCH_MIN_CORES}" "${SEARCH_MAX_CORES}"

  jq -r '
    [
      "",
      "Best Candidate:",
      "  fitness: \(.fitness)",
      "  nodes: \(.candidate.nodes)",
      "  gpus: \(.candidate.gpus)",
      "  cores: \(.candidate.cores)",
      "  strategy: \(.candidate.strategy)",
      "  topology: \(.simulation_forecast.topology // "n/a")",
      "  recommended collective: \(.simulation_forecast.recommended_collective // "n/a")",
      "  estimated network bps: \(.simulation_forecast.estimated_network_bps // 0)",
      "  estimated active flows: \(.simulation_forecast.estimated_active_flows // 0)",
      "  estimated gpu util pct: \(.gpu_simulation_forecast.estimated_util_pct // .simulation_forecast.estimated_gpu_utilization_avg_pct // 0)",
      "  estimated cpu util pct: \(.simulation_forecast.estimated_cpu_usage_pct // 0)",
      "  estimated latency pressure: \(.simulation_forecast.estimated_latency_pressure // 0)",
      "  estimated queue pressure: \(.simulation_forecast.estimated_queue_pressure // 0)",
      "  estimated power w: \(.simulation_forecast.estimated_power_w // 0)",
      "  confidence: \(.simulation_forecast.confidence // 0)",
      "  within heuristic limits: \(.simulation_forecast.within_heuristic_limits // false)",
      "  clamped: \(.simulation_forecast.clamped // false)",
      "  simulation focus: \(.simulation_forecast.simulation_focus // "n/a")",
      (if (.ai_advice.summary // "") != "" then "  ai advice: \(.ai_advice.summary)" else empty end)
    ] | .[]
  ' "${summary_file}"

  printf '\nReplay Command:\n  %s\n' "${best_command}"
  printf '\nTop Candidates:\n'
  awk -F'\t' '
    NR == 1 {
      printf "%-10s %-7s %-7s %-7s %-12s %-28s %-18s %-12s %-9s %-9s\n",
        "fitness", "nodes", "gpus", "cores", "strategy", "topology", "collective", "network_bps", "gpu_util", "latency"
      next
    }
    {
      printf "%-10s %-7s %-7s %-7s %-12s %-28s %-18s %-12s %-9s %-9s\n",
        $1, $2, $3, $4, $5, $6, $7, $8, $9, $11
    }
  ' "${TOP_FILE}"

  printf '\nFiles:\n'
  printf '  baseline raw: %s\n' "${BASELINE_RAW_FILE}"
  printf '  baseline context: %s\n' "${BASELINE_CONTEXT_FILE}"
  printf '  leaderboard: %s\n' "${LEADERBOARD_FILE}"
  printf '  top candidates: %s\n' "${TOP_FILE}"
  printf '  summary json: %s\n' "${SUMMARY_JSON_FILE}"
  printf '  cli stderr log: %s\n' "${CLI_STDERR_LOG}"
}

SCOPE="${SCOPE:-fleet}"
RACK_ID="${RACK_ID:-}"
AGENT_ID="${AGENT_ID:-}"
GPU_ID="${GPU_ID:-}"
OBJECTIVE="${OBJECTIVE:-balanced}"
POPULATION_SIZE="${POPULATION_SIZE:-18}"
GENERATIONS="${GENERATIONS:-10}"
ELITE_COUNT="${ELITE_COUNT:-4}"
IMMIGRANTS="${IMMIGRANTS:-3}"
MUTATION_RATE="${MUTATION_RATE:-0.28}"
STALL_LIMIT="${STALL_LIMIT:-4}"
STRATEGIES_CSV="${STRATEGIES_CSV:-balanced,throughput,collective}"
TOP_K="${TOP_K:-8}"
QUIET="${QUIET:-0}"
SEED="${SEED:-}"
KEEP_ARTIFACTS="${KEEP_ARTIFACTS:-1}"
WORK_DIR="${WORK_DIR:-}"

MIN_NODES_OVERRIDE=""
MAX_NODES_OVERRIDE=""
MIN_GPUS_OVERRIDE=""
MAX_GPUS_OVERRIDE=""
MIN_CORES_OVERRIDE=""
MAX_CORES_OVERRIDE=""

if [[ -x "./nmc_client/build/nmc" ]]; then
  NMC_BIN="${NMC_BIN:-./nmc_client/build/nmc}"
else
  NMC_BIN="${NMC_BIN:-nmc}"
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --scope)
      SCOPE="${2:-}"
      shift 2
      ;;
    --rack)
      RACK_ID="${2:-}"
      shift 2
      ;;
    --agent)
      AGENT_ID="${2:-}"
      shift 2
      ;;
    --gpu)
      GPU_ID="${2:-}"
      shift 2
      ;;
    --objective)
      OBJECTIVE="${2:-}"
      shift 2
      ;;
    --population)
      POPULATION_SIZE="${2:-}"
      shift 2
      ;;
    --generations)
      GENERATIONS="${2:-}"
      shift 2
      ;;
    --elite)
      ELITE_COUNT="${2:-}"
      shift 2
      ;;
    --immigrants)
      IMMIGRANTS="${2:-}"
      shift 2
      ;;
    --mutation-rate)
      MUTATION_RATE="${2:-}"
      shift 2
      ;;
    --stall-limit)
      STALL_LIMIT="${2:-}"
      shift 2
      ;;
    --strategies)
      STRATEGIES_CSV="${2:-}"
      shift 2
      ;;
    --top-k)
      TOP_K="${2:-}"
      shift 2
      ;;
    --seed)
      SEED="${2:-}"
      shift 2
      ;;
    --min-nodes)
      MIN_NODES_OVERRIDE="${2:-}"
      shift 2
      ;;
    --max-nodes)
      MAX_NODES_OVERRIDE="${2:-}"
      shift 2
      ;;
    --min-gpus)
      MIN_GPUS_OVERRIDE="${2:-}"
      shift 2
      ;;
    --max-gpus)
      MAX_GPUS_OVERRIDE="${2:-}"
      shift 2
      ;;
    --min-cores)
      MIN_CORES_OVERRIDE="${2:-}"
      shift 2
      ;;
    --max-cores)
      MAX_CORES_OVERRIDE="${2:-}"
      shift 2
      ;;
    --nmc-bin)
      NMC_BIN="${2:-}"
      shift 2
      ;;
    --work-dir)
      WORK_DIR="${2:-}"
      KEEP_ARTIFACTS=1
      shift 2
      ;;
    --keep-artifacts)
      KEEP_ARTIFACTS=1
      shift
      ;;
    --no-keep-artifacts)
      KEEP_ARTIFACTS=0
      shift
      ;;
    --quiet)
      QUIET=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

case "${OBJECTIVE}" in
  balanced|throughput|efficiency|resilience) ;;
  *) die "Unsupported objective: ${OBJECTIVE}" ;;
esac

[[ "${POPULATION_SIZE}" =~ ^[0-9]+$ ]] || die "--population must be an integer"
[[ "${GENERATIONS}" =~ ^[0-9]+$ ]] || die "--generations must be an integer"
[[ "${ELITE_COUNT}" =~ ^[0-9]+$ ]] || die "--elite must be an integer"
[[ "${IMMIGRANTS}" =~ ^[0-9]+$ ]] || die "--immigrants must be an integer"
[[ "${STALL_LIMIT}" =~ ^[0-9]+$ ]] || die "--stall-limit must be an integer"
[[ "${TOP_K}" =~ ^[0-9]+$ ]] || die "--top-k must be an integer"

(( POPULATION_SIZE >= 4 )) || die "--population must be >= 4"
(( GENERATIONS >= 1 )) || die "--generations must be >= 1"
(( ELITE_COUNT >= 1 )) || die "--elite must be >= 1"
(( IMMIGRANTS >= 0 )) || die "--immigrants must be >= 0"
(( STALL_LIMIT >= 1 )) || die "--stall-limit must be >= 1"
(( TOP_K >= 1 )) || die "--top-k must be >= 1"

if [[ -n "${SEED}" ]]; then
  [[ "${SEED}" =~ ^[0-9]+$ ]] || die "--seed must be an integer"
  RANDOM="${SEED}"
fi

MUTATION_RATE_PPM="$(
  awk -v rate="${MUTATION_RATE}" '
    BEGIN {
      if (rate < 0 || rate > 1) {
        exit 1
      }
      printf "%d", int(rate * 1000000)
    }
  '
)" || die "--mutation-rate must be between 0 and 1"

IFS=',' read -r -a STRATEGIES_RAW <<<"${STRATEGIES_CSV}"
declare -a STRATEGIES=()
declare -A STRATEGY_SEEN=()
for strategy in "${STRATEGIES_RAW[@]}"; do
  strategy="$(normalize_strategy "${strategy}")"
  if [[ -z "${STRATEGY_SEEN[$strategy]+x}" ]]; then
    STRATEGY_SEEN["$strategy"]=1
    STRATEGIES+=("$strategy")
  fi
done
(( ${#STRATEGIES[@]} > 0 )) || die "No valid strategies resolved from --strategies"

if [[ -n "${WORK_DIR}" ]]; then
  mkdir -p "${WORK_DIR}"
else
  WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tracey-ga.XXXXXX")"
fi

RAW_DIR="${WORK_DIR}/raw"
SUMMARY_DIR="${WORK_DIR}/summaries"
mkdir -p "${RAW_DIR}" "${SUMMARY_DIR}"

CLI_STDERR_LOG="${WORK_DIR}/cli.stderr.log"
BASELINE_RAW_FILE="${WORK_DIR}/baseline.raw.json"
BASELINE_CONTEXT_FILE="${WORK_DIR}/baseline.context.json"
PROGRESS_FILE="${WORK_DIR}/progress.tsv"
LEADERBOARD_FILE="${WORK_DIR}/leaderboard.tsv"
TOP_FILE="${WORK_DIR}/top_candidates.tsv"
SUMMARY_JSON_FILE="${WORK_DIR}/summary.json"
BEST_RAW_FILE="${WORK_DIR}/best.raw.json"
BEST_SUMMARY_FILE="${WORK_DIR}/best.summary.json"

printf 'generation\tfitness\tnodes\tgpus\tcores\tstrategy\ttopology\trecommended_collective\n' >"${PROGRESS_FILE}"
: >"${CLI_STDERR_LOG}"

cleanup() {
  local status=$?
  if [[ "${KEEP_ARTIFACTS}" != "1" ]]; then
    rm -rf "${WORK_DIR}"
  fi
  exit "${status}"
}
trap cleanup EXIT

require_cmd jq
require_cmd awk
require_cmd sort
require_cmd mktemp
require_cmd "${NMC_BIN}"

build_tracey_cmd

note "Fetching live Tracey baseline from: ${NMC_BIN} ${TRACEY_CMD[*]}"
BASELINE_PAYLOAD="$(run_tracey_payload)" || die "Failed to fetch live Tracey baseline. See ${CLI_STDERR_LOG}"
printf '%s\n' "${BASELINE_PAYLOAD}" >"${BASELINE_RAW_FILE}"
build_baseline_context

jq -e '.continuum | type == "object" and (.baseline | type == "object")' "${BASELINE_CONTEXT_FILE}" >/dev/null \
  || die "Selected Tracey scope does not expose resource_forecast.continuum_expansion"

derive_bounds

ELITE_COUNT="$(min_int "${ELITE_COUNT}" "$(( POPULATION_SIZE - 1 ))")"
IMMIGRANTS="$(min_int "${IMMIGRANTS}" "$(( POPULATION_SIZE - ELITE_COUNT ))")"

declare -A FITNESS_CACHE=()
declare -A SUMMARY_FILE_CACHE=()
declare -a POPULATION=()
DUPLICATE_FALLBACK_NOTED=0
EVALUATE_SCORE=""

init_population

BEST_CANDIDATE=""
BEST_SCORE="-1000000000"
STALL_COUNT=0

for (( generation = 1; generation <= GENERATIONS; generation++ )); do
  rank_file="$(mktemp "${WORK_DIR}/rank.XXXXXX")"
  : >"${rank_file}"
  for candidate in "${POPULATION[@]}"; do
    evaluate_candidate "${candidate}"
    printf '%s\t%s\n' "${EVALUATE_SCORE}" "${candidate}" >>"${rank_file}"
  done
  mapfile -t ranked < <(sort -t$'\t' -k1,1nr -k2,2 "${rank_file}")
  rm -f "${rank_file}"

  (( ${#ranked[@]} > 0 )) || die "No ranked candidates were produced"

  current_best_line="${ranked[0]}"
  current_best_key="${current_best_line#*$'\t'}"
  current_best_score="${current_best_line%%$'\t'*}"
  current_best_summary="${SUMMARY_FILE_CACHE[$current_best_key]}"

  if [[ -z "${BEST_CANDIDATE}" ]] || float_gt "${current_best_score}" "${BEST_SCORE}"; then
    BEST_CANDIDATE="${current_best_key}"
    BEST_SCORE="${current_best_score}"
    STALL_COUNT=0
  else
    STALL_COUNT=$(( STALL_COUNT + 1 ))
  fi

  note ""
  emit_generation_progress "${generation}" "${current_best_summary}"

  if (( generation >= GENERATIONS )) || (( STALL_COUNT >= STALL_LIMIT )); then
    break
  fi

  declare -A next_seen=()
  declare -a NEXT_POPULATION=()

  for (( i = 0; i < ELITE_COUNT && i < ${#ranked[@]}; i++ )); do
    elite_key="${ranked[$i]#*$'\t'}"
    if [[ -z "${next_seen[$elite_key]+x}" ]]; then
      next_seen["$elite_key"]=1
      NEXT_POPULATION+=("$elite_key")
    fi
  done

  top_pool="$(max_int 1 "$(( ${#ranked[@]} / 2 ))")"
  target_non_random=$(( POPULATION_SIZE - IMMIGRANTS ))
  child_attempts=0
  max_child_attempts="$(unique_attempt_limit "${target_non_random}")"
  while (( ${#NEXT_POPULATION[@]} < target_non_random && child_attempts < max_child_attempts )); do
    parent_left="$(tournament_pick ranked "${top_pool}")"
    parent_right="$(tournament_pick ranked "${top_pool}")"
    child="$(make_child "${parent_left}" "${parent_right}" "${generation}")"
    child_attempts=$(( child_attempts + 1 ))
    if [[ -z "${next_seen[$child]+x}" ]]; then
      next_seen["$child"]=1
      NEXT_POPULATION+=("$child")
    fi
  done

  while (( ${#NEXT_POPULATION[@]} < target_non_random )); do
    note_duplicate_fallback
    parent_left="$(tournament_pick ranked "${top_pool}")"
    parent_right="$(tournament_pick ranked "${top_pool}")"
    NEXT_POPULATION+=("$(make_child "${parent_left}" "${parent_right}" "${generation}")")
  done

  immigrant_attempts=0
  max_immigrant_attempts="$(unique_attempt_limit "${IMMIGRANTS}")"
  while (( ${#NEXT_POPULATION[@]} < POPULATION_SIZE && immigrant_attempts < max_immigrant_attempts )); do
    immigrant="$(random_candidate)"
    immigrant_attempts=$(( immigrant_attempts + 1 ))
    if [[ -z "${next_seen[$immigrant]+x}" ]]; then
      next_seen["$immigrant"]=1
      NEXT_POPULATION+=("$immigrant")
    fi
  done

  while (( ${#NEXT_POPULATION[@]} < POPULATION_SIZE )); do
    note_duplicate_fallback
    NEXT_POPULATION+=("$(random_candidate)")
  done

  POPULATION=("${NEXT_POPULATION[@]}")
done

[[ -n "${BEST_CANDIDATE}" ]] || die "No best candidate was produced"

BEST_SUMMARY_SOURCE="${SUMMARY_FILE_CACHE[$BEST_CANDIDATE]}"
BEST_FITNESS_VALUE="$(jq -r '.fitness' "${BEST_SUMMARY_SOURCE}")"
if ! float_gt "${BEST_FITNESS_VALUE}" "-999999"; then
  die "All candidate evaluations failed; see ${CLI_STDERR_LOG}"
fi
BEST_COMMAND="$(build_best_command "${BEST_SUMMARY_SOURCE}")"
cp "${BEST_SUMMARY_SOURCE}" "${BEST_SUMMARY_FILE}"
cp "$(jq -r '.raw_payload_file' "${BEST_SUMMARY_SOURCE}")" "${BEST_RAW_FILE}" 2>/dev/null || true

write_leaderboard
write_summary_json "${BEST_COMMAND}"
print_final_report "${BEST_SUMMARY_FILE}" "${BEST_COMMAND}"
