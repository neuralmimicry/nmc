Overview: Improve Continuum through the Conductor execution loop.

Delivery Context:
- Current stage: development
- Validated stages: none
- Rollout strategy: canary

Requirements Register:
- REQ-001: Inspect and record current repository, runtime, or job evidence before selecting an operation.
- REQ-002: Implement only the scoped change, job update, or progress-monitoring action supported by that evidence.
- REQ-003: Preserve secure, resilient behaviour and avoid destructive commands.
- REQ-004: Update or add tests covering the changed path, or provide the relevant live operational check.
- REQ-005: Run verification commands and report the outcome.
- REQ-006: Leave unrelated files untouched.
- REQ-007: Record rollback/recovery steps and the acceptance signal proving the gap is closed.
- REQ-008: Preserve staged progression and rollout governance metadata.
- REQ-013: When runtime rollout or restart work is needed, use the available Ansible automation context: {"ansible_root":"/srv/swarmhpc/ansible","config_path":"/srv/swarmhpc/ansible/ansible.cfg","host_targets":[],"hosts":[],"inventory_path":"/srv/swarmhpc/ansible/inventory/hosts.ini","playbooks":["host_vars"],"repo_root":"/srv/swarmhpc","roles_path":"/srv/swarmhpc/ansible/roles","secrets_root":"/srv/swarmhpc/ansible/.secrets"}.

Work Item Summary:
Continuum shows a sustained worsening control-plane trend across 24 samples. Tighten recruitment, placement, or backlog handling before orchestration latency compounds. Current headline: avg_network_queue_pressure=1.00 (worsening), active_remote_nodes=0.00 (stable), adaptive_overall_score=0.94 (stable)

Authoritative delivery constraints (mandatory; implement and verify these, do not merely describe them):
- No structured delivery constraints were supplied; follow the work-item summary exactly.

Plan JSON:
{"action":"correct_continuum_trend","finding_id":"16d92b95-5664-4228-ad72-4acd815c2c70","finding_key":"continuum_worsening_trend","headline":"continuum trend is worsening via avg_network_queue_pressure (1.00 latest, 0.12 slope).","metrics":[{"average":0.8452795488293586,"direction":"worsening","latest":1.0,"metric_name":"avg_network_queue_pressure","slope":0.12490220007472042},{"average":0.0,"direction":"stable","latest":0.0,"metric_name":"active_remote_nodes","slope":0.0},{"average":0.9435965796709248,"direction":"stable","latest":0.943596579670925,"metric_name":"adaptive_overall_score","slope":0.0},{"average":1.0,"direction":"stable","latest":1.0,"metric_name":"adaptive_placement_score","slope":0.0},{"average":0.9142499999999996,"direction":"stable","latest":0.91425,"metric_name":"adaptive_readiness_score","slope":0.0},{"average":1.0,"direction":"stable","latest":1.0,"metric_name":"avg_network_latency_pressure","slope":0.0},{"average":0.10000000000000003,"direction":"stable","latest":0.1,"metric_name":"capability_count","slope":0.0},{"average":0.040000000000000015,"direction":"stable","latest":0.04,"metric_name":"dependency_count","slope":0.0},{"average":0.0,"direction":"stable","latest":0.0,"metric_name":"health_severity","slope":0.0},{"average":1.0,"direction":"stable","latest":1.0,"metric_name":"preferred_gpus","slope":0.0},{"average":0.11916666666666674,"direction":"stable","latest":0.12,"metric_name":"preferred_hosts","slope":0.0},{"average":1.0,"direction":"stable","latest":1.0,"metric_name":"pressure_score","slope":0.0},{"average":0.0,"direction":"stable","latest":0.0,"metric_name":"requested_remote_nodes","slope":0.0}],"sample_count":24}