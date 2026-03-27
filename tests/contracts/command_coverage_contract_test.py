#!/usr/bin/env python3
"""
Command-level contract test:
- Every server API route must be addressable by at least one CloudAPIClient method.
- Every addressable server API route must be reachable via at least one registered CLI command path.

This enforces end-to-end alignment at the command surface:
server route -> CloudAPIClient method -> CLI command.
"""

from __future__ import annotations

import pathlib
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from typing import DefaultDict, Dict, Iterable, List, Sequence, Set, Tuple


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVER_FILE = REPO_ROOT / "nmc_server" / "src" / "Core" / "APIRoutes.cpp"
CLOUD_API_FILE = REPO_ROOT / "nmc_client" / "src" / "Core" / "CloudAPIClient.cpp"
COMMANDS_DIR = REPO_ROOT / "nmc_client" / "src" / "Commands"
MAIN_FILE = REPO_ROOT / "nmc_client" / "src" / "main.cpp"


@dataclass(frozen=True)
class Route:
    method: str
    path: str
    is_regex: bool


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[command-coverage] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def strip_single_line_comments(src: str) -> str:
    lines: List[str] = []
    for line in src.splitlines():
        if line.lstrip().startswith("//"):
            continue
        lines.append(line)
    return "\n".join(lines)


def extract_braced_block(src: str, open_brace_index: int) -> Tuple[str, int]:
    if open_brace_index < 0 or open_brace_index >= len(src) or src[open_brace_index] != "{":
        raise ValueError("invalid opening brace index")
    depth = 0
    for idx in range(open_brace_index, len(src)):
        ch = src[idx]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return src[open_brace_index + 1 : idx], idx
    raise ValueError("unterminated brace block")


def template_from_literal_parts(parts: Sequence[str]) -> str:
    template = parts[0]
    for part in parts[1:]:
        # Dynamic concatenations are represented as "{var}" placeholders.
        template += "{var}" + part
    return template


def collect_server_routes(server_src: str) -> List[Route]:
    src = strip_single_line_comments(server_src)
    routes: Dict[Tuple[str, str, bool], Route] = {}
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
        print("[command-coverage] no server routes were detected", file=sys.stderr)
        sys.exit(1)
    return sorted(routes.values(), key=lambda item: (item.method, item.path, item.is_regex))


def collect_cloud_method_routes(cloud_src: str) -> Dict[str, List[Route]]:
    src = strip_single_line_comments(cloud_src)
    method_pattern = re.compile(
        r"Models::CloudResponse\s+CloudAPIClient::([A-Za-z_]\w*)\s*\([^)]*\)\s*\{",
        re.MULTILINE,
    )
    method_routes: Dict[str, Set[Route]] = {}

    for match in method_pattern.finditer(src):
        method_name = match.group(1)
        try:
            body, _ = extract_braced_block(src, match.end() - 1)
        except ValueError:
            print(f"[command-coverage] failed to parse method body for CloudAPIClient::{method_name}", file=sys.stderr)
            sys.exit(1)

        routes: Set[Route] = set()
        var_paths: DefaultDict[str, Set[str]] = defaultdict(set)

        var_pattern = re.compile(
            r"(?:const\s+)?std::string\s+([A-Za-z_]\w*)\s*=\s*(.+?);",
            re.MULTILINE | re.DOTALL,
        )
        for var_name, expr in var_pattern.findall(body):
            literal_parts = re.findall(r"\"(/[^\"\?]*)\"", expr)
            if not literal_parts:
                continue
            var_paths[var_name].add(template_from_literal_parts(literal_parts))

        call_first_arg_pattern = re.compile(
            r"(?:tempCli|cli)->(Get|Post|Delete|Put|Patch)\(\s*(.*?)\s*(?:,|\))",
            re.MULTILINE | re.DOTALL,
        )
        for http_method, first_arg_expr in call_first_arg_pattern.findall(body):
            literal_parts = re.findall(r"\"(/[^\"\?]*)\"", first_arg_expr)
            if literal_parts:
                routes.add(Route(http_method, template_from_literal_parts(literal_parts), False))
                continue

            var_match = re.fullmatch(r"([A-Za-z_]\w*)", first_arg_expr.strip())
            if not var_match:
                continue
            var_name = var_match.group(1)
            for path in var_paths.get(var_name, set()):
                routes.add(Route(http_method, path, False))

        if routes:
            method_routes.setdefault(method_name, set()).update(routes)

    if not method_routes:
        print("[command-coverage] no CloudAPIClient route mappings were detected", file=sys.stderr)
        sys.exit(1)

    return {
        method: sorted(route_set, key=lambda item: (item.method, item.path))
        for method, route_set in method_routes.items()
    }


