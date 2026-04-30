#!/usr/bin/env python3
"""
Tracey adaptive contract test:
- Ensure /tracey/adaptive remains registered on the server.
- Ensure handleTraceyAdaptive exposes the stable response envelope used by CLI/dashboard.
- Ensure fallback loop synthesis and operator policy selection remain wired in.
"""

from __future__ import annotations

import re
import sys

from server_route_sources import read_route_registration_source


def extract_braced_block(src: str, open_brace_index: int) -> tuple[str, int]:
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
    raise ValueError("unterminated braced block")


def extract_function_body(src: str, name: str) -> str:
    match = re.search(
        rf"void\s+APIRoutes::{re.escape(name)}\s*\(const httplib::Request& req, httplib::Response& res\)\s*\{{",
        src,
        re.MULTILINE,
    )
    if not match:
        raise ValueError(f"failed to locate APIRoutes::{name}")
    body, _ = extract_braced_block(src, match.end() - 1)
    return body


def extract_initializer_block(src: str, anchor_pattern: str) -> str:
    match = re.search(anchor_pattern, src, re.MULTILINE)
    if not match:
        raise ValueError(f"failed to locate initializer block: {anchor_pattern}")
    body, _ = extract_braced_block(src, match.end() - 1)
    return body


def main() -> int:
    route_src = read_route_registration_source()
    server_src = route_src
    failures: list[str] = []

    if 'svr.Get("/tracey/adaptive"' not in route_src:
        failures.append('missing `svr.Get("/tracey/adaptive", ...)` registration')

    try:
        handle_body = extract_function_body(server_src, "handleTraceyAdaptive")
    except ValueError as exc:
        print(f"[tracey-adaptive-contract] {exc}", file=sys.stderr)
        return 1

    if "buildAdaptiveLoopFallback(view)" not in handle_body:
        failures.append("handleTraceyAdaptive no longer synthesizes fallback loop state for older agents")
    if 'req.has_param("policy")' not in handle_body:
        failures.append("handleTraceyAdaptive no longer reads the policy query parameter")
    if "normalizeAdaptivePlacementPolicy" not in handle_body:
        failures.append("handleTraceyAdaptive no longer resolves adaptive placement policy")
    for policy in ("balanced", "throughput", "risk", "energy"):
        if f'"{policy}"' not in server_src:
            failures.append(f"adaptive policy literal missing: {policy}")

    try:
        response_body = extract_initializer_block(handle_body, r"apiResponse\.data\s*=\s*\{")
        summary_body = extract_initializer_block(response_body, r'\{"summary",\s*\{')
    except ValueError as exc:
        print(f"[tracey-adaptive-contract] {exc}", file=sys.stderr)
        return 1

    top_level_keys = [
        "generated_epoch_ms",
        "summary",
        "infrastructure",
        "plan",
        "ramp",
        "optimize",
        "repeat",
        "recommendations",
        "placement_candidates",
        "gpu_candidates",
        "recent_actions",
        "agents",
    ]
    for key in top_level_keys:
        if f'{{"{key}",' not in response_body:
            failures.append(f"adaptive response missing top-level key: {key}")

    summary_keys = [
        "mode",
        "policy",
        "policy_label",
        "overall_score",
        "readiness_score",
        "placement_score",
        "next_action",
    ]
    for key in summary_keys:
        if f'{{"{key}",' not in summary_body:
            failures.append(f"adaptive summary missing key: {key}")

    if 'placementPolicyLabel + " policy | "' not in handle_body:
        failures.append("adaptive optimize headline no longer surfaces the resolved policy label")

    if failures:
        print("[tracey-adaptive-contract] adaptive route contract violations detected:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("[tracey-adaptive-contract] OK: /tracey/adaptive exposes the expected route, fallback loop, policy controls, and response keys.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
