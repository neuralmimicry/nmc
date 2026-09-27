#!/usr/bin/env python3
"""Guard the NMC/Gail overview contract and its bounded telemetry payload."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
SERVER = (ROOT / "nmc_server/src/Core/APIRoutes_DomainProxy.cpp").read_text(encoding="utf-8")
CLIENT = (ROOT / "nmc_client/src/Core/CloudAPIClient.cpp").read_text(encoding="utf-8")
CLIENT_HEADER = (ROOT / "nmc_client/src/Core/CloudAPIClient.h").read_text(encoding="utf-8")
DASHBOARD = (ROOT / "nmc_server/src/docs/dashboard.js").read_text(encoding="utf-8")


def require(source: str, fragment: str, description: str) -> None:
    if fragment not in source:
        raise AssertionError(f"missing {description}: {fragment}")


def main() -> int:
    require(SERVER, 'svr.Get("/gail/trading/overview"', "server overview route")
    require(CLIENT, 'std::string path = "/gail/trading/overview"', "client overview route")
    require(CLIENT_HEADER, "getGailTradingOverview(int historyLimit = -1, int logLimit = -1)", "client overview declaration")

    # The dashboard contract needs an active issue count and normalized trade
    # fields, while issue details must remain bounded per polling response.
    require(SERVER, 'nlohmann::json summarizeApiIssues', "bounded issue summarizer")
    require(SERVER, '{"sample", nlohmann::json::array()}', "bounded issue sample")
    require(SERVER, '{"truncated", false}', "issue truncation marker")
    require(SERVER, 'summarizeApiIssues(*issuesObject)', "bounded issue details in overview")
    if '{"details", issuesObject}' in SERVER:
        raise AssertionError("overview must not serialize the complete API issue map")

    require(SERVER, 'normalized["timestamp"] = timestamp', "normalized trade timestamp")
    require(SERVER, 'normalized["side"] = action', "normalized trade side")
    require(SERVER, '{"source", tradeSource}', "unified trade source metadata")
    require(SERVER, '{"execution_authority", tradeExecutionAuthority}', "trade execution authority metadata")
    require(SERVER, '{"includes_pre_gail_octobot_history", true}', "pre-Gail history inclusion marker")
    require(DASHBOARD, 'entry.side || entry.action', "dashboard trade side column")
    require(DASHBOARD, 'entry.source === "octobot_imported"', "dashboard trade provenance column")
    require(DASHBOARD, "Unified OctoBot and Gail Trade History", "unified trade heading")
    require(DASHBOARD, 'gailTradingEntryTimestamp(entry)', "dashboard trade time column")
    require(DASHBOARD, "if (dashboardRefreshInFlight)", "dashboard refresh overlap guard")
    require(DASHBOARD, 'fetch(withBase("/auth/session")', "session validation before login redirect")

    print("[gail-contract-test] OK: server/client routes and bounded dashboard schema are aligned.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
