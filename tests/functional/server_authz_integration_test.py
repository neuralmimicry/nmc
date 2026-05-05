#!/usr/bin/env python3
"""
Focused end-to-end server authorisation test with a real nmc_server process.

Coverage goals:
- central-session identity normalisation on /auth/session
- Continuum observe/control route enforcement
- Tracey observe/use/control route enforcement
- AARNN observe/use/control route enforcement
- static admin-token fallback for protected routes
"""

from __future__ import annotations

import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_SERVER_BIN = REPO_ROOT / "nmc_server" / "build" / "nmc_server"
NMC_SERVER_BIN = pathlib.Path(os.getenv("NMC_SERVER_BIN", str(DEFAULT_SERVER_BIN)))
STATIC_ADMIN_TOKEN = "static-admin-token"


def build_service_access_entry(
    service_key: str,
    access_level: str,
    public_access_level: str,
) -> dict[str, Any]:
    return {
        "service_key": service_key,
        "access_level": access_level,
        "public_access_level": public_access_level,
    }


def build_identity(
    user: str,
    *,
    service_access: dict[str, dict[str, Any]],
    role: str = "user",
    groups: list[str] | None = None,
    identity_type: str | None = None,
) -> dict[str, Any]:
    resolved_groups = list(groups) if isinstance(groups, list) else [role]
    return {
        "authenticated": True,
        "user": user,
        "role": role,
        "groups": resolved_groups,
        "identity_type": identity_type,
        "service_access": service_access,
    }


TOKEN_IDENTITIES: dict[str, dict[str, Any]] = {
    "continuum-observe-token": build_identity(
        "continuum-observer",
        service_access={
            "continuum": build_service_access_entry("continuum", "none", "observe"),
        },
    ),
    "continuum-control-token": build_identity(
        "continuum-controller",
        service_access={
            "continuum": build_service_access_entry("continuum", "control", "observe"),
        },
    ),
    "tracey-observe-token": build_identity(
        "tracey-observer",
        service_access={
            "tracey": build_service_access_entry("tracey", "none", "observe"),
        },
    ),
    "tracey-use-token": build_identity(
        "tracey-operator",
        service_access={
            "tracey": build_service_access_entry("tracey", "use", "observe"),
        },
    ),
    "tracey-control-token": build_identity(
        "tracey-controller",
        service_access={
            "tracey": build_service_access_entry("tracey", "control", "observe"),
        },
    ),
    "gail-trading-observe-token": build_identity(
        "gail-trading-observer",
        service_access={
            "gail_trading": build_service_access_entry("gail_trading", "observe", "none"),
        },
    ),
    "gail-trading-control-token": build_identity(
        "gail-trading-controller",
        service_access={
            "gail_trading": build_service_access_entry("gail_trading", "control", "none"),
        },
    ),
    "gail-observe-token": build_identity(
        "gail-observer",
        service_access={
            "gail": build_service_access_entry("gail", "observe", "none"),
        },
    ),
    "gail-control-token": build_identity(
        "gail-controller",
        service_access={
            "gail": build_service_access_entry("gail", "control", "none"),
        },
    ),
    "aarnn-request-token": build_identity(
        "aarnn-requester",
        service_access={
            "aarnn": build_service_access_entry("aarnn", "request", "request"),
        },
    ),
    "aarnn-observe-token": build_identity(
        "aarnn-observer",
        service_access={
            "aarnn": build_service_access_entry("aarnn", "observe", "request"),
        },
    ),
    "aarnn-use-token": build_identity(
        "aarnn-runtime",
        service_access={
            "aarnn": build_service_access_entry("aarnn", "use", "request"),
        },
    ),
    "aarnn-control-token": build_identity(
        "aarnn-controller",
        service_access={
            "aarnn": build_service_access_entry("aarnn", "control", "request"),
        },
    ),
    "continuum-service-account-token": build_identity(
        "continuum-sync",
        role="service_account",
        groups=["ops"],
        identity_type="service_account",
        service_access={
            "continuum": build_service_access_entry("continuum", "use", "observe"),
        },
    ),
}


