#!/usr/bin/env python3
"""
Tracey CLI simulation contract test:
- Ensure the client transport exposes a reusable Tracey simulation query model.
- Ensure Tracey CLI commands expose simulation flags and validate strategy input.
- Ensure fleet/rack/server/gpu Tracey CLI commands pass simulation requests through to CloudAPIClient.
"""

from __future__ import annotations

import pathlib
import re
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER_FILE = REPO_ROOT / "nmc_client" / "src" / "Core" / "CloudAPIClient.h"
CLIENT_FILE = REPO_ROOT / "nmc_client" / "src" / "Core" / "CloudAPIClient.cpp"
COMMANDS_FILE = REPO_ROOT / "nmc_client" / "src" / "Commands" / "TraceyCommands.cpp"


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[tracey-cli-simulation-contract] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def require_substrings(src: str, values: list[str], failures: list[str], context: str) -> None:
    for value in values:
        if value not in src:
            failures.append(f"{context} missing `{value}`")


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


def extract_function_body(src: str, signature_pattern: str) -> str:
    match = re.search(signature_pattern, src, re.MULTILINE)
    if not match:
        raise ValueError(f"failed to locate function: {signature_pattern}")
    body, _ = extract_braced_block(src, match.end() - 1)
    return body


def main() -> int:
    header_src = read_text(HEADER_FILE)
    client_src = read_text(CLIENT_FILE)
    commands_src = read_text(COMMANDS_FILE)

    failures: list[str] = []

    require_substrings(
        header_src,
        [
            "struct TraceySimulationQuery {",
            "int simulationNodes{-1};",
            "int simulationGpus{-1};",
            "int simulationCores{-1};",
            "std::string simulationStrategy;",
            "[[nodiscard]] bool hasAny() const {",
            "Models::CloudResponse getTraceyFleet(const TraceySimulationQuery& simulation = TraceySimulationQuery{});",
            "Models::CloudResponse getTraceyRackDetails(const std::string& rackId,",
            "Models::CloudResponse getTraceyAgentServer(const std::string& agentId,",
            "Models::CloudResponse getTraceyAgentGpu(const std::string& agentId,",
        ],
        failures,
        "CloudAPIClient.h",
    )

    require_substrings(
        client_src,
        [
            "void appendTraceySimulationQuery(std::string& path, const NMC::Core::TraceySimulationQuery& simulation) {",
            'appendQueryInt(path, "simulation_nodes", simulation.simulationNodes);',
            'appendQueryInt(path, "simulation_gpus", simulation.simulationGpus);',
            'appendQueryInt(path, "simulation_cores", simulation.simulationCores);',
            'appendQueryString(path, "simulation_strategy", simulation.simulationStrategy);',
            'std::string path = "/tracey/fleet";',
            'std::string path = "/tracey/racks/" + rackId;',
            'std::string path = "/tracey/agents/" + agentId + "/server";',
            'std::string path = "/tracey/agents/" + agentId + "/gpus/" + gpuId + "/telemetry";',
            "appendTraceySimulationQuery(path, simulation);",
        ],
        failures,
        "CloudAPIClient.cpp",
    )

    require_substrings(
        commands_src,
        [
            'command.addFlag(CLI::Flag("N", "sim-nodes", "Scenario target node count", CLI::FlagType::Int, false));',
            'command.addFlag(CLI::Flag("G", "sim-gpus", "Scenario target GPU count", CLI::FlagType::Int, false));',
            'command.addFlag(CLI::Flag("C", "sim-cores", "Scenario target CPU core count", CLI::FlagType::Int, false));',
            'command.addFlag(CLI::Flag("S", "sim-strategy", "Scenario strategy: balanced, throughput, collective", CLI::FlagType::String, false));',
            'errorOut = "--sim-strategy must be one of: balanced, throughput, collective.";',
            'usage = "nmc tracey fleet [--sim-nodes N] [--sim-gpus N] [--sim-cores N] [--sim-strategy balanced|throughput|collective]";',
            'usage = "nmc tracey rack RACK_ID [--sim-nodes N] [--sim-gpus N] [--sim-cores N] [--sim-strategy balanced|throughput|collective]";',
            'usage = "nmc tracey server AGENT_ID [--sim-nodes N] [--sim-gpus N] [--sim-cores N] [--sim-strategy balanced|throughput|collective]";',
            'usage = "nmc tracey gpu AGENT_ID GPU_ID [--sim-nodes N] [--sim-gpus N] [--sim-cores N] [--sim-strategy balanced|throughput|collective]";',
        ],
        failures,
        "TraceyCommands.cpp",
    )

    execute_expectations = [
        (
            "TraceyFleetCommand::execute",
            r"int\s+TraceyFleetCommand::execute\s*\([^)]*\)\s*\{",
            [
                "parseTraceySimulationFlags(parsedFlags, simulation, parseError)",
                "apiClient->getTraceyFleet(simulation)",
            ],
        ),
        (
            "TraceyRackCommand::execute",
            r"int\s+TraceyRackCommand::execute\s*\([^)]*\)\s*\{",
            [
                "parseTraceySimulationFlags(parsedFlags, simulation, parseError)",
                "apiClient->getTraceyRackDetails(parsedArgs[0], simulation)",
            ],
        ),
        (
            "TraceyServerCommand::execute",
            r"int\s+TraceyServerCommand::execute\s*\([^)]*\)\s*\{",
            [
                "parseTraceySimulationFlags(parsedFlags, simulation, parseError)",
                "apiClient->getTraceyAgentServer(parsedArgs[0], simulation)",
            ],
        ),
        (
            "TraceyGpuCommand::execute",
            r"int\s+TraceyGpuCommand::execute\s*\([^)]*\)\s*\{",
            [
                "parseTraceySimulationFlags(parsedFlags, simulation, parseError)",
                "apiClient->getTraceyAgentGpu(parsedArgs[0], parsedArgs[1], simulation)",
            ],
        ),
    ]

    for label, pattern, expected in execute_expectations:
        try:
            body = extract_function_body(commands_src, pattern)
        except ValueError as exc:
            print(f"[tracey-cli-simulation-contract] {exc}", file=sys.stderr)
            return 1
        require_substrings(body, expected, failures, label)

    if failures:
        print("[tracey-cli-simulation-contract] Tracey CLI simulation contract violations detected:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("[tracey-cli-simulation-contract] OK: Tracey CLI simulation flags, transport query model, and scope command wiring are aligned.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
