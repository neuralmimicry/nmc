Overview: Improve Nemoclaw through the Conductor execution loop.

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
- REQ-013: When runtime rollout or restart work is needed, use the available Ansible automation context: {"ansible_root":"/srv/swarmhpc/ansible","config_path":"/srv/swarmhpc/ansible/ansible.cfg","host_targets":["rk1"],"hosts":["spirit"],"inventory_path":"/srv/swarmhpc/ansible/inventory/hosts.ini","playbooks":["continuum_tenant_nemoclaw_site.yml"],"repo_root":"/srv/swarmhpc","roles_path":"/srv/swarmhpc/ansible/roles","secrets_root":"/srv/swarmhpc/ansible/.secrets"}.

Work Item Summary:
nmc is linked to live services but no obvious test capability was discovered in the repository inventory. Establish at least a minimal regression or smoke-test baseline before deeper autonomous changes.

Authoritative delivery constraints (mandatory; implement and verify these, do not merely describe them):
- No structured delivery constraints were supplied; follow the work-item summary exactly.

Plan JSON:
{"action":"establish_repository_test_baseline","finding_id":"201c1396-3e7c-4c5b-9929-878bc6ace4d8","finding_key":"repository_test_baseline:neuralmimicry/nmc","linked_services":["nemoclaw"],"repository":"neuralmimicry/nmc"}

Planner guidance (advisory; it must not weaken or contradict the authoritative work-item requirements):
Overview: This work item establishes a test baseline for the nmc service, which is linked to live services but currently lacks executable project-native validation. The baseline will enable safe autonomous changes by providing a measurable standard for regression and smoke testing.

Requirements Register:
- REQ-001: Inspect the NeuralMimicry/nmc repository to confirm path accessibility, current branch state, and existing test coverage gaps.
- REQ-002: Query the local K3s cluster status to verify control-plane connectivity and worker node probes.
- REQ-003: Review service health logs and metrics on host 'spirit' to identify specific degradation causes or operational context.
- REQ-004: Validate Ansible syntax and inventory without applying changes using the continuum_tenant_nemoclaw_site.yml playbook.
- REQ-005: Generate a test inventory using pytest --collect-only to identify missing or failing tests.
- REQ-006: Execute a subset of critical tests to establish a baseline pass rate and visualise coverage gaps.
- REQ-007: Document the baseline metrics and any observed degradation in the project runtime notes.

Rollout Strategy: Canary
Verification: Successful execution of the critical test subset and stable cluster metrics.


Protected rollout contract (mandatory): capture a fresh readiness baseline before any change; use the selected canary or red_green strategy; verify health throughout the post-rollout window; if health or verification degrades, automatically revert the exact produced commit without rewriting history, rerun tests and GitHub Actions, and verify recovery.