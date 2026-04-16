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
GAIL_ENV_KEYS = (
    "NMC_GAIL_BASE_URL",
    "GAIL_BASE_URL",
    "NMC_GAIL_URL",
    "GAIL_URL",
    "NMC_GAIL_API_TOKEN",
    "GAIL_API_TOKEN",
    "NMC_GAIL_BEARER_TOKEN",
    "GAIL_BEARER_TOKEN",
    "NMC_GAIL_AUTH_TOKEN",
    "GAIL_AUTH_TOKEN",
)


@dataclass(frozen=True)
class RequestRecord:
    method: str
    path: str
    body: str
    authorization: str
    content_type: str


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

    def _record(
        self,
        method: str,
        path: str,
        body: str,
        authorization: str,
        content_type: str,
    ) -> None:
        with self._lock:
            self._records.append(
                RequestRecord(
                    method=method,
                    path=path,
                    body=body,
                    authorization=authorization,
                    content_type=content_type,
                )
            )

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
        self._record(
            handler.command,
            handler.path,
            body,
            handler.headers.get("Authorization", ""),
            handler.headers.get("Content-Type", ""),
        )

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

        if handler.command == "GET" and path_only == "/healthz":
            self._send_json(
                handler,
                200,
                {"ok": True, "service": "gail", "version": "0.1.0", "path": handler.path},
            )
            return

        if handler.command == "GET" and path_only == "/v1/status/orchestration":
            self._send_json(
                handler,
                200,
                {"route": "gail_status", "path": handler.path},
            )
            return

        if handler.command == "POST" and path_only == "/v1/llm/complete":
            try:
                payload = json.loads(body) if body else {}
            except json.JSONDecodeError:
                self._send_json(handler, 400, {"message": "invalid json payload"})
                return
            self._send_json(
                handler,
                200,
                {"route": "gail_complete", "payload": payload},
            )
            return

        if handler.command == "POST" and path_only == "/v1/llm/transcribe":
            self._send_json(
                handler,
                200,
                {"route": "gail_transcribe", "path": handler.path},
            )
            return

        if handler.command == "PATCH" and path_only == "/v1/testing/raw":
            self._send_json(
                handler,
                200,
                {"route": "gail_raw", "path": handler.path, "body_length": len(body)},
            )
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

        if handler.command == "GET" and path_only == "/proxmox/resources":
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "Proxmox resources retrieved.",
                    "data": {"total_gpus": 24, "available_gpus": 10, "cluster_nodes": 4},
                },
            )
            return

        if handler.command == "GET" and path_only == "/proxmox/clusters":
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "Proxmox clusters listed.",
                    "data": [
                        {
                            "name": "edge-pve",
                            "status": "running",
                            "endpoint": "https://pve.example.internal:8006",
                            "provider": "proxmox",
                        }
                    ],
                },
            )
            return

        proxmox_details_match = re.match(r"^/proxmox/details/([^/]+)$", path_only)
        if handler.command == "GET" and proxmox_details_match:
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "Proxmox cluster details retrieved.",
                    "data": {
                        "name": proxmox_details_match.group(1),
                        "status": "running",
                        "endpoint": "https://pve.example.internal:8006",
                        "provider": "proxmox",
                    },
                },
            )
            return

        if handler.command == "POST" and path_only == "/proxmox/clusters/request":
            try:
                payload = json.loads(body) if body else {}
            except json.JSONDecodeError:
                self._send_json(handler, 400, {"message": "invalid json payload"})
                return
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "Proxmox cluster request submitted.",
                    "data": {
                        "cluster": {
                            "name": payload.get("name", "unknown"),
                            "status": "creating",
                            "provider": payload.get("provider", "proxmox"),
                        }
                    },
                },
            )
            return

        if handler.command == "GET" and path_only == "/aarnn/endpoints":
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "AARNN endpoints discovered.",
                    "data": {
                        "runtime": {"found": True, "api_base_url": "http://10.0.0.10:8080"},
                        "control": {"found": True, "api_base_url": "http://10.0.0.11:8080"},
                        "orchestrator": {"found": True, "grpc_url": "http://10.0.0.12:50051"},
                    },
                },
            )
            return

        if handler.command == "GET" and path_only == "/aarnn/inventory":
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "AARNN inventory generated.",
                    "data": {
                        "summary": {
                            "cluster_count": 1,
                            "orchestrator_count": 1,
                            "node_count": 2,
                            "network_count": 1,
                        },
                        "clusters": [{"cluster_id": "aarnn-cluster-edge-1-50051"}],
                        "orchestrators": [{"orchestrator_id": "aarnn-orchestrator-edge-1-50051"}],
                        "nodes": [{"node_id": "node-1"}, {"node_id": "node-2"}],
                        "networks": [{"network_id": "tenant-aarnn"}],
                    },
                },
            )
            return

        aarnn_proxy_match = re.match(r"^/aarnn/proxy/([^/]+)$", path_only)
        if handler.command == "POST" and aarnn_proxy_match:
            try:
                payload = json.loads(body) if body else {}
            except json.JSONDecodeError:
                self._send_json(handler, 400, {"message": "invalid json payload"})
                return
            self._send_json(
                handler,
                200,
                {
                    "success": True,
                    "message": "AARNN proxy request completed.",
                    "data": {
                        "plane": aarnn_proxy_match.group(1),
                        "payload": payload,
                    },
                },
            )
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