@dataclass(frozen=True)
class RequestRecord:
    method: str
    path: str
    authorization: str
    body: str


class MockBackend:
    def __init__(self) -> None:
        self._records: list[RequestRecord] = []
        self._lock = threading.Lock()
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

        outer = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, fmt: str, *args: object) -> None:
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
            raise RuntimeError("mock backend is not started")
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

    def count_requests(self, path: str, method: str | None = None) -> int:
        wanted_path = path.strip()
        wanted_method = method.upper() if method else None
        with self._lock:
            return sum(
                1
                for record in self._records
                if record.path == wanted_path and (wanted_method is None or record.method == wanted_method)
            )

    def _record(self, method: str, path: str, authorization: str, body: str) -> None:
        with self._lock:
            self._records.append(
                RequestRecord(
                    method=method,
                    path=path,
                    authorization=authorization,
                    body=body,
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

    def _handle(self, handler: BaseHTTPRequestHandler) -> None:
        try:
            length = int(handler.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        raw_body = handler.rfile.read(length) if length > 0 else b""
        body = raw_body.decode("utf-8")
        path_only = handler.path.split("?", 1)[0]
        authorization = handler.headers.get("Authorization", "")
        self._record(handler.command, path_only, authorization, body)

        if path_only == "/api/session":
            token = extract_bearer_token(authorization)
            payload = TOKEN_IDENTITIES.get(token or "")
            if not payload:
                self._send_json(handler, 401, {"authenticated": False, "error": "invalid_token"})
                return
            self._send_json(handler, 200, payload)
            return

        if path_only == "/runtime/echo":
            request_payload = parse_json(body)
            self._send_json(
                handler,
                200,
                {
                    "plane": "runtime",
                    "method": handler.command,
                    "path": path_only,
                    "payload": request_payload,
                },
            )
            return

        if path_only == "/control/echo":
            request_payload = parse_json(body)
            self._send_json(
                handler,
                200,
                {
                    "plane": "control",
                    "method": handler.command,
                    "path": path_only,
                    "payload": request_payload,
                },
            )
            return

        if path_only.startswith("/v1/trading/"):
            request_payload = parse_json(body)
            self._send_json(
                handler,
                200,
                {
                    "ok": True,
                    "path": path_only,
                    "method": handler.command,
                    "payload": request_payload,
                },
            )
            return

        if path_only == "/tracey-target/control/tracey_guard":
            request_payload = parse_json(body)
            self._send_json(
                handler,
                200,
                {
                    "control": request_payload,
                    "summary": {"enabled": request_payload.get("enabled", True)},
                    "updated_ms": int(time.time() * 1000),
                },
            )
            return

        self._send_json(handler, 404, {"error": "not_found", "path": path_only})


class NmcServerProcess:
    def __init__(self, backend_base_url: str) -> None:
        self._backend_base_url = backend_base_url
        self._tmp_dir = tempfile.TemporaryDirectory(prefix="nmc-server-authz-")
        self._home_dir = pathlib.Path(self._tmp_dir.name)
        self._log_path = self._home_dir / "nmc_server.log"
        self._port = reserve_port()
        self._process: subprocess.Popen[str] | None = None

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self._port}"

    @property
    def log_path(self) -> pathlib.Path:
        return self._log_path

    def start(self) -> None:
        write_k8s_config(self._home_dir)
        env = os.environ.copy()
        env.update(
            {
                "HOME": str(self._home_dir),
                "NMC_LOG_FILE": str(self._log_path),
                "NMC_AUTH_MODE": "token",
                "NMC_AUTH_TOKEN": STATIC_ADMIN_TOKEN,
                "NMC_CENTRAL_AUTH_SESSION_URL": f"{self._backend_base_url}/api/session",
                "NMC_CENTRAL_AUTH_TIMEOUT_MS": "1000",
                "NMC_CENTRAL_AUTH_CACHE_TTL_MS": "1000",
                "NMC_DOCS_ENABLED": "1",
                "NMC_TRACEY_DISCOVERY_ENABLED": "0",
                "NMC_AARNN_DISCOVERY_ENABLED": "0",
                "NMC_AARNN_RUNTIME_URL": f"{self._backend_base_url}/runtime",
                "NMC_AARNN_CONTROL_URL": f"{self._backend_base_url}/control",
                "NMC_GAIL_BASE_URL": self._backend_base_url,
                "NMC_GAIL_API_TOKEN": "gail-backend-token",
            }
        )
        log_file = self._log_path.open("w", encoding="utf-8")
        self._process = subprocess.Popen(
            [str(NMC_SERVER_BIN), "--port", str(self._port)],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            stdout=log_file,
            stderr=subprocess.STDOUT,
        )
        wait_for_server_ready(self.base_url, self._process, self._log_path)

    def stop(self) -> None:
        if self._process is not None and self._process.poll() is None:
            self._process.terminate()
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait(timeout=10)
        self._process = None
        self._tmp_dir.cleanup()


def reserve_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def write_k8s_config(home_dir: pathlib.Path) -> None:
    config_dir = home_dir / ".nmc"
    config_dir.mkdir(parents=True, exist_ok=True)
    config_path = config_dir / "config.json"
    config_path.write_text(
        json.dumps(
            {
                "server": "127.0.0.1",
                "port": 6443,
                "token": "integration-token",
            },
            indent=2,
        ),
        encoding="utf-8",
    )


def wait_for_server_ready(
    base_url: str,
    process: subprocess.Popen[str],
    log_path: pathlib.Path,
) -> None:
    deadline = time.time() + 20
    last_error: Exception | None = None
    while time.time() < deadline:
        if process.poll() is not None:
            log_output = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
            raise AssertionError(
                f"nmc_server exited early with code {process.returncode}.\n{log_output}"
            )
        try:
            status, payload = request_json(base_url, "GET", "/health")
            if status == 200 and isinstance(payload, dict) and payload.get("success") is True:
                return
        except Exception as exc:  # noqa: BLE001
            last_error = exc
        time.sleep(0.25)
    raise AssertionError(f"nmc_server did not become ready in time: {last_error}")


def extract_bearer_token(header_value: str) -> str | None:
    raw = str(header_value or "").strip()
    if raw.startswith("Bearer "):
        return raw[7:].strip()
    if raw.startswith("bearer "):
        return raw[7:].strip()
    return None


def parse_json(raw: str) -> Any:
    if not raw:
        return {}
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {"raw_body": raw}


def request_json(
    base_url: str,
    method: str,
    path: str,
    *,
    token: str | None = None,
    payload: dict[str, Any] | None = None,
) -> tuple[int, Any]:
    url = f"{base_url}{path}"
    body = json.dumps(payload).encode("utf-8") if payload is not None else None
    request = urllib.request.Request(url, data=body, method=method)
    request.add_header("Accept", "application/json")
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    if payload is not None:
        request.add_header("Content-Type", "application/json")

    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            raw_body = response.read().decode("utf-8")
            parsed = parse_json(raw_body)
            return response.status, parsed
    except urllib.error.HTTPError as exc:
        raw_body = exc.read().decode("utf-8")
        parsed = parse_json(raw_body)
        return exc.code, parsed


def assert_true(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def assert_status(status: int, expected: int, label: str) -> None:
    assert_true(status == expected, f"{label} expected HTTP {expected}, got {status}")


def test_auth_session_reflects_central_identity(server: NmcServerProcess) -> None:
    status, payload = request_json(server.base_url, "GET", "/auth/session", token="tracey-use-token")
    assert_status(status, 200, "auth/session central identity")
    assert_true(payload.get("authenticated") is True, "auth/session should mark identity authenticated")
    assert_true(payload.get("user") == "tracey-operator", "auth/session should preserve the central user")
    service_access = payload.get("service_access") or {}
    tracey = service_access.get("tracey") or {}
    assert_true(tracey.get("can_use") is True, "auth/session should resolve tracey use access")
    assert_true("tracey" in (payload.get("visible_services") or []), "auth/session should expose visible tracey service")


def test_auth_session_preserves_service_account_groups(server: NmcServerProcess) -> None:
    status, payload = request_json(server.base_url, "GET", "/auth/session", token="continuum-service-account-token")
    assert_status(status, 200, "auth/session service-account identity")
    assert_true(payload.get("authenticated") is True, "service-account auth/session should be authenticated")
    assert_true(payload.get("identity_type") == "service_account", "identity_type should be preserved")
    assert_true(payload.get("role") == "service_account", "role should be preserved for service accounts")
    assert_true(payload.get("groups") == ["ops"], "service-account groups should remain explicit only")
    continuum = ((payload.get("service_access") or {}).get("continuum") or {})
    assert_true(continuum.get("can_use") is True, "service-account session should preserve explicit continuum use access")


def test_auth_session_supports_static_admin_token(server: NmcServerProcess) -> None:
    status, payload = request_json(server.base_url, "GET", "/auth/session", token=STATIC_ADMIN_TOKEN)
    assert_status(status, 200, "auth/session static admin token")
    assert_true(payload.get("user") == "service-token", "static admin token should resolve service-token identity")
    assert_true("admin" in (payload.get("groups") or []), "static admin token should resolve admin group membership")
    continuum = ((payload.get("service_access") or {}).get("continuum") or {})
    assert_true(continuum.get("can_control") is True, "static admin token should resolve continuum control access")


def test_continuum_route_authorisation(server: NmcServerProcess) -> None:
    status, payload = request_json(server.base_url, "GET", "/server/version", token="continuum-observe-token")
    assert_status(status, 200, "continuum observe route")
    assert_true(payload.get("success") is True, "continuum observe route should succeed")

    status, _ = request_json(server.base_url, "GET", "/connections", token="continuum-observe-token")
    assert_status(status, 403, "continuum control route denied")

    status, payload = request_json(server.base_url, "GET", "/connections", token="continuum-control-token")
    assert_status(status, 200, "continuum control route allowed")
    assert_true(payload.get("success") is True, "continuum control route should succeed with control access")

    status, payload = request_json(server.base_url, "GET", "/connections", token=STATIC_ADMIN_TOKEN)
    assert_status(status, 200, "continuum static admin control route")
    assert_true(payload.get("success") is True, "static admin token should pass continuum control route")


def test_tracey_route_authorisation(server: NmcServerProcess, backend: MockBackend) -> None:
    status, payload = request_json(server.base_url, "GET", "/tracey/analytics", token="tracey-observe-token")
    assert_status(status, 200, "tracey observe route")
    assert_true(payload.get("success") is True, "tracey observe route should succeed")

    heartbeat_payload = {
        "agent_id": "authz-agent-1",
        "status": "healthy",
        "cluster": "authz-cluster",
        "status_addr": f"{backend.base_url}/tracey-target",
    }

    status, _ = request_json(
        server.base_url,
        "POST",
        "/tracey/agents/heartbeat",
        token="tracey-observe-token",
        payload=heartbeat_payload,
    )
    assert_status(status, 403, "tracey use route denied")

    status, payload = request_json(
        server.base_url,
        "POST",
        "/tracey/agents/heartbeat",
        token="tracey-use-token",
        payload=heartbeat_payload,
    )
    assert_status(status, 200, "tracey use route allowed")
    assert_true(payload.get("success") is True, "tracey heartbeat should succeed with use access")

    backend.clear_records()
    status, _ = request_json(
        server.base_url,
        "POST",
        "/tracey/agents/authz-agent-1/control",
        token="tracey-use-token",
        payload={"enabled": False},
    )
    assert_status(status, 403, "tracey control route denied")
    assert_true(
        backend.count_requests("/tracey-target/control/tracey_guard", "POST") == 0,
        "denied tracey control requests should not reach the upstream status endpoint",
    )

    status, payload = request_json(
        server.base_url,
        "POST",
        "/tracey/agents/authz-agent-1/control",
        token="tracey-control-token",
        payload={"enabled": False},
    )
    assert_status(status, 200, "tracey control route allowed")
    assert_true(payload.get("success") is True, "tracey control should succeed with control access")
    assert_true(
        backend.count_requests("/tracey-target/control/tracey_guard", "POST") == 1,
        "allowed tracey control requests should reach the upstream status endpoint exactly once",
    )


def test_aarnn_route_authorisation(server: NmcServerProcess, backend: MockBackend) -> None:
    status, _ = request_json(server.base_url, "GET", "/aarnn/endpoints", token="aarnn-request-token")
    assert_status(status, 403, "aarnn observe route denied")

    status, payload = request_json(server.base_url, "GET", "/aarnn/endpoints", token="aarnn-observe-token")
    assert_status(status, 200, "aarnn observe route allowed")
    assert_true(payload.get("success") is True, "aarnn endpoints should succeed with observe access")

    runtime_payload = {
        "method": "POST",
        "path": "/echo",
        "json": {"mode": "runtime"},
    }

    backend.clear_records()
    status, _ = request_json(
        server.base_url,
        "POST",
        "/aarnn/proxy/runtime",
        token="aarnn-observe-token",
        payload=runtime_payload,
    )
    assert_status(status, 403, "aarnn runtime route denied")
    assert_true(
        backend.count_requests("/runtime/echo", "POST") == 0,
        "denied aarnn runtime requests should not reach the upstream runtime endpoint",
    )

    status, payload = request_json(
        server.base_url,
        "POST",
        "/aarnn/proxy/runtime",
        token="aarnn-use-token",
        payload=runtime_payload,
    )
    assert_status(status, 200, "aarnn runtime route allowed")
    assert_true(payload.get("success") is True, "aarnn runtime proxy should succeed with use access")
    assert_true(
        backend.count_requests("/runtime/echo", "POST") == 1,
        "allowed aarnn runtime requests should reach the upstream runtime endpoint exactly once",
    )

    control_payload = {
        "method": "POST",
        "path": "/echo",
        "json": {"mode": "control"},
    }

    backend.clear_records()
    status, _ = request_json(
        server.base_url,
        "POST",
        "/aarnn/proxy/control",
        token="aarnn-use-token",
        payload=control_payload,
    )
    assert_status(status, 403, "aarnn control route denied")
    assert_true(
        backend.count_requests("/control/echo", "POST") == 0,
        "denied aarnn control requests should not reach the upstream control endpoint",
    )

    status, payload = request_json(
        server.base_url,
        "POST",
        "/aarnn/proxy/control",
        token="aarnn-control-token",
        payload=control_payload,
    )
    assert_status(status, 200, "aarnn control route allowed")
    assert_true(payload.get("success") is True, "aarnn control proxy should succeed with control access")
    assert_true(
        backend.count_requests("/control/echo", "POST") == 1,
        "allowed aarnn control requests should reach the upstream control endpoint exactly once",
    )


def test_gail_trading_route_authorisation(server: NmcServerProcess, backend: MockBackend) -> None:
    backend.clear_records()
    status, _ = request_json(server.base_url, "GET", "/gail/trading/status", token="continuum-observe-token")
    assert_status(status, 403, "gail trading observe route denied without gail_trading access")
    assert_true(
        backend.count_requests("/v1/trading/status", "GET") == 0,
        "denied Gail Trading status requests should not reach Gail",
    )

    status, payload = request_json(
        server.base_url,
        "GET",
        "/gail/trading/status",
        token="gail-trading-observe-token",
    )
    assert_status(status, 200, "gail trading observe route allowed")
    assert_true(
        (((payload.get("data") or {}).get("payload") or {}).get("ok")) is True,
        "Gail Trading status should proxy with observe access",
    )
    assert_true(
        backend.count_requests("/v1/trading/status", "GET") == 1,
        "allowed Gail Trading status requests should reach Gail exactly once",
    )

    status, payload = request_json(
        server.base_url,
        "GET",
        "/gail/trading/status",
        token="gail-observe-token",
    )
    assert_status(status, 200, "gail trading observe route allowed with Gail service access")
    assert_true(
        (((payload.get("data") or {}).get("payload") or {}).get("ok")) is True,
        "Gail Trading status should proxy with Gail observe access",
    )
    assert_true(
        backend.count_requests("/v1/trading/status", "GET") == 2,
        "Gail service access should reach Gail status exactly once",
    )

    backend.clear_records()
    status, _ = request_json(
        server.base_url,
        "POST",
        "/gail/trading/pause",
        token="gail-trading-observe-token",
        payload={},
    )
    assert_status(status, 403, "gail trading control route denied with observe access")
    assert_true(
        backend.count_requests("/v1/trading/pause", "POST") == 0,
        "denied Gail Trading control requests should not reach Gail",
    )

    status, payload = request_json(
        server.base_url,
        "POST",
        "/gail/trading/pause",
        token="gail-trading-control-token",
        payload={},
    )
    assert_status(status, 200, "gail trading control route allowed")
    assert_true(
        (((payload.get("data") or {}).get("payload") or {}).get("ok")) is True,
        "Gail Trading control should proxy with control access",
    )
    assert_true(
        backend.count_requests("/v1/trading/pause", "POST") == 1,
        "allowed Gail Trading control requests should reach Gail exactly once",
    )

    status, payload = request_json(
        server.base_url,
        "POST",
        "/gail/trading/pause",
        token="gail-control-token",
        payload={},
    )
    assert_status(status, 200, "gail trading control route allowed with Gail service access")
    assert_true(
        (((payload.get("data") or {}).get("payload") or {}).get("ok")) is True,
        "Gail Trading control should proxy with Gail control access",
    )
    assert_true(
        backend.count_requests("/v1/trading/pause", "POST") == 2,
        "Gail service control access should reach Gail pause exactly once",
    )


def main() -> int:
    if not NMC_SERVER_BIN.exists():
        print(
            f"[server-authz-test] nmc_server binary not found at {NMC_SERVER_BIN}. "
            "Build with: cmake -S nmc_server -B nmc_server/build && cmake --build nmc_server/build -j4",
            file=sys.stderr,
        )
        return 1

    backend = MockBackend()
    backend.start()
    server = NmcServerProcess(backend.base_url)
    try:
        server.start()
        test_auth_session_reflects_central_identity(server)
        test_auth_session_preserves_service_account_groups(server)
        test_auth_session_supports_static_admin_token(server)
        test_continuum_route_authorisation(server)
        test_tracey_route_authorisation(server, backend)
        test_aarnn_route_authorisation(server, backend)
        test_gail_trading_route_authorisation(server, backend)
    except AssertionError as exc:
        log_output = server.log_path.read_text(encoding="utf-8", errors="replace") if server.log_path.exists() else ""
        print(f"[server-authz-test] FAILED: {exc}", file=sys.stderr)
        if log_output:
            print("[server-authz-test] nmc_server log:", file=sys.stderr)
            print(log_output, file=sys.stderr)
        return 1
    finally:
        server.stop()
        backend.stop()

    print(
        "[server-authz-test] OK: validated end-to-end Continuum, Tracey, Gail Trading, and AARNN route authorisation "
        "against the real nmc_server process."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
