#!/usr/bin/env python3
"""Keep Tracey status polling scoped to the requested agent identity."""

from __future__ import annotations

import pathlib
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNTIME_SOURCE = REPO_ROOT / "nmc_server" / "src" / "Core" / "APIRoutes_TraceyRuntime.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise ValueError(f"missing function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise ValueError(f"missing function body: {signature}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise ValueError(f"unterminated function body: {signature}")


def main() -> int:
    try:
        source = RUNTIME_SOURCE.read_text(encoding="utf-8")
        poll = function_body(source, "void APIRoutes::pollTraceyStatus(")
    except (OSError, ValueError) as exc:
        print(f"[tracey-status-identity-contract] {exc}", file=sys.stderr)
        return 1

    failures: list[str] = []
    if '{"X-Tracey-Proxy-Hop", "1"}' not in poll:
        failures.append("status polling must request the local snapshot past Tracey's follower proxy")
    if poll.count("client.Get(statusPath.c_str(), headers)") != 2:
        failures.append("both HTTP and HTTPS polling paths must send the status request headers")
    if "responseAgentId.empty()" not in poll:
        failures.append("status polling must reject a response without an agent identity")
    if "responseAgentId != agentId" not in poll:
        failures.append("status polling must reject a response from a different agent")
    if 'agent.metrics.erase("status_snapshot")' not in poll:
        failures.append("an untrusted response must not leave a misattributed snapshot available")
    if 'markFailure("Status endpoint identity does not match the requested agent.", &endpoint, true)' not in poll:
        failures.append("a mismatched response must discard the previously stored status snapshot")
    if '"agent_id", "agentId", "node_id", "nodeId", "id"' not in poll:
        failures.append("status polling must accept the supported agent identity field aliases")

    identity_guard = poll.find("responseAgentId != agentId")
    snapshot_write = poll.find('agent.metrics["status_snapshot"] = statusPayload')
    if snapshot_write < 0:
        failures.append("status polling no longer stores the accepted status snapshot")
    elif identity_guard < 0 or identity_guard > snapshot_write:
        failures.append("status identity validation must happen before storing telemetry")
    empty_identity_guard = poll.find("responseAgentId.empty()")
    if snapshot_write >= 0 and (empty_identity_guard < 0 or empty_identity_guard > snapshot_write):
        failures.append("missing identity validation must happen before storing telemetry")

    if failures:
        print("[tracey-status-identity-contract] violations detected:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("[tracey-status-identity-contract] OK: local snapshots are requested and mismatched identities fail closed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
