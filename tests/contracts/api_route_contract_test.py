#!/usr/bin/env python3
"""
API contract test:
- Collect client endpoints referenced by CloudAPIClient.
- Collect server routes registered in APIRoutes.
- Fail if either side exposes routes the other side cannot address.
"""

from __future__ import annotations

import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Dict, List, Sequence

from server_route_sources import read_route_registration_source


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CLIENT_FILE = REPO_ROOT / "nmc_client" / "src" / "Core" / "CloudAPIClient.cpp"


@dataclass(frozen=True)
class Route:
    method: str
    path: str
    is_regex: bool


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[contract-test] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def strip_single_line_comments(src: str) -> str:
    lines: List[str] = []
    for line in src.splitlines():
        if line.lstrip().startswith("//"):
            continue
        lines.append(line)
    return "\n".join(lines)


def collect_client_routes(client_src: str) -> List[Route]:
    src = strip_single_line_comments(client_src)
    routes: Dict[tuple[str, str], Route] = {}

    def template_from_literal_parts(parts: Sequence[str]) -> str:
        template = parts[0]
        for part in parts[1:]:
            if template.endswith("/") and part.startswith("/"):
                template += "{var}" + part
            else:
                template += "{var}" + part
        return template

    # Calls where the first argument is an expression, e.g.:
    # cli->Get("/tracey/agents/" + id + "/analysis")
    call_first_arg_pattern = re.compile(
        r"(?:tempCli|cli)->(Get|Post|Delete|Put|Patch)\(\s*(.*?)\s*(?:,|\))",
        re.MULTILINE | re.DOTALL,
    )
    for method, first_arg_expr in call_first_arg_pattern.findall(src):
        literal_parts = re.findall(r"\"(/[^\"\?]*)\"", first_arg_expr)
        if not literal_parts:
            continue
        template = template_from_literal_parts(literal_parts)
        routes[(method, template)] = Route(method, template, False)

    # Named route variables (literal or concatenated), e.g.:
    # std::string path = "/k8s/list";
    # std::string path = "/tracey/agents/" + agentId + "/analysis";
    var_pattern = re.compile(r"std::string\s+([A-Za-z_]\w*)\s*=\s*(.+?);", re.MULTILINE | re.DOTALL)
    var_paths: Dict[str, set[str]] = {}
    for name, expr in var_pattern.findall(src):
        literal_parts = re.findall(r"\"(/[^\"\?]*)\"", expr)
        if not literal_parts:
            continue
        var_paths.setdefault(name, set()).add(template_from_literal_parts(literal_parts))

    # Variable-based calls, e.g. cli->Get(path)
    variable_call_pattern = re.compile(
        r"(?:tempCli|cli)->(Get|Post|Delete|Put|Patch)\(\s*([A-Za-z_]\w*)\s*[\),]",
        re.MULTILINE,
    )
    for method, var_name in variable_call_pattern.findall(src):
        paths = var_paths.get(var_name)
        if not paths:
            continue
        for path in paths:
            routes[(method, path)] = Route(method, path, False)

    if not routes:
        print("[contract-test] no client endpoints were detected", file=sys.stderr)
        sys.exit(1)

    return sorted(routes.values(), key=lambda item: (item.method, item.path))


def collect_server_routes(server_src: str) -> List[Route]:
    src = strip_single_line_comments(server_src)
    routes: Dict[tuple[str, str, bool], Route] = {}

    pattern = re.compile(
        r"svr\.(Get|Post|Delete|Put|Patch)\(\s*(R\"\((.*?)\)\"|\"([^\"]+)\")",
        re.MULTILINE | re.DOTALL,
    )
    for match in pattern.finditer(src):
        method = match.group(1)
        raw_regex = match.group(3)
        raw_static = match.group(4)
        if raw_static is not None:
            route = Route(method, raw_static, False)
        elif raw_regex is not None:
            route = Route(method, raw_regex, True)
        else:
            continue
        routes[(route.method, route.path, route.is_regex)] = route

    if not routes:
        print("[contract-test] no server routes were detected", file=sys.stderr)
        sys.exit(1)
    return sorted(routes.values(), key=lambda item: (item.method, item.path, item.is_regex))


def is_docs_route(route: Route) -> bool:
    if route.is_regex:
        return route.path.startswith("^/services/health/monitoring")
    return route.path in {"/", "/index.html", "/docs", "/login", "/logout", "/auth/login", "/auth/session"}


def endpoint_samples(path: str) -> Sequence[str]:
    # Dynamic client endpoints are represented by route prefixes that end with "/".
    if "{var}" in path:
        return (path.replace("{var}", "contract-id"),)
    if path.endswith("/"):
        return (path + "contract-id",)
    return (path,)


def route_matches_endpoint(route: Route, endpoint: str) -> bool:
    if route.is_regex:
        try:
            return re.fullmatch(route.path, endpoint) is not None
        except re.error as exc:
            print(f"[contract-test] invalid server regex route '{route.path}': {exc}", file=sys.stderr)
            sys.exit(1)

    # Allow client query strings while comparing path templates.
    return endpoint == route.path or endpoint.startswith(route.path + "?")


def client_has_route_for_server(server_route: Route, client_routes: Sequence[Route]) -> bool:
    for client_route in client_routes:
        if client_route.method != server_route.method:
            continue
        for sample in endpoint_samples(client_route.path):
            if route_matches_endpoint(server_route, sample):
                return True
    return False


def server_has_route_for_client(client_route: Route, server_routes: Sequence[Route]) -> bool:
    for server_route in server_routes:
        if server_route.method != client_route.method:
            continue
        if is_docs_route(server_route):
            continue
        for sample in endpoint_samples(client_route.path):
            if route_matches_endpoint(server_route, sample):
                return True
    return False


def main() -> int:
    client_src = read_text(CLIENT_FILE)
    server_src = read_route_registration_source()

    client_routes = collect_client_routes(client_src)
    server_routes = collect_server_routes(server_src)
    server_api_routes = [route for route in server_routes if not is_docs_route(route)]

    client_missing_on_server: List[Route] = []
    for client_route in client_routes:
        if not server_has_route_for_client(client_route, server_routes):
            client_missing_on_server.append(client_route)

    server_missing_on_client: List[Route] = []
    for server_route in server_api_routes:
        if not client_has_route_for_server(server_route, client_routes):
            server_missing_on_client.append(server_route)

    if client_missing_on_server or server_missing_on_client:
        print("[contract-test] client/server API contract mismatch detected:")
        if client_missing_on_server:
            print("  client route missing on server:")
            for route in client_missing_on_server:
                print(f"    - {route.method} {route.path}")
        if server_missing_on_client:
            print("  server route missing in client:")
            for route in server_missing_on_client:
                route_kind = "regex" if route.is_regex else "static"
                print(f"    - {route.method} {route.path} ({route_kind})")
        return 1

    print(
        f"[contract-test] OK: validated {len(client_routes)} client routes "
        f"against {len(server_api_routes)} server API routes."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
