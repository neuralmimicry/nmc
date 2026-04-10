#!/usr/bin/env python3
"""
Server safety contract test:
- Ensure API routes (except docs + unauthenticated health) use the shared guard.
- Ensure guard invocation exists in each protected route body.
- Ensure sensitive endpoint prefixes remain excluded from body logging.
"""

from __future__ import annotations

import pathlib
import re
import sys
from dataclasses import dataclass
from typing import List


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVER_FILE = REPO_ROOT / "nmc_server" / "src" / "Core" / "APIRoutes.cpp"

DOCS_STATIC_PATHS = {"/", "/index.html", "/docs", "/login", "/logout", "/auth/login"}
REDACTION_MINIMUM_PREFIXES = {
    "/ssh/create",
    "/vm/create",
    "/model/upload",
    "/connections/make",
    "/openshift/clusters/request",
    "/openstack/clusters/request",
    "/proxmox/clusters/request",
    "/node/recruit",
}


@dataclass(frozen=True)
class RouteRegistration:
    method: str
    path: str
    capture: str
    body: str


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[safety-test] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def collect_routes(src: str) -> List[RouteRegistration]:
    route_start = re.compile(
        r'^\s*svr\.(Get|Post|Delete|Put|Patch)\(\s*(R"\((.*?)\)"|"([^"]+)")\s*,\s*\[([^\]]*)\]\(const httplib::Request& req, httplib::Response& res\)\s*\{',
    )
    route_end = re.compile(r"^\s*}\);\s*$")
    lines = src.splitlines()
    routes: List[RouteRegistration] = []

    i = 0
    while i < len(lines):
        match = route_start.search(lines[i])
        if not match:
            i += 1
            continue

        method = match.group(1)
        raw_regex = match.group(3)
        raw_static = match.group(4)
        path = raw_regex if raw_regex is not None else raw_static
        capture = match.group(5)

        body_lines: List[str] = []
        j = i + 1
        while j < len(lines):
            if route_end.match(lines[j]):
                break
            body_lines.append(lines[j])
            j += 1

        routes.append(
            RouteRegistration(
                method=method,
                path=path,
                capture=capture,
                body="\n".join(body_lines),
            )
        )
        i = j + 1

    if not routes:
        print("[safety-test] no route registrations were detected", file=sys.stderr)
        sys.exit(1)
    return routes


def is_docs_or_public_health(path: str) -> bool:
    if path in DOCS_STATIC_PATHS:
        return True
    if path == "/health":
        return True
    return path.startswith("^/services/health/monitoring")


def ensure_guarded_routes(routes: List[RouteRegistration]) -> List[str]:
    guard_check = re.compile(r"if\s*\(\s*!guard\(req,\s*res\)\s*\)\s*return\s*;")
    failures: List[str] = []
    for route in routes:
        if is_docs_or_public_health(route.path):
            continue
        if "guard" not in route.capture:
            failures.append(f"{route.method} {route.path}: missing 'guard' in lambda capture list")
            continue
        if not guard_check.search(route.body):
            failures.append(f"{route.method} {route.path}: missing 'if (!guard(req, res)) return;' check")
    return failures


def extract_redaction_prefixes(src: str) -> List[str]:
    section = re.search(
        r"bool\s+APIRoutes::shouldLogBody\(const std::string& path\)\s+const\s*\{(.*?)\n\s*return true;\n\s*\}",
        src,
        re.DOTALL,
    )
    if not section:
        print("[safety-test] failed to locate shouldLogBody implementation", file=sys.stderr)
        sys.exit(1)
    prefixes = sorted(set(re.findall(r'"([^"]+)"', section.group(1))))
    if not prefixes:
        print("[safety-test] no redacted prefixes detected in shouldLogBody", file=sys.stderr)
        sys.exit(1)
    if "path.rfind(prefix, 0) == 0" not in section.group(1):
        print("[safety-test] shouldLogBody no longer performs prefix-based path redaction checks", file=sys.stderr)
        sys.exit(1)
    return prefixes


def main() -> int:
    src = read_text(SERVER_FILE)
    routes = collect_routes(src)
    guard_failures = ensure_guarded_routes(routes)

    redacted_prefixes = set(extract_redaction_prefixes(src))
    missing_redactions = sorted(REDACTION_MINIMUM_PREFIXES - redacted_prefixes)

    if guard_failures or missing_redactions:
        print("[safety-test] server safety contract violations detected:")
        if guard_failures:
            print("  guarded route failures:")
            for failure in guard_failures:
                print(f"    - {failure}")
        if missing_redactions:
            print("  missing required redaction prefixes:")
            for prefix in missing_redactions:
                print(f"    - {prefix}")
        return 1

    protected_route_count = sum(1 for route in routes if not is_docs_or_public_health(route.path))
    print(
        f"[safety-test] OK: validated {protected_route_count} protected routes and "
        f"{len(redacted_prefixes)} body-redaction prefixes."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