def write_gail_config(home_dir: pathlib.Path, base_url: str, api_token: str) -> None:
    config_path = home_dir / ".nmc" / "config.json"
    payload = json.loads(config_path.read_text(encoding="utf-8"))
    payload["gail"] = {
        "base_url": base_url,
        "api_token": api_token,
    }
    config_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def run_nmc(
    args: List[str],
    home_dir: pathlib.Path,
    *,
    extra_env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["HOME"] = str(home_dir)
    for key in AUTH_ENV_KEYS:
        env.pop(key, None)
    for key in GAIL_ENV_KEYS:
        env.pop(key, None)
    if extra_env:
        env.update(extra_env)
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


def test_proxmox_request_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "proxmox",
            "request",
            "edge-pve",
            "--org",
            "neuralmimicry",
            "--gpu-count",
            "4",
            "--arch",
            "amd64",
            "--region",
            "edge-1",
            "--provider",
            "proxmox",
        ],
        home_dir,
    )
    assert_success(result, "proxmox request")

    records = server.records()
    assert_true(len(records) == 1, f"proxmox request expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"proxmox request expected POST, got {req.method}")
    assert_true(req.path == "/proxmox/clusters/request", f"proxmox request wrong path: {req.path}")
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("name") == "edge-pve", "proxmox request missing name")
    assert_true(payload.get("organization") == "neuralmimicry", "proxmox request missing organization")
    assert_true(payload.get("gpu_count") == 4, "proxmox request missing gpu_count")
    assert_true(payload.get("architecture") == "amd64", "proxmox request missing architecture")
    assert_true(payload.get("region") == "edge-1", "proxmox request missing region")
    assert_true(payload.get("provider") == "proxmox", "proxmox request missing provider")


def test_proxmox_status_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["proxmox", "status", "edge-pve"], home_dir)
    assert_success(result, "proxmox status")

    records = server.records()
    assert_true(len(records) == 1, f"proxmox status expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"proxmox status expected GET, got {req.method}")
    assert_true(req.path == "/proxmox/details/edge-pve", f"proxmox status wrong path: {req.path}")


def test_proxmox_invalid_provider_fails_before_network(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "proxmox",
            "request",
            "edge-pve",
            "--org",
            "neuralmimicry",
            "--gpu-count",
            "4",
            "--arch",
            "amd64",
            "--region",
            "edge-1",
            "--provider",
            "vmware",
        ],
        home_dir,
    )
    assert_failure(result, "proxmox request invalid-provider")
    assert_true(
        "provider must be one of: proxmox, proxmox-ve, hybrid-burst." in result.stderr,
        f"expected proxmox provider validation error, got stderr:\n{result.stderr}",
    )
    assert_true(len(server.records()) == 0, "invalid proxmox provider should not perform network calls")


