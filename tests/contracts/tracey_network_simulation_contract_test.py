#!/usr/bin/env python3
"""
Tracey network simulation contract test:
- Ensure Tracey agent views enrich resource_forecast with continuum expansion data.
- Ensure the dashboard exposes Actual/Expansion tabs and simulation controls for fleet/rack/server scopes.
- Ensure dashboard JS still wires the simulation model, controls, and renderers together.
"""

from __future__ import annotations

import pathlib
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVER_FILE = REPO_ROOT / "nmc_server" / "src" / "Core" / "APIRoutes.cpp"
DASHBOARD_JS_FILE = REPO_ROOT / "nmc_server" / "src" / "docs" / "dashboard.js"
INDEX_HTML_FILE = REPO_ROOT / "nmc_server" / "src" / "docs" / "index.html"


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[tracey-network-simulation-contract] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def require_substrings(src: str, values: list[str], failures: list[str], context: str) -> None:
    for value in values:
        if value not in src:
            failures.append(f"{context} missing `{value}`")


def main() -> int:
    server_src = read_text(SERVER_FILE)
    dashboard_js = read_text(DASHBOARD_JS_FILE)
    index_html = read_text(INDEX_HTML_FILE)

    failures: list[str] = []

    require_substrings(
        server_src,
        [
            "enrichTraceyResourceForecastView(server, networkSummary, gpus, resourceForecastView, nowMs);",
            'resourceForecast["continuum_expansion"] = {',
            '{"recommended_targets", {',
            '{"maximum_targets", {',
            '{"heuristics", heuristics}',
            '{"dimensions", nlohmann::json::array({',
            '{"recommended_simulation", {',
            'resourceForecast["ai_advice"] = aiAdvice;',
            'resourceForecast["simulation"] = simulationView;',
        ],
        failures,
        "APIRoutes.cpp",
    )

    require_substrings(
        index_html,
        [
            'id="traceyFleetTopologyTabActual"',
            'id="traceyFleetTopologyTabExpansion"',
            'id="traceyFleetSimulationControls"',
            'id="traceyFleetSimulationNodes"',
            'id="traceyFleetSimulationGpus"',
            'id="traceyFleetSimulationCores"',
            'id="traceyFleetSimulationStrategy"',
            'id="traceyFleetSimulationRecommended"',
            'id="traceyFleetSimulationFacts"',
            'id="traceyRackTopologyTabActual"',
            'id="traceyRackTopologyTabExpansion"',
            'id="traceyRackSimulationControls"',
            'id="traceyRackSimulationNodes"',
            'id="traceyRackSimulationGpus"',
            'id="traceyRackSimulationCores"',
            'id="traceyRackSimulationStrategy"',
            'id="traceyRackSimulationRecommended"',
            'id="traceyRackSimulationFacts"',
            'id="traceyServerTopologyTabActual"',
            'id="traceyServerTopologyTabExpansion"',
            'id="traceyServerSimulationControls"',
            'id="traceyServerSimulationNodes"',
            'id="traceyServerSimulationGpus"',
            'id="traceyServerSimulationCores"',
            'id="traceyServerSimulationStrategy"',
            'id="traceyServerSimulationRecommended"',
            'id="traceyServerSimulationFacts"',
            'Switch to Expansion to model a larger fleet fabric.',
            'Switch to Expansion to model additional servers and GPUs in this rack.',
            'Switch to Expansion to model larger node, GPU, or CPU-core targets.',
        ],
        failures,
        "index.html",
    )

    require_substrings(
        dashboard_js,
        [
            "const TRACEY_SIMULATION_SCOPE_CONFIG = {",
            'function bindTraceySimulationControls(scope) {',
            'function buildTraceyScopedSimulationTopology(',
            'function buildTraceyServerSimulationTopology(',
            'function traceyEstimateExpansionScenario(',
            'function buildTraceySimulationFactRows(',
            'function currentTraceySimulationModel(scope) {',
            'function renderTraceyFleetNetworkSection(data) {',
            'function renderTraceyRackNetworkSection(detail) {',
            'function renderTraceyServerNetworkSection(data) {',
            'bindTraceySimulationControls("fleet");',
            'bindTraceySimulationControls("rack");',
            'bindTraceySimulationControls("server");',
            'readTraceyTopologyTab(scope) === "expansion"',
            'buildTraceyTopologyLegendEntries(dataset, "simulation"',
            'buildTraceyScenarioTalkers(',
        ],
        failures,
        "dashboard.js",
    )

    if failures:
        print("[tracey-network-simulation-contract] Tracey network simulation contract violations detected:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("[tracey-network-simulation-contract] OK: Tracey network expansion payloads and UI controls are wired end to end.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
