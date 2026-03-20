#!/usr/bin/env python3
"""
Small API contract test:
- Collects client endpoints referenced by CloudAPIClient.
- Collects server routes registered in APIRoutes.
- Fails if a client endpoint does not match any server route.
"""

from __future__ import annotations

import pathlib
import re
import sys
from typing import List, Set, Tuple


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CLIENT_FILE = REPO_ROOT / "nmc_client" / "src" / "Core" / "CloudAPIClient.cpp"
SERVER_FILE = REPO_ROOT / "nmc_server" / "src" / "Core" / "APIRoutes.cpp"


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[contract-test] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def collect_client_endpoints(client_src: str) -> List[str]:
    endpoints: Set[str] = set()

    # Direct HTTP calls with literal paths.
    direct_call_pattern = re.compile(
        r'(?:tempCli|cli)->(?:Get|Post|Delete|Put|Patch)\(\s*"(/[^"]*)"',
        re.MULTILINE,
    )
    endpoints.update(direct_call_pattern.findall(client_src))

    # Local `path` variables used for request routes.
    path_assignment_pattern = re.compile(
        r'std::string\s+path\s*=\s*"(/[^"]*)";',
        re.MULTILINE,
    )
    endpoints.update(path_assignment_pattern.findall(client_src))

    # Fallback probe chain literals.
    probe_chain_pattern = re.compile(r'"(/health|/server/version|/connections/status)"')
    endpoints.update(probe_chain_pattern.findall(client_src))

    # Keep only route-like values.
    filtered = sorted({value.strip() for value in endpoints if value.strip().startswith("/")})
    if not filtered:
        print("[contract-test] no client endpoints were detected", file=sys.stderr)
        sys.exit(1)
    return filtered


def collect_server_routes(server_src: str) -> Tuple[Set[str], List[re.Pattern[str]]]:
    static_routes: Set[str] = set()
    regex_routes: List[re.Pattern[str]] = []

    route_pattern = re.compile(
        r'svr\.(?:Get|Post|Delete|Put|Patch)\(\s*(R"\((.*?)\)"|"([^"]+)")',
        re.MULTILINE | re.DOTALL,
    )
    for match in route_pattern.finditer(server_src):
        raw_regex = match.group(2)
        raw_static = match.group(3)
        if raw_static is not None:
            static_routes.add(raw_static)
            continue
        if raw_regex is None:
            continue
        try:
            regex_routes.append(re.compile(raw_regex))
        except re.error as exc:
            print(f"[contract-test] invalid server regex route '{raw_regex}': {exc}", file=sys.stderr)
            sys.exit(1)

    if not static_routes and not regex_routes:
        print("[contract-test] no server routes were detected", file=sys.stderr)
        sys.exit(1)
    return static_routes, regex_routes


def endpoint_matches_server_route(endpoint: str, static_routes: Set[str], regex_routes: List[re.Pattern[str]]) -> bool:
    # Dynamic client endpoints are assembled as prefix + identifier.
    sample = endpoint + "contract-id" if endpoint.endswith("/") else endpoint

    if sample in static_routes:
        return True
    for route_re in regex_routes:
        if route_re.fullmatch(sample):
            return True
    return False


def main() -> int:
    client_src = read_text(CLIENT_FILE)
    server_src = read_text(SERVER_FILE)

    client_endpoints = collect_client_endpoints(client_src)
    static_routes, regex_routes = collect_server_routes(server_src)

    missing: List[str] = []
    for endpoint in client_endpoints:
        if not endpoint_matches_server_route(endpoint, static_routes, regex_routes):
            missing.append(endpoint)

    if missing:
        print("[contract-test] client/server API contract mismatch detected:")
        for endpoint in missing:
            print(f"  - missing server route for client endpoint: {endpoint}")
        return 1

    print(
        f"[contract-test] OK: validated {len(client_endpoints)} client endpoints "
        f"against {len(static_routes)} static and {len(regex_routes)} regex server routes."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