def test_aarnn_endpoints_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["aarnn", "endpoints"], home_dir)
    assert_success(result, "aarnn endpoints")

    records = server.records()
    assert_true(len(records) == 1, f"aarnn endpoints expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"aarnn endpoints expected GET, got {req.method}")
    assert_true(req.path == "/aarnn/endpoints", f"aarnn endpoints wrong path: {req.path}")


def test_aarnn_inventory_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["aarnn", "inventory", "--cluster", "aarnn-cluster-edge-1-50051"], home_dir)
    assert_success(result, "aarnn inventory")

    records = server.records()
    assert_true(len(records) == 1, f"aarnn inventory expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"aarnn inventory expected GET, got {req.method}")
    assert_true(
        req.path == "/aarnn/inventory?cluster_id=aarnn-cluster-edge-1-50051",
        f"aarnn inventory wrong path: {req.path}",
    )


def test_aarnn_network_status_inventory_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["aarnn", "network", "status"], home_dir)
    assert_success(result, "aarnn network status")

    records = server.records()
    assert_true(len(records) == 1, f"aarnn network status expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"aarnn network status expected GET, got {req.method}")
    assert_true(req.path == "/aarnn/inventory", f"aarnn network status wrong path: {req.path}")


def test_aarnn_network_control_proxy_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        ["aarnn", "network", "control", "tenant-aarnn", "--action", "start"],
        home_dir,
    )
    assert_success(result, "aarnn network control")

    records = server.records()
    assert_true(len(records) == 1, f"aarnn network control expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"aarnn network control expected POST, got {req.method}")
    assert_true(req.path == "/aarnn/proxy/control", f"aarnn network control wrong path: {req.path}")
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("method") == "POST", "aarnn network control missing proxy method")
    assert_true(payload.get("path") == "/api/control_network", "aarnn network control wrong upstream path")
    upstream_json = payload.get("json", {})
    assert_true(upstream_json.get("network_id") == "tenant-aarnn", "aarnn network control missing network_id")
    assert_true(upstream_json.get("action") == "start", "aarnn network control missing action")


def test_aarnn_network_targeted_proxy_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "aarnn",
            "network",
            "snapshot",
            "tenant-aarnn",
            "--orchestrator",
            "aarnn-orchestrator-edge-1-50051",
        ],
        home_dir,
    )
    assert_success(result, "aarnn network snapshot targeted")

    records = server.records()
    assert_true(len(records) == 1, f"aarnn targeted snapshot expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"aarnn targeted snapshot expected POST, got {req.method}")
    assert_true(req.path == "/aarnn/proxy/control", f"aarnn targeted snapshot wrong path: {req.path}")
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("method") == "GET", "aarnn targeted snapshot missing proxy method")
    assert_true(payload.get("path") == "/api/snapshot?network_id=tenant-aarnn", "aarnn targeted snapshot wrong upstream path")
    assert_true(
        payload.get("orchestrator_id") == "aarnn-orchestrator-edge-1-50051",
        "aarnn targeted snapshot missing orchestrator_id",
    )


def test_aarnn_runtime_create_control_plane_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "aarnn",
            "runtime",
            "create",
            "--plane",
            "control",
            "--workspace-id",
            "ws-1",
            "--name",
            "demo",
            "--config-json",
            '{"num_sensory_neurons":4,"num_hidden_layers":1,"num_output_neurons":2}',
            "--auto-start",
        ],
        home_dir,
    )
    assert_success(result, "aarnn runtime create")

    records = server.records()
    assert_true(len(records) == 1, f"aarnn runtime create expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"aarnn runtime create expected POST, got {req.method}")
    assert_true(req.path == "/aarnn/proxy/control", f"aarnn runtime create wrong path: {req.path}")
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("method") == "POST", "aarnn runtime create missing proxy method")
    assert_true(payload.get("path") == "/api/runtime/workspaces", "aarnn runtime create wrong upstream path")
    upstream_json = payload.get("json", {})
    assert_true(upstream_json.get("workspace_id") == "ws-1", "aarnn runtime create missing workspace_id")
    assert_true(upstream_json.get("name") == "demo", "aarnn runtime create missing name")
    assert_true(upstream_json.get("auto_start") is True, "aarnn runtime create missing auto_start")
    assert_true(isinstance(upstream_json.get("config_json"), str), "aarnn runtime create config_json should be string")