def collect_command_tokens(commands_dir: pathlib.Path) -> Dict[str, str]:
    class_tokens: Dict[str, str] = {}
    ctor_pattern = re.compile(
        r"([A-Za-z_]\w*)::\1\s*\([^)]*\)\s*:\s*BaseCommand\(\s*\"([^\"]+)\"",
        re.MULTILINE,
    )
    for cpp_file in sorted(commands_dir.glob("*.cpp")):
        src = read_text(cpp_file)
        for class_name, token in ctor_pattern.findall(src):
            class_tokens[class_name] = token
    if not class_tokens:
        print("[command-coverage] no command class tokens were detected", file=sys.stderr)
        sys.exit(1)
    return class_tokens


def collect_command_graph(main_src: str) -> Tuple[Set[str], Dict[str, Set[str]]]:
    src = strip_single_line_comments(main_src)

    var_to_class: Dict[str, str] = {}
    for var_name, class_name in re.findall(
        r"auto\s+([A-Za-z_]\w*)\s*=\s*std::make_shared<NMC::Commands::([A-Za-z_]\w*)>\(apiClient\)\s*;",
        src,
    ):
        var_to_class[var_name] = class_name

    edges: DefaultDict[str, Set[str]] = defaultdict(set)
    for parent_var, child_class in re.findall(
        r"([A-Za-z_]\w*)->addSubcommand\(\s*std::make_shared<NMC::Commands::([A-Za-z_]\w*)>\(apiClient\)\s*\)\s*;",
        src,
    ):
        parent_class = var_to_class.get(parent_var)
        if not parent_class:
            continue
        edges[parent_class].add(child_class)

    roots: Set[str] = set()
    for root_var in re.findall(r"parser\.registerCommand\(\s*([A-Za-z_]\w*)\s*\)\s*;", src):
        class_name = var_to_class.get(root_var)
        if class_name:
            roots.add(class_name)
    for inline_class in re.findall(
        r"parser\.registerCommand\(\s*std::make_shared<NMC::Commands::([A-Za-z_]\w*)>\(apiClient\)\s*\)\s*;",
        src,
    ):
        roots.add(inline_class)

    if not roots:
        print("[command-coverage] no registered root commands were detected in main.cpp", file=sys.stderr)
        sys.exit(1)
    return roots, edges


def build_class_cli_paths(
    roots: Set[str], edges: Dict[str, Set[str]], class_tokens: Dict[str, str]
) -> Dict[str, Set[str]]:
    class_paths: DefaultDict[str, Set[str]] = defaultdict(set)

    def dfs(class_name: str, tokens: List[str], stack: Set[str]) -> None:
        class_paths[class_name].add(" ".join(tokens))
        for child in sorted(edges.get(class_name, set())):
            child_token = class_tokens.get(child)
            if not child_token:
                continue
            if child in stack:
                continue
            dfs(child, tokens + [child_token], stack | {child})

    for root in sorted(roots):
        token = class_tokens.get(root)
        if not token:
            continue
        dfs(root, [token], {root})

    if not class_paths:
        print("[command-coverage] no command paths were derived from main.cpp registrations", file=sys.stderr)
        sys.exit(1)
    return dict(class_paths)


