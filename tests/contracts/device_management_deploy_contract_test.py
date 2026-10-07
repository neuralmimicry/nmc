#!/usr/bin/env python3
"""Protect the deployed Continuum inventory and service-health contract."""

from __future__ import annotations

import pathlib
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def main() -> int:
    playbook = (REPO_ROOT / "ansible" / "deploy.yml").read_text(encoding="utf-8")
    environment = (REPO_ROOT / "ansible" / "templates" / "nmc.env.j2").read_text(encoding="utf-8")
    requirements = {
        "inventory path has a protected state-directory default":
            'nmc_device_inventory_path: "{{ nmc_state_dir }}/device-inventory.json"' in playbook,
        "the process environment receives the configured inventory path":
            "NMC_DEVICE_INVENTORY_PATH={{ nmc_device_inventory_path }}" in environment,
        "only a missing registry is seeded":
            "Seed the versioned Continuum device registry when it is absent" in playbook
            and "force: false" in playbook,
        "the seed uses owner-only permissions":
            'mode: "0600"' in playbook,
        "the seed records the versioned inventory contract":
            '"schema_version": 1' in playbook
            and '"dependency_graph_revision": "bootstrap-empty-v1"' in playbook,
        "preserving an existing environment requires a root-only regular file":
            "nmc_preserve_env_file" in playbook
            and "nmc_preserved_env_stat.stat.mode == '0600'" in playbook
            and "nmc_preserved_env_stat.stat.pw_name == 'root'" in playbook,
        "preservation changes only the registry path and leaves the other secrets intact":
            "Update only the device registry path in a preserved runtime environment" in playbook
            and "regexp: '^NMC_DEVICE_INVENTORY_PATH='" in playbook,
        "deployment waits for the unauthenticated health route to return 200":
            'url: "http://127.0.0.1:{{ nmc_port }}/health"' in playbook
            and "status_code: 200" in playbook
            and "nmc_health_probe" in playbook
            and playbook.index("meta: flush_handlers") < playbook.index("url: \"http://127.0.0.1:{{ nmc_port }}/health\""),
    }
    failures = [description for description, passed in requirements.items() if not passed]
    if failures:
        print("[device-deploy-contract] failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("[device-deploy-contract] OK: private inventory seed, environment wiring and HTTP health gate are aligned.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
