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

from server_route_sources import read_route_registration_source


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DOC_FILE = REPO_ROOT / "nmc_server" / "src" / "docs" / "node.html"


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[node-recruit-capacity-test] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def extract_function_body(src: str, signature_pattern: str) -> str | None:
    match = re.search(signature_pattern, src)
    if not match:
        return None

    brace_index = match.end() - 1
    depth = 0
    for idx in range(brace_index, len(src)):
        ch = src[idx]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return src[brace_index + 1 : idx]
    return None


def main() -> int:
    server_src = read_route_registration_source()
    doc_src = read_text(DOC_FILE)

    handle_body = extract_function_body(
        server_src,
        r"void\s+APIRoutes::handleRecruitNode\(const httplib::Request& req, httplib::Response& res\)\s*\{",
    )
    if handle_body is None:
        print("[node-recruit-capacity-test] failed to locate handleRecruitNode implementation", file=sys.stderr)
        return 1

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