def collect_class_api_calls(commands_dir: pathlib.Path) -> Dict[str, Set[str]]:
    class_methods: DefaultDict[str, Set[str]] = defaultdict(set)
    execute_pattern = re.compile(r"int\s+([A-Za-z_]\w*)::execute\s*\([^)]*\)\s*\{", re.MULTILINE)

    for cpp_file in sorted(commands_dir.glob("*.cpp")):
        src = strip_single_line_comments(read_text(cpp_file))
        for match in execute_pattern.finditer(src):
            class_name = match.group(1)
            try:
                body, _ = extract_braced_block(src, match.end() - 1)
            except ValueError:
                print(f"[command-coverage] failed to parse execute body for {class_name}", file=sys.stderr)
                sys.exit(1)
            for api_method in re.findall(r"apiClient->([A-Za-z_]\w*)\s*\(", body):
                class_methods[class_name].add(api_method)

    if not class_methods:
        print("[command-coverage] no command execute->CloudAPI method calls were detected", file=sys.stderr)
        sys.exit(1)
    return dict(class_methods)


def is_docs_route(route: Route) -> bool:
    if route.is_regex:
        return route.path.startswith("^/services/health/monitoring")
    return route.path in {"/", "/index.html", "/docs", "/login", "/logout"}


def endpoint_samples(path: str) -> Sequence[str]:
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
            print(f"[command-coverage] invalid server regex route '{route.path}': {exc}", file=sys.stderr)
            sys.exit(1)
    return endpoint == route.path or endpoint.startswith(route.path + "?")


def match_server_route_to_cloud_methods(
    server_route: Route, method_routes: Dict[str, List[Route]]
) -> List[str]:
    matches: Set[str] = set()
    for method_name, cloud_routes in method_routes.items():
        for cloud_route in cloud_routes:
            if cloud_route.method != server_route.method:
                continue
            if any(route_matches_endpoint(server_route, sample) for sample in endpoint_samples(cloud_route.path)):
                matches.add(method_name)
                break
    return sorted(matches)


def main() -> int:
    server_src = read_text(SERVER_FILE)
    cloud_src = read_text(CLOUD_API_FILE)
    main_src = read_text(MAIN_FILE)

    server_routes = [route for route in collect_server_routes(server_src) if not is_docs_route(route)]
    method_routes = collect_cloud_method_routes(cloud_src)
    class_tokens = collect_command_tokens(COMMANDS_DIR)
    roots, edges = collect_command_graph(main_src)
    class_paths = build_class_cli_paths(roots, edges, class_tokens)
    class_api_calls = collect_class_api_calls(COMMANDS_DIR)

    method_to_cli_paths: DefaultDict[str, Set[str]] = defaultdict(set)
    for class_name, api_methods in class_api_calls.items():
        for api_method in api_methods:
            for path in class_paths.get(class_name, set()):
                method_to_cli_paths[api_method].add(path)

    missing_cloud: List[Route] = []
    missing_cli: List[Tuple[Route, List[str]]] = []
    covered_edges = 0

    for server_route in server_routes:
        mapped_methods = match_server_route_to_cloud_methods(server_route, method_routes)
        if not mapped_methods:
            missing_cloud.append(server_route)
            continue

        mapped_with_cli = [m for m in mapped_methods if method_to_cli_paths.get(m)]
        if not mapped_with_cli:
            missing_cli.append((server_route, mapped_methods))
            continue
        covered_edges += 1

    if missing_cloud or missing_cli:
        print("[command-coverage] server->cloud->command coverage gaps detected:")
        if missing_cloud:
            print("  server routes missing CloudAPIClient mapping:")
            for route in missing_cloud:
                route_kind = "regex" if route.is_regex else "static"
                print(f"    - {route.method} {route.path} ({route_kind})")
        if missing_cli:
            print("  server routes missing CLI command coverage:")
            for route, methods in missing_cli:
                route_kind = "regex" if route.is_regex else "static"
                joined = ", ".join(methods)
                print(f"    - {route.method} {route.path} ({route_kind}) via CloudAPI methods: {joined}")
        return 1

    distinct_cli_paths = sorted({path for paths in method_to_cli_paths.values() for path in paths})
    print(
        f"[command-coverage] OK: validated {len(server_routes)} server API routes; "
        f"{covered_edges} routes have server->cloud->command coverage across {len(distinct_cli_paths)} command paths."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
