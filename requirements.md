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
- REQ-009: Capture a fresh protected-target readiness baseline before any change.
- REQ-010: Use the selected canary or red-green rollout strategy and verify the post-rollout health window.
- REQ-011: Automatically revert the exact produced commit without rewriting history if health or verification degrades.
- REQ-012: Verify rollback readiness and recovery before finalising the delivery.
- REQ-013: When runtime rollout or restart work is needed, use the available Ansible automation context: {"ansible_root":"/srv/swarmhpc/ansible","config_path":"/srv/swarmhpc/ansible/ansible.cfg","host_targets":[],"hosts":[],"inventory_path":"/srv/swarmhpc/ansible/inventory/hosts.ini","playbooks":["host_vars"],"repo_root":"/srv/swarmhpc","roles_path":"/srv/swarmhpc/ansible/roles","secrets_root":"/srv/swarmhpc/ansible/.secrets"}.

Work Item Summary:
Continuum shows a sustained worsening control-plane trend across 24 samples. Tighten recruitment, placement, or backlog handling before orchestration latency compounds. Current headline: health_severity=0.50 (worsening), capability_count=0.10 (stable), dependency_count=0.04 (stable)

Authoritative delivery constraints (mandatory; implement and verify these, do not merely describe them):
- No structured delivery constraints were supplied; follow the work-item summary exactly.

Plan JSON:
{"action":"correct_continuum_trend","finding_id":"40098281-654e-42dd-8086-bf653bbb1bd7","finding_key":"continuum_worsening_trend","headline":"continuum trend is worsening via health_severity (0.50 latest, 0.50 slope).","metrics":[{"average":0.08333333333333333,"direction":"worsening","latest":0.5,"metric_name":"health_severity","slope":0.5},{"average":0.10000000000000003,"direction":"stable","latest":0.1,"metric_name":"capability_count","slope":0.0},{"average":0.040000000000000015,"direction":"stable","latest":0.04,"metric_name":"dependency_count","slope":0.0},{"average":0.0,"direction":"stable","latest":0.0,"metric_name":"pressure_score","slope":0.0}],"sample_count":24}

Planner guidance (advisory; it must not weaken or contradict the authoritative work-item requirements):
Overview: This work item addresses the sustained worsening control-plane trend in Continuum by tightening recruitment, placement, or backlog handling mechanisms. The objective is to restore health_severity stability before orchestration latency compounds, ensuring non-destructive implementation within the existing governed delivery pipeline.

Requirements Register:
- REQ-001: Inspect the current Continuum repository state at /srv/neuralmimicry/nmc to confirm branch state and existing file structure integrity.
- REQ-002: Execute Ansible syntax validation on host_vars playbooks to ensure no unintended changes are applied to the live inventory.
- REQ-003: Verify K3s control-plane connectivity and worker node probe paths to isolate specific degradation causes linked to health_severity.
- REQ-004: Review service health logs and metrics to identify specific degradation causes before proceeding with remediation logic.
- REQ-005: Modify Continuum recruitment or placement logic to tighten backlog handling, ensuring changes are scoped and resilient.
- REQ-006: Execute code formatting and compilation checks to validate changes without introducing new dependencies or breaking existing builds.
- REQ-007: Initiate a canary staged rollout to a subset of hosts to validate the remediation under live load conditions.
- REQ-008: Monitor health_severity and capability_count metrics during the post-rollout readiness health window for any degradation.
- REQ-009: Implement automatic rollback procedures if health metrics degrade beyond acceptable thresholds during the canary phase.
- REQ-010: Verify rollback readiness and recovery procedures to ensure rapid restoration of service if the remediation fails.
- REQ-011: Document the acceptance signal proving the gap is closed, including updated metrics and confirmation of trend cessation.
- REQ-012: Ensure all changes leave unrelated files untouched and adhere to staged delivery gates to maintain governed change integrity.


Protected rollout contract (mandatory): capture a fresh readiness baseline before any change; use the selected canary or red_green strategy; verify health throughout the post-rollout window; if health or verification degrades, automatically revert the exact produced commit without rewriting history, rerun tests and GitHub Actions, and verify recovery.