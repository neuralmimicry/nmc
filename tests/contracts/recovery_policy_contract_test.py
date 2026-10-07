#!/usr/bin/env python3
"""Keep workload recovery opt-in, explicitly scoped and documented."""

from __future__ import annotations

import pathlib
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    sources = {
        "handler": (REPO_ROOT / "nmc_server/src/Core/K8sHandlers.h").read_text(encoding="utf-8"),
        "server": (REPO_ROOT / "nmc_server/src/Core/K8sHandlers.cpp").read_text(encoding="utf-8"),
        "recovery": (REPO_ROOT / "nmc_server/src/Core/K8sHandlers_Recovery.cpp").read_text(encoding="utf-8"),
        "playbook": (REPO_ROOT / "ansible/deploy.yml").read_text(encoding="utf-8"),
        "environment": (REPO_ROOT / "ansible/templates/nmc.env.j2").read_text(encoding="utf-8"),
        "documentation": (REPO_ROOT / "docs/RECOVERY.md").read_text(encoding="utf-8"),
    }
    restart_handler = sources["recovery"].split("void K8sHandlers::handleRestartDeployment", 1)[-1]
    requirements = {
        "recovery is disabled in the server constructor by default": "bool recoveryEnabled{false};" in sources["handler"],
        "only an explicit true or 1 value opens the server gate": '== "true" || normalisedRecoveryEnabled == "1"' in sources["server"],
        "the preflight reports when restart policy is disabled": '"recovery_disabled"' in sources["recovery"],
        "restart checks the policy before Kubernetes reads or writes": 0 <= restart_handler.find('if (!recoveryEnabled)') < restart_handler.find('getGenericClient("apps", "v1", "deployments")'),
        "Ansible recovery defaults off": "nmc_recovery_enabled: false" in sources["playbook"],
        "Ansible requires a cluster ID before enabling recovery": "nmc_k8s_cluster_id | length > 0" in sources["playbook"],
        "the environment template carries cluster scope and recovery policy": "NMC_K8S_CLUSTER_ID=" in sources["environment"] and "NMC_RECOVERY_ENABLED=" in sources["environment"],
        "operator documentation describes the guarded workflow": "NMC_RECOVERY_ENABLED=true" in sources["documentation"] and "nmc_k8s_cluster_id" in sources["documentation"],
    }
    failures = [description for description, passed in requirements.items() if not passed]
    if failures:
        for failure in failures:
            print(f"[recovery-policy-contract] FAIL: {failure}", file=sys.stderr)
        return 1
    print(f"[recovery-policy-contract] OK: {len(requirements)} recovery safety requirements")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
