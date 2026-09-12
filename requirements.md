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
Continuum depends on shared data services but no clear persistent-storage profile was inferred from Ansible. Confirm PVCs or durable mounts before further automation.

Authoritative delivery constraints (mandatory; implement and verify these, do not merely describe them):
- No structured delivery constraints were supplied; follow the work-item summary exactly.

Plan JSON:
{"action":"verify_persistent_storage","finding_id":"85d6b0f6-85b9-4976-96c0-6284819c9415","finding_key":"storage_profile:continuum","service":"continuum"}

Planner guidance (advisory; it must not weaken or contradict the authoritative work-item requirements):
Overview: This work item verifies the persistent storage strategy for the Continuum service to ensure data durability and state consistency. The operation focuses on inspecting the repository and Ansible automation to confirm the absence of a defined persistent volume claim (PVC) or durable mount profile. A minimal regression test baseline will be established to enable safe autonomous changes without destructive modifications.

Requirements Register:
- REQ-001: Inspect repository state at /srv/neuralmimicry/nmc to identify existing test coverage and runtime job configurations.
- REQ-002: Document observed evidence regarding storage readiness and uncertainty levels to inform the scope of the verification operation.
- REQ-003: Execute cargo fmt --check and cargo check to ensure code style compliance and compilation readiness.
- REQ-004: Run cargo test to validate the current baseline stability without modifying production logic or existing files.
- REQ-005: Review Ansible playbooks in /srv/swarmhpc/ansible/roles to confirm the absence of PVC definitions or durable mount configurations.
- REQ-006: Create a minimal smoke test file in the tests directory to verify core service health and storage mount readiness.
- REQ-007: Execute the new smoke test using python -m pytest to confirm the baseline is stable, non-destructive, and passes locally.
- REQ-008: Prepare a canary rollout plan to apply any identified storage configuration fixes to a subset of the Continuum service.
- REQ-009: Verify post-rollout readiness health window and automatic rollback on degradation for the canary deployment.
- REQ-010: Ensure all changes are scoped, resilient, and secure, leaving unrelated files untouched.
- REQ-011: Confirm that no destructive commands are executed during the initial inspection phase.
- REQ-012: Validate that the acceptance signal proves the gap is closed before proceeding to full rollout.


Protected rollout contract (mandatory): capture a fresh readiness baseline before any change; use the selected canary or red_green strategy; verify health throughout the post-rollout window; if health or verification degrades, automatically revert the exact produced commit without rewriting history, rerun tests and GitHub Actions, and verify recovery.