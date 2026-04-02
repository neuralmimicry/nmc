#!/usr/bin/env python3
"""
Node recruitment capacity contract test:
- Ensure /node/recruit performs a same-hardware capacity assessment.
- Ensure the guarded rejection message remains stable.
- Ensure built-in docs mention the same-hardware 409 rejection path.
"""

from __future__ import annotations

import pathlib
import re
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVER_FILE = REPO_ROOT / "nmc_server" / "src" / "Core" / "APIRoutes.cpp"
DOC_FILE = REPO_ROOT / "nmc_server" / "src" / "docs" / "node.html"


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[node-recruit-capacity-test] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def main() -> int:
    server_src = read_text(SERVER_FILE)
    doc_src = read_text(DOC_FILE)

    handle_match = re.search(
        r"void\s+APIRoutes::handleRecruitNode\(const httplib::Request& req, httplib::Response& res\)\s*\{(.*)\n\s*}\n\n} // namespace NMC::Server",
        server_src,
        re.DOTALL,
    )
    if not handle_match:
        print("[node-recruit-capacity-test] failed to locate handleRecruitNode implementation", file=sys.stderr)
        return 1

    handle_body = handle_match.group(1)
    failures: list[str] = []

    if "assessRecruitCapacity(host)" not in handle_body:
        failures.append("handleRecruitNode no longer calls assessRecruitCapacity(host)")
    if '"Insufficient local capacity for same-hardware node recruitment."' not in handle_body:
        failures.append("handleRecruitNode missing stable same-hardware capacity rejection message")
    if '"capacity_check"' not in handle_body:
        failures.append("handleRecruitNode no longer exposes capacity_check diagnostics")

    lowered_doc = doc_src.lower()
    if "same-hardware capacity admission check" not in lowered_doc:
        failures.append("node API docs do not mention same-hardware capacity admission")
    if "http 409" not in lowered_doc:
        failures.append("node API docs do not mention the 409 same-hardware capacity rejection")

    if failures:
        print("[node-recruit-capacity-test] node recruitment capacity contract violations detected:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("[node-recruit-capacity-test] OK: same-hardware capacity guard is wired into /node/recruit and documented.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
