#!/usr/bin/env python3
"""Keep node resource observations read-only, bounded and connected end to end."""

from __future__ import annotations

import pathlib
import re
import sys

from server_route_sources import read_route_registration_source


ROOT = pathlib.Path(__file__).resolve().parents[2]


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[node-resources-contract] failed to read {path}: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc


def function_body(source: str, signature: str) -> str | None:
    match = re.search(signature, source)
    if not match:
        return None
    opening = match.end() - 1
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    return None


def main() -> int:
    route_source = read_route_registration_source()
    handler = read_text(ROOT / "nmc_server/src/Core/K8sHandlers.cpp")
    auth = read_text(ROOT / "nmc_server/src/Core/APIRoutes_ControlSurface.cpp")
    client = read_text(ROOT / "nmc_client/src/Core/CloudAPIClient.cpp")
    command = read_text(ROOT / "nmc_client/src/Commands/K8sCommands.cpp")
    registration = read_text(ROOT / "nmc_client/src/main.cpp")

    body = function_body(
        handler,
        r"void\s+K8sHandlers::handleGetNodeResources\(const httplib::Request& req,\s*"
        r"httplib::Response& res\)\s*\{",
    )
    failures: list[str] = []
    if 'svr.Get("/k8s/node-resources"' not in route_source or "handleGetNodeResources(req, res)" not in route_source:
        failures.append("the read-only node-resources server route is not wired to its handler")
    if 'path == "/k8s/node-resources" && method == "GET"' not in auth or "SERVICE_ACCESS_OBSERVE" not in auth:
        failures.append("GET /k8s/node-resources is not explicitly observe-authorised")
    if body is None:
        failures.append("the node resource handler is missing")
    else:
        for token, description in (
            ("apiClientMutex", "the shared Kubernetes client is not serialised"),
            ("CoreV1API_readNode", "the handler does not scope the read to one named node"),
            ("Generic_readResource", "the handler does not read the Metrics API node sample"),
            ('"metrics.k8s.io"', "the metrics.k8s.io API group is not selected"),
            ('{"capacity",', "node capacity is not returned"),
            ('{"allocatable",', "node allocatable resources are not returned"),
            ('{"available", false}', "missing Metrics API data is not explicitly unavailable"),
            ('{"freshness_threshold_seconds", 120}', "metric freshness is not bounded and reported"),
            ('{"scheduler_request_headroom_proven", false}', "the response does not disclose that Pod requests are not summed"),
        ):
            if token not in body:
                failures.append(description)
    if '"/k8s/node-resources?node="' not in client or "encodeQueryValue(nodeName)" not in client:
        failures.append("the CloudAPIClient does not safely address the node-resources route")
    if 'BaseCommand("node-resources"' not in command or "getK8sNodeResources(parsedArgs.front())" not in command:
        failures.append("the nmc k8s node-resources command is not implemented")
    if "K8sNodeResourcesCommand" not in registration:
        failures.append("the CLI does not register the node-resources command")

    if failures:
        print("[node-resources-contract] violations:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("[node-resources-contract] OK: read-only node capacity and explicit Metrics API freshness are exposed through Continuum.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
