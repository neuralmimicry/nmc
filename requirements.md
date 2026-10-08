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
Continuum shows a sustained worsening control-plane trend across 24 samples. Tighten recruitment, placement, or backlog handling before orchestration latency compounds. Current headline: health_severity=0.50 (worsening), capability_count=0.10 (stable), dependency_count=0.04 (stable)

Authoritative delivery constraints (mandatory; implement and verify these, do not merely describe them):
- No structured delivery constraints were supplied; follow the work-item summary exactly.

Plan JSON:
{"action":"correct_continuum_trend","finding_id":"fec50103-2709-4a58-82f2-b07e3d09b75d","finding_key":"continuum_worsening_trend","headline":"continuum trend is worsening via health_severity (0.50 latest, 0.50 slope).","metrics":[{"average":0.020833333333333332,"direction":"worsening","latest":0.5,"metric_name":"health_severity","slope":0.5},{"average":0.10000000000000003,"direction":"stable","latest":0.1,"metric_name":"capability_count","slope":0.0},{"average":0.040000000000000015,"direction":"stable","latest":0.04,"metric_name":"dependency_count","slope":0.0},{"average":0.0,"direction":"stable","latest":0.0,"metric_name":"pressure_score","slope":0.0}],"sample_count":24}