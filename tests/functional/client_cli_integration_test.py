#!/usr/bin/env python3
"""
Functional CLI integration test with a local mock HTTP server.

Coverage goals:
- request precision (method/path/query/body serialization)
- command correctness (success/failure exit codes)
- resilience (input validation before network calls)
- robustness (malformed upstream response handling)
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import threading
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import List


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_BIN = REPO_ROOT / "nmc_client" / "build" / "nmc"
NMC_BIN = pathlib.Path(os.getenv("NMC_BIN", str(DEFAULT_BIN)))
AUTH_ENV_KEYS = (
    "NMC_OIDC_ACCESS_TOKEN",
    "NM_OIDC_ACCESS_TOKEN",
    "NMC_BEARER_TOKEN",
    "NM_BEARER_TOKEN",
    "NMC_AUTH_TOKEN",
    "NM_AUTH_TOKEN",
)


@dataclass(frozen=True)
class RequestRecord:
    method: str
    path: str
    body: str


class MockServer:
    def __init__(self) -> None:
        self._records: List[RequestRecord] = []
        self._lock = threading.Lock()
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

        outer = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, fmt: str, *args) -> None:  # noqa: D401
                # Suppress noisy test logs.
                return

            def do_GET(self) -> None:
                outer._handle(self)

            def do_POST(self) -> None:
                outer._handle(self)

            def do_DELETE(self) -> None:
                outer._handle(self)

            def do_PUT(self) -> None:
                outer._handle(self)

            def do_PATCH(self) -> None:
                outer._handle(self)

        self._handler_cls = Handler

    @property
    def base_url(self) -> str:
        if not self._httpd:
            raise RuntimeError("mock server is not started")
        host, port = self._httpd.server_address
        return f"http://{host}:{port}"

    def start(self) -> None:
        self._httpd = ThreadingHTTPServer(("127.0.0.1", 0), self._handler_cls)
        self._thread = threading.Thread(target=self._httpd.serve_forever, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        if self._httpd:
            self._httpd.shutdown()
            self._httpd.server_close()
            self._httpd = None
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None

    def clear_records(self) -> None:
        with self._lock:
            self._records.clear()

    def records(self) -> List[RequestRecord]:
        with self._lock:
            return list(self._records)

    def _record(self, method: str, path: str, body: str) -> None:
        with self._lock:
            self._records.append(RequestRecord(method=method, path=path, body=body))

    @staticmethod
    def _send_json(handler: BaseHTTPRequestHandler, status: int, payload: object) -> None:
        data = json.dumps(payload).encode("utf-8")
        handler.send_response(status)
        handler.send_header("Content-Type", "application/json")
        handler.send_header("Content-Length", str(len(data)))
        handler.end_headers()
        handler.wfile.write(data)

    @staticmethod
    def _send_text(handler: BaseHTTPRequestHandler, status: int, payload: str) -> None:
        data = payload.encode("utf-8")
        handler.send_response(status)
        handler.send_header("Content-Type", "text/plain")
        handler.send_header("Content-Length", str(len(data)))
        handler.end_headers()
        handler.wfile.write(data)

    def _handle(self, handler: BaseHTTPRequestHandler) -> None:
        length_header = handler.headers.get("Content-Length", "0")
        try:
            length = int(length_header)
        except ValueError:
            length = 0
        raw_body = handler.rfile.read(length) if length > 0 else b""
        body = raw_body.decode("utf-8")
        self._record(handler.command, handler.path, body)

        path_only = handler.path.split("?", 1)[0]
        analysis_match = re.match(r"^/tracey/agents/([^/]+)/analysis$", path_only)
        control_match = re.match(r"^/tracey/agents/([^/]+)/control$", path_only)
        deepdive_match = re.match(r"^/tracey/agents/([^/]+)/deepdive$", path_only)

        if handler.command == "GET" and path_only == "/tracey/analytics":
            self._send_json(handler, 200, {"route": "tracey_analytics", "path": handler.path})
            return

        if handler.command == "GET" and path_only == "/tracey/adaptive":
            self._send_json(handler, 200, {"route": "tracey_adaptive", "path": handler.path})
            return

        if handler.command == "GET" and analysis_match:
            self._send_json(
                handler,
                200,
                {
                    "route": "tracey_analysis",
                    "agent_id": analysis_match.group(1),
                    "path": handler.path,
                },
            )
            return

        if handler.command == "POST" and control_match:
            try:
                payload = json.loads(body) if body else {}
            except json.JSONDecodeError:
                self._send_json(handler, 400, {"message": "invalid json payload"})
                return
            self._send_json(
                handler,
                200,
                {
                    "route": "tracey_control",
                    "agent_id": control_match.group(1),
                    "payload": payload,
                },
            )
            return

        if handler.command == "POST" and path_only == "/tracey/agents/heartbeat":
            try:
                payload = json.loads(body) if body else {}
            except json.JSONDecodeError:
                self._send_json(handler, 400, {"message": "invalid json payload"})
                return
            self._send_json(handler, 200, {"route": "tracey_heartbeat", "payload": payload})
            return

        if handler.command == "GET" and deepdive_match:
            agent_id = deepdive_match.group(1)
            if agent_id == "broken":
                self._send_text(handler, 200, "not-json-response")
                return
            self._send_json(handler, 200, {"route": "tracey_deepdive", "agent_id": agent_id})
            return

        self._send_json(handler, 404, {"message": "not found"})


def write_connection_config(home_dir: pathlib.Path, endpoint: str) -> None:
    config_dir = home_dir / ".nmc"
    config_dir.mkdir(parents=True, exist_ok=True)
    config_path = config_dir / "config.json"
    config_path.write_text(
        json.dumps(
            {
                "connections": [
                    {
                        "name": "integration",
                        "endpoint": endpoint,
                        "isActive": True,
                        "token": "",
                    }
                ],
                "default_connection": "integration",
            },
            indent=2,
        ),
        encoding="utf-8",
    )


def run_nmc(args: List[str], home_dir: pathlib.Path) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["HOME"] = str(home_dir)
    for key in AUTH_ENV_KEYS:
        env.pop(key, None)
    return subprocess.run(
        [str(NMC_BIN), *args],
        cwd=REPO_ROOT,
        env=env,
        text=True,
        capture_output=True,
        timeout=20,
        check=False,
    )


def assert_true(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def assert_success(result: subprocess.CompletedProcess[str], label: str) -> None:
    assert_true(
        result.returncode == 0,
        f"{label} failed (exit={result.returncode})\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
    )


def assert_failure(result: subprocess.CompletedProcess[str], label: str) -> None:
    assert_true(
        result.returncode != 0,
        f"{label} unexpectedly succeeded\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}",
    )


def test_analytics_query_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "tracey",
            "analytics",
            "--window-seconds",
            "7200",
            "--bucket-seconds",
            "120",
            "--log-limit",
            "5",
        ],
        home_dir,
    )
    assert_success(result, "tracey analytics")

    records = server.records()
    assert_true(len(records) == 1, f"tracey analytics expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"tracey analytics expected GET, got {req.method}")
    assert_true(
        req.path == "/tracey/analytics?window_seconds=7200&bucket_seconds=120&log_limit=5",
        f"tracey analytics wrong path: {req.path}",
    )


def test_agent_analysis_query_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "tracey",
            "analysis",
            "agent-1",
            "--window-seconds",
            "60",
            "--bucket-seconds",
            "30",
            "--log-limit",
            "9",
        ],
        home_dir,
    )
    assert_success(result, "tracey analysis")

    records = server.records()
    assert_true(len(records) == 1, f"tracey analysis expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"tracey analysis expected GET, got {req.method}")
    assert_true(
        req.path == "/tracey/agents/agent-1/analysis?window_seconds=60&bucket_seconds=30&log_limit=9",
        f"tracey analysis wrong path: {req.path}",
    )


def test_adaptive_query_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["tracey", "adaptive"], home_dir)
    assert_success(result, "tracey adaptive")

    records = server.records()
    assert_true(len(records) == 1, f"tracey adaptive expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"tracey adaptive expected GET, got {req.method}")
    assert_true(req.path == "/tracey/adaptive", f"tracey adaptive wrong path: {req.path}")


def test_adaptive_policy_query_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["tracey", "adaptive", "--policy", "risk"], home_dir)
    assert_success(result, "tracey adaptive --policy risk")

    records = server.records()
    assert_true(len(records) == 1, f"tracey adaptive --policy risk expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"tracey adaptive --policy risk expected GET, got {req.method}")
    assert_true(
        req.path == "/tracey/adaptive?policy=risk",
        f"tracey adaptive --policy risk wrong path: {req.path}",
    )


def test_control_payload_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "tracey",
            "control",
            "agent-2",
            "--action",
            "clear_quarantine",
            "--reason",
            "maintenance",
        ],
        home_dir,
    )
    assert_success(result, "tracey control")

    records = server.records()
    assert_true(len(records) == 1, f"tracey control expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"tracey control expected POST, got {req.method}")
    assert_true(req.path == "/tracey/agents/agent-2/control", f"tracey control wrong path: {req.path}")
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("action") == "clear_quarantine", "tracey control missing action field")
    assert_true(payload.get("reason") == "maintenance", "tracey control missing reason field")


def test_heartbeat_payload_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "tracey",
            "heartbeat",
            "--agent-id",
            "edge-1",
            "--status",
            "healthy",
            "--cluster",
            "cluster-a",
            "--metrics",
            '{"gpu_util":0.92}',
            "--score",
            "7",
            "--is-coordinator",
        ],
        home_dir,
    )
    assert_success(result, "tracey heartbeat")

    records = server.records()
    assert_true(len(records) == 1, f"tracey heartbeat expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"tracey heartbeat expected POST, got {req.method}")
    assert_true(req.path == "/tracey/agents/heartbeat", f"tracey heartbeat wrong path: {req.path}")
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("agent_id") == "edge-1", "tracey heartbeat missing agent_id")
    assert_true(payload.get("status") == "healthy", "tracey heartbeat missing status")
    assert_true(payload.get("cluster") == "cluster-a", "tracey heartbeat missing cluster")
    assert_true(payload.get("score") == 7, "tracey heartbeat missing score")
    assert_true(payload.get("is_coordinator") is True, "tracey heartbeat missing coordinator flag")
    assert_true(isinstance(payload.get("metrics"), dict), "tracey heartbeat metrics should be object")


def test_invalid_json_fails_before_network(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        ["tracey", "control", "agent-3", "--payload", "{bad-json"],
        home_dir,
    )
    assert_failure(result, "tracey control invalid-json")
    assert_true(
        "Invalid JSON in --payload" in result.stderr,
        f"expected invalid payload parse error, got stderr:\n{result.stderr}",
    )
    assert_true(len(server.records()) == 0, "invalid payload should not perform network calls")


def test_invalid_flag_fails_before_network(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        ["tracey", "analytics", "--window-seconds", "not-a-number"],
        home_dir,
    )
    assert_failure(result, "tracey analytics invalid-window")
    assert_true(
        ("--window-seconds must be an integer." in result.stderr)
        or ("--window-seconds must be greater than zero." in result.stderr),
        f"expected window-seconds validation error, got stderr:\n{result.stderr}",
    )
    assert_true(len(server.records()) == 0, "invalid window flag should not perform network calls")


def test_invalid_adaptive_policy_fails_before_network(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["tracey", "adaptive", "--policy", "turbo"], home_dir)
    assert_failure(result, "tracey adaptive invalid-policy")
    assert_true(
        "--policy must be one of: balanced, throughput, risk, energy." in result.stderr,
        f"expected adaptive policy validation error, got stderr:\n{result.stderr}",
    )
    assert_true(len(server.records()) == 0, "invalid adaptive policy should not perform network calls")


def test_malformed_upstream_response_fails_predictably(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["tracey", "deepdive", "broken"], home_dir)
    assert_failure(result, "tracey deepdive malformed-response")
    assert_true(
        "Failed to parse server response" in result.stderr,
        f"expected upstream parse error, got stderr:\n{result.stderr}",
    )
    records = server.records()
    assert_true(len(records) == 1, f"tracey deepdive expected 1 request, got {len(records)}")
    assert_true(records[0].method == "GET", f"tracey deepdive expected GET, got {records[0].method}")
    assert_true(
        records[0].path == "/tracey/agents/broken/deepdive",
        f"tracey deepdive wrong path: {records[0].path}",
    )


def main() -> int:
    if not NMC_BIN.exists():
        print(
            f"[functional-test] nmc binary not found at {NMC_BIN}. "
            "Build with: cmake -S nmc_client -B nmc_client/build && cmake --build nmc_client/build",
            file=sys.stderr,
        )
        return 1

    server = MockServer()
    server.start()
    try:
        with tempfile.TemporaryDirectory(prefix="nmc-functional-home-") as tmp_home:
            home_dir = pathlib.Path(tmp_home)
            write_connection_config(home_dir, server.base_url)

            test_analytics_query_serialization(server, home_dir)
            test_agent_analysis_query_serialization(server, home_dir)
            test_adaptive_query_serialization(server, home_dir)
            test_adaptive_policy_query_serialization(server, home_dir)
            test_control_payload_serialization(server, home_dir)
            test_heartbeat_payload_serialization(server, home_dir)
            test_invalid_json_fails_before_network(server, home_dir)
            test_invalid_flag_fails_before_network(server, home_dir)
            test_invalid_adaptive_policy_fails_before_network(server, home_dir)
            test_malformed_upstream_response_fails_predictably(server, home_dir)
    except AssertionError as exc:
        print(f"[functional-test] FAILED: {exc}", file=sys.stderr)
        return 1
    finally:
        server.stop()

    print("[functional-test] OK: validated CLI request precision and failure behavior against mock server.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