def test_aarnn_runtime_invalid_plane_fails_before_network(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(["aarnn", "runtime", "status", "--plane", "bogus"], home_dir)
    assert_failure(result, "aarnn runtime invalid-plane")
    assert_true(
        "--plane must be either 'runtime' or 'control'." in result.stderr,
        f"expected invalid plane error, got stderr:\n{result.stderr}",
    )
    assert_true(len(server.records()) == 0, "invalid plane should not perform network calls")


def test_gail_health_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        ["gail", "health"],
        home_dir,
        extra_env={"NMC_GAIL_BASE_URL": server.base_url},
    )
    assert_success(result, "gail health")

    records = server.records()
    assert_true(len(records) == 1, f"gail health expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"gail health expected GET, got {req.method}")
    assert_true(req.path == "/healthz", f"gail health wrong path: {req.path}")


def test_gail_status_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        ["gail", "status", "--limit", "7", "--probe-engines", "--probe-providers"],
        home_dir,
        extra_env={"NMC_GAIL_BASE_URL": server.base_url},
    )
    assert_success(result, "gail status")

    records = server.records()
    assert_true(len(records) == 1, f"gail status expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"gail status expected GET, got {req.method}")
    assert_true(
        req.path == "/v1/status/orchestration?limit=7&probe_engines=true&probe_providers=true",
        f"gail status wrong path: {req.path}",
    )


def test_gail_health_uses_config_fallback(server: MockServer, home_dir: pathlib.Path) -> None:
    write_gail_config(home_dir, server.base_url, "config-secret")

    server.clear_records()
    result = run_nmc(["gail", "health"], home_dir)
    assert_success(result, "gail health config fallback")

    records = server.records()
    assert_true(len(records) == 1, f"gail health config fallback expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "GET", f"gail health config fallback expected GET, got {req.method}")
    assert_true(req.path == "/healthz", f"gail health config fallback wrong path: {req.path}")
    assert_true(
        req.authorization == "Bearer config-secret",
        f"gail health config fallback missing authorization header: {req.authorization!r}",
    )


def test_gail_complete_serialization_and_auth_header(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "gail",
            "complete",
            "--json",
            '{"workflow":"assistant_requirements","role":"assistant","messages":[{"role":"user","content":"Hello Gail"}]}',
        ],
        home_dir,
        extra_env={
            "NMC_GAIL_BASE_URL": server.base_url,
            "NMC_GAIL_API_TOKEN": "gail-secret",
        },
    )
    assert_success(result, "gail complete")

    records = server.records()
    assert_true(len(records) == 1, f"gail complete expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"gail complete expected POST, got {req.method}")
    assert_true(req.path == "/v1/llm/complete", f"gail complete wrong path: {req.path}")
    assert_true(
        req.authorization == "Bearer gail-secret",
        f"gail complete missing authorization header: {req.authorization!r}",
    )
    payload = json.loads(req.body or "{}")
    assert_true(payload.get("workflow") == "assistant_requirements", "gail complete missing workflow")
    assert_true(payload.get("role") == "assistant", "gail complete missing role")
    assert_true(payload.get("messages", [{}])[0].get("content") == "Hello Gail", "gail complete wrong payload")


def test_gail_transcribe_multipart_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    audio_path = home_dir / "sample.wav"
    audio_path.write_bytes(b"RIFFTESTDATA")

    server.clear_records()
    result = run_nmc(
        [
            "gail",
            "transcribe",
            "--provider",
            "openai",
            "--file",
            str(audio_path),
            "--model",
            "gpt-4o-mini-transcribe",
        ],
        home_dir,
        extra_env={"NMC_GAIL_BASE_URL": server.base_url},
    )
    assert_success(result, "gail transcribe")

    records = server.records()
    assert_true(len(records) == 1, f"gail transcribe expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "POST", f"gail transcribe expected POST, got {req.method}")
    assert_true(req.path == "/v1/llm/transcribe", f"gail transcribe wrong path: {req.path}")
    assert_true(
        req.content_type.startswith("multipart/form-data;"),
        f"gail transcribe expected multipart content-type, got {req.content_type!r}",
    )
    assert_true('name="provider"' in req.body, "gail transcribe missing provider field")
    assert_true("openai" in req.body, "gail transcribe missing provider value")
    assert_true('name="model"' in req.body, "gail transcribe missing model field")
    assert_true("gpt-4o-mini-transcribe" in req.body, "gail transcribe missing model value")
    assert_true('filename="sample.wav"' in req.body, "gail transcribe missing filename")


def test_gail_request_raw_body_serialization(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        [
            "gail",
            "request",
            "/v1/testing/raw",
            "--method",
            "PATCH",
            "--body",
            "hello from nmc",
            "--content-type",
            "text/plain",
        ],
        home_dir,
        extra_env={"NMC_GAIL_BASE_URL": server.base_url},
    )
    assert_success(result, "gail request raw")

    records = server.records()
    assert_true(len(records) == 1, f"gail request expected 1 request, got {len(records)}")
    req = records[0]
    assert_true(req.method == "PATCH", f"gail request expected PATCH, got {req.method}")
    assert_true(req.path == "/v1/testing/raw", f"gail request wrong path: {req.path}")
    assert_true(req.body == "hello from nmc", f"gail request wrong body: {req.body!r}")
    assert_true(req.content_type == "text/plain", f"gail request wrong content-type: {req.content_type!r}")


def test_gail_invalid_json_fails_before_network(server: MockServer, home_dir: pathlib.Path) -> None:
    server.clear_records()
    result = run_nmc(
        ["gail", "complete", "--json", "{bad-json"],
        home_dir,
        extra_env={"NMC_GAIL_BASE_URL": server.base_url},
    )
    assert_failure(result, "gail invalid-json")
    assert_true(
        "Invalid JSON in --json" in result.stderr,
        f"expected Gail invalid JSON error, got stderr:\n{result.stderr}",
    )
    assert_true(len(server.records()) == 0, "invalid Gail payload should not perform network calls")


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
            test_proxmox_request_serialization(server, home_dir)
            test_proxmox_status_serialization(server, home_dir)
            test_proxmox_invalid_provider_fails_before_network(server, home_dir)
            test_aarnn_endpoints_serialization(server, home_dir)
            test_aarnn_inventory_serialization(server, home_dir)
            test_aarnn_network_status_inventory_serialization(server, home_dir)
            test_aarnn_network_control_proxy_serialization(server, home_dir)
            test_aarnn_network_targeted_proxy_serialization(server, home_dir)
            test_aarnn_runtime_create_control_plane_serialization(server, home_dir)
            test_aarnn_runtime_invalid_plane_fails_before_network(server, home_dir)
            test_gail_health_serialization(server, home_dir)
            test_gail_status_serialization(server, home_dir)
            test_gail_health_uses_config_fallback(server, home_dir)
            test_gail_complete_serialization_and_auth_header(server, home_dir)
            test_gail_transcribe_multipart_serialization(server, home_dir)
            test_gail_request_raw_body_serialization(server, home_dir)
            test_gail_invalid_json_fails_before_network(server, home_dir)
    except AssertionError as exc:
        print(f"[functional-test] FAILED: {exc}", file=sys.stderr)
        return 1
    finally:
        server.stop()

    print("[functional-test] OK: validated CLI request precision and failure behavior against mock server.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
