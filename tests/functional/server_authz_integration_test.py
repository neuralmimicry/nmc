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

import base64
import json
import os
import pathlib
import socket
import ssl
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

        if path_only == "/ha/api/states":
            if authorization != "Bearer authz-home-assistant-token":
                self._send_json(handler, 401, {"error": "unauthorized"})
                return
            self._send_json(
                handler,
                200,
                [
                    {"entity_id": "sensor.authz_device", "state": "online", "last_changed": "2026-10-07T10:00:00Z"},
                    {"entity_id": "person.operator", "state": "home"},
                ],
            )
            return

        if path_only == "/dhcp/api/v1/dhcp/leases":
            if authorization != "Bearer authz-dhcp-token":
                self._send_json(handler, 401, {"error": "unauthorized"})
                return
            self._send_json(
                handler,
                200,
                {
                    "schema_version": 1,
                    "server_id": "vega-dhcp",
                    "observed_at_unix_ms": int(time.time() * 1000),
                    "leases": [
                        {
                            "address": "192.168.1.44",
                            "mac_address": "AA-BB-CC-DD-EE-FF",
                            "client_id": "01:aabbccddeeff",
                            "hostname": "authz-device",
                            "lease_expires_at_unix_ms": int((time.time() + 3600) * 1000),
                            "options": [
                                {"code": 1, "name": "subnet-mask", "value": "255.255.255.0", "source": "dhcp_ack", "observed_at_unix_ms": int(time.time() * 1000)},
                                {"code": 3, "name": "router", "value": ["192.168.1.1"], "source": "dhcp_ack", "observed_at_unix_ms": int(time.time() * 1000)},
                                {"code": 6, "name": "domain-name-server", "value": ["192.168.1.1"], "source": "server_policy", "observed_at_unix_ms": int(time.time() * 1000)},
                                {"code": 55, "name": "parameter-request-list", "value": [1, 3, 6, 15, 119], "source": "client_request", "observed_at_unix_ms": int(time.time() * 1000)},
                            ],
                        },
                        {
                            "address": "192.168.1.88",
                            "mac_address": "02:00:00:00:00:88",
                            "client_id": "unknown-client-88",
                            "hostname": "unregistered-device",
                            "lease_expires_at_unix_ms": int((time.time() + 3600) * 1000),
                            "options": [],
                        },
                    ],
                },
            )
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
            if path_only == "/v1/trading/status":
                upstream_payload = {
                    "enabled": True,
                    "paused": False,
                    "evaluation_count": 4,
                    "trade_count": 1,
                    "last_trade_at": 1_700_000_123,
                }
            elif path_only == "/v1/trading/history":
                upstream_payload = {
                    "trades": [
                        {
                            "symbol": "BTC/USDT",
                            "action": "buy",
                            "ts": 1_700_000_123,
                            "amount_usd": 25.5,
                            "exchange": "binance",
                        }
                    ]
                }
            else:
                upstream_payload = {}
            self._send_json(
                handler,
                200,
                {
                    "ok": True,
                    "path": path_only,
                    "method": handler.command,
                    "payload": upstream_payload if handler.command == "GET" else request_payload,
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


class RedfishBmcMock:
    """HTTPS Redfish endpoint with vendor-shaped IDs and manager resources."""

    PROFILES: dict[str, dict[str, Any]] = {
        "hp_ilo": {
            "target": "1",
            "manufacturer": "Hewlett Packard Enterprise",
            "product": "HPE Integrated Lights-Out",
            "system_model": "ProLiant DL380 Gen10",
            "manager_id": "1",
            "manager_name": "HPE iLO 6",
            "firmware": "1.60",
        },
        "dell_idrac": {
            "target": "System.Embedded.1",
            "manufacturer": "Dell Inc.",
            "product": "iDRAC9",
            "system_model": "PowerEdge R750",
            "manager_id": "iDRAC.Embedded.1",
            "manager_name": "Dell iDRAC",
            "firmware": "7.10.30.00",
        },
        "ami_megarac": {
            "target": "BMC",
            "manufacturer": "American Megatrends International",
            "product": "MegaRAC SP-X",
            "system_model": "AMI Server Platform",
            "manager_id": "BMC",
            "manager_name": "AMI MegaRAC BMC",
            "firmware": "12.34.5",
        },
    }

    def __init__(self, parent_dir: pathlib.Path) -> None:
        self._parent_dir = parent_dir
        self._cert_path = parent_dir / "redfish-test-cert.pem"
        self._key_path = parent_dir / "redfish-test-key.pem"
        self._records: list[tuple[str, str]] = []
        self._lock = threading.Lock()
        self._profile_name = "hp_ilo"
        self._unsafe_systems_link = False
        self._reset_capabilities = ["GracefulRestart", "PowerCycle"]
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-keyout", str(self._key_path), "-out", str(self._cert_path),
                "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1",
            ],
            check=True,
            capture_output=True,
            timeout=20,
        )

        outer = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, fmt: str, *args: object) -> None:
                return

            def do_GET(self) -> None:
                outer._handle(self)

            def do_POST(self) -> None:
                outer._handle(self)

        self._handler_cls = Handler

    @property
    def base_url(self) -> str:
        if not self._httpd:
            raise RuntimeError("mock Redfish service is not started")
        host, port = self._httpd.server_address
        return f"https://{host}:{port}"

    @property
    def ca_file(self) -> pathlib.Path:
        return self._cert_path

    def start(self) -> None:
        self._httpd = ThreadingHTTPServer(("127.0.0.1", 0), self._handler_cls)
        tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls_context.load_cert_chain(str(self._cert_path), str(self._key_path))
        self._httpd.socket = tls_context.wrap_socket(self._httpd.socket, server_side=True)
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

    def select_profile(self, profile_name: str) -> None:
        self._profile_name = profile_name
        self._unsafe_systems_link = False
        with self._lock:
            self._records.clear()

    def enable_unsafe_systems_link(self) -> None:
        self._unsafe_systems_link = True
        with self._lock:
            self._records.clear()

    def set_reset_capabilities(self, values: list[str]) -> None:
        self._reset_capabilities = list(values)
        with self._lock:
            self._records.clear()

    def records(self) -> list[tuple[str, str]]:
        with self._lock:
            return list(self._records)

    def _send_json(self, handler: BaseHTTPRequestHandler, status: int, payload: object) -> None:
        data = json.dumps(payload).encode("utf-8")
        handler.send_response(status)
        handler.send_header("Content-Type", "application/json")
        handler.send_header("Content-Length", str(len(data)))
        handler.end_headers()
        handler.wfile.write(data)

    def _handle(self, handler: BaseHTTPRequestHandler) -> None:
        path = handler.path.split("?", 1)[0]
        with self._lock:
            self._records.append((handler.command, path))
        expected_auth = "Basic " + base64.b64encode(b"test-bmc-user:test-bmc-password").decode("ascii")
        if handler.headers.get("Authorization") != expected_auth:
            self._send_json(handler, 401, {"error": "unauthorized"})
            return
        profile = self.PROFILES[self._profile_name]
        target = str(profile["target"])
        system_path = f"/redfish/v1/Systems/{target}"
        manager_path = f"/redfish/v1/Managers/{profile['manager_id']}"
        if path == "/redfish/v1/":
            systems_link = "https://attacker.invalid/redfish/v1/Systems" if self._unsafe_systems_link else "/redfish/v1/Systems"
            self._send_json(
                handler,
                200,
                {
                    "RedfishVersion": "1.15.0",
                    "Vendor": profile["manufacturer"],
                    "Product": profile["product"],
                    "Systems": {"@odata.id": systems_link},
                    "Managers": {"@odata.id": "/redfish/v1/Managers"},
                },
            )
        elif path == "/redfish/v1/Systems":
            self._send_json(handler, 200, {"Members": [{"@odata.id": system_path}]})
        elif path == system_path:
            self._send_json(
                handler,
                200,
                {
                    "Id": target,
                    "Name": "Managed test host",
                    "Manufacturer": profile["manufacturer"],
                    "Model": profile["system_model"],
                    "PowerState": "On",
                    "Status": {"State": "Enabled", "Health": "OK"},
                    "Actions": {
                        "#ComputerSystem.Reset": {
                            "target": f"/redfish/v1/Systems/{target}/Actions/ComputerSystem.Reset",
                            "ResetType@Redfish.AllowableValues": self._reset_capabilities,
                        }
                    },
                },
            )
        elif path == "/redfish/v1/Managers":
            self._send_json(handler, 200, {"Members": [{"@odata.id": manager_path}]})
        elif path == manager_path:
            self._send_json(
                handler,
                200,
                {
                    "Id": str(profile["manager_id"]),
                    "Name": profile["manager_name"],
                    "ManagerType": "BMC",
                    "Manufacturer": profile["manufacturer"],
                    "Model": profile["product"],
                    "FirmwareVersion": profile["firmware"],
                    "Status": {"State": "Enabled", "Health": "OK"},
                },
            )
        elif path == f"/redfish/v1/Systems/{target}/Actions/ComputerSystem.Reset" and handler.command == "POST":
            self._send_json(handler, 202, {"success": True})
        else:
            self._send_json(handler, 404, {"error": "not_found", "path": path})


class NmcServerProcess:
    def __init__(
        self,
        backend_base_url: str,
        cluster_id: str | None = "authz-test-cluster",
        load_kubeconfig: bool = True,
        device_power_control_enabled: bool = False,
    ) -> None:
        self._backend_base_url = backend_base_url
        self._cluster_id = cluster_id
        self._load_kubeconfig = load_kubeconfig
        self._device_power_control_enabled = device_power_control_enabled
        self._tmp_dir = tempfile.TemporaryDirectory(prefix="nmc-server-authz-")
        self._home_dir = pathlib.Path(self._tmp_dir.name)
        self._log_path = self._home_dir / "nmc_server.log"
        self.inventory_path: pathlib.Path | None = None
        self._port = reserve_port()
        self._process: subprocess.Popen[str] | None = None

    @property
    def base_url(self) -> str:
        return f"http://127.0.0.1:{self._port}"

    @property
    def log_path(self) -> pathlib.Path:
        return self._log_path

    def start(self) -> None:
        write_k8s_config(self._home_dir, load_kubeconfig=self._load_kubeconfig)
        inventory_path = write_device_inventory(self._home_dir, self._backend_base_url)
        self.inventory_path = inventory_path
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
                "NMC_TRACEY_CVE_ENABLED": "0",
                "NMC_AARNN_DISCOVERY_ENABLED": "0",
                "NMC_AARNN_RUNTIME_URL": f"{self._backend_base_url}/runtime",
                "NMC_AARNN_CONTROL_URL": f"{self._backend_base_url}/control",
                "NMC_GAIL_BASE_URL": self._backend_base_url,
                "NMC_GAIL_API_TOKEN": "gail-backend-token",
                "NMC_DEVICE_INVENTORY_PATH": str(inventory_path),
                "NMC_DEVICE_ACTION_AUDIT_PATH": str(self._home_dir / ".nmc" / "device-actions.jsonl"),
                "AUTHZ_DHCP_TOKEN": "authz-dhcp-token",
                "AUTHZ_BMC_USERNAME": "test-bmc-user",
                "AUTHZ_BMC_PASSWORD": "test-bmc-password",
                "AUTHZ_HA_TOKEN": "authz-home-assistant-token",
            }
        )
        env.pop("NMC_DEVICE_POWER_CONTROL_ENABLED", None)
        if self._device_power_control_enabled:
            env["NMC_DEVICE_POWER_CONTROL_ENABLED"] = "true"
        env.pop("NMC_K8S_CLUSTER_ID", None)
        if self._cluster_id is not None:
            env["NMC_K8S_CLUSTER_ID"] = self._cluster_id
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


def write_k8s_config(home_dir: pathlib.Path, *, load_kubeconfig: bool = True) -> None:
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
    if load_kubeconfig:
        kubeconfig_dir = home_dir / ".kube"
        kubeconfig_dir.mkdir(parents=True, exist_ok=True)
        (kubeconfig_dir / "config").write_text(
            """apiVersion: v1
kind: Config
clusters:
- cluster:
    insecure-skip-tls-verify: true
    server: https://127.0.0.1:6443
  name: authz-test-cluster
contexts:
- context:
    cluster: authz-test-cluster
    user: authz-test-user
  name: authz-test-cluster
current-context: authz-test-cluster
users:
- name: authz-test-user
  user:
    token: integration-token
""",
            encoding="utf-8",
        )


def write_device_inventory(home_dir: pathlib.Path, backend_base_url: str) -> pathlib.Path:
    inventory_path = home_dir / ".nmc" / "device-inventory.json"
    inventory_path.parent.mkdir(parents=True, exist_ok=True)
    inventory_path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "revision": "authz-device-inventory-v1",
                "dependency_graph_revision": "authz-dependency-graph-v1",
                "generated_at_unix_ms": int(time.time() * 1000),
                "max_age_ms": 300000,
                "devices": [
                    {
                        "id": "authz-device",
                        "kind": "compute_host",
                        "environment": "test",
                        "criticality": "low",
                        "addresses": ["127.0.0.1", "192.168.1.88"],
                        "mac_addresses": ["aa:bb:cc:dd:ee:ff"],
                        "dhcp_client_ids": ["01:aabbccddeeff"],
                        "services": ["authz-test-service"],
                        "depends_on": [],
                        "affected_services": ["authz-test-service"],
                        "home_assistant_entity": "sensor.authz_device",
                        "controller_id": "authz-bmc",
                        "controller_target": "system-1",
                    }
                ],
                "controllers": [
                    {
                        "id": "authz-bmc",
                        "protocol": "redfish",
                        "endpoint": "https://127.0.0.1:9443",
                        "username_env": "AUTHZ_BMC_USERNAME",
                        "password_env": "AUTHZ_BMC_PASSWORD",
                        "manages": ["authz-device"],
                        "power_actions_enabled": False,
                    }
                ],
                "home_assistant": {
                    "base_url": f"{backend_base_url}/ha",
                    "token_env": "AUTHZ_HA_TOKEN",
                    "allow_http": True,
                    "entities": ["sensor.authz_device"],
                },
                "dhcp_servers": [
                    {
                        "id": "authz-vega-dhcp",
                        "server_id": "vega-dhcp",
                        "endpoint": f"{backend_base_url}/dhcp",
                        "allow_http": True,
                        "token_env": "AUTHZ_DHCP_TOKEN",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    # Match Ansible's protected runtime file rather than the process umask.
    inventory_path.chmod(0o600)
    return inventory_path


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


def test_continuum_route_authorisation(server: NmcServerProcess, backend: MockBackend) -> None:
    status, payload = request_json(server.base_url, "GET", "/server/version", token="continuum-observe-token")
    assert_status(status, 200, "continuum observe route")
    assert_true(payload.get("success") is True, "continuum observe route should succeed")

    status, _ = request_json(server.base_url, "GET", "/connections", token="continuum-observe-token")
    assert_status(status, 403, "continuum control route denied")

    status, payload = request_json(server.base_url, "GET", "/connections", token="continuum-control-token")
    assert_status(status, 200, "continuum control route allowed")
    assert_true(payload.get("success") is True, "continuum control route should succeed with control access")

    status, payload = request_json(server.base_url, "GET", "/devices/inventory", token="continuum-observe-token")
    assert_true(status == 200, f"device inventory observe route expected HTTP 200, got {status}: {payload}")
    inventory = payload.get("data", {})
    assert_true(inventory.get("revision") == "authz-device-inventory-v1", "inventory should expose its active revision")
    assert_true(len(inventory.get("devices", [])) == 1, "inventory should include the registered host")
    assert_true(len(inventory.get("controllers", [])) == 1, "inventory should include the registered BMC")
    assert_true("AUTHZ_BMC_PASSWORD" not in json.dumps(inventory), "inventory must not reveal credential references")

    status, payload = request_json(server.base_url, "GET", "/devices/home-assistant", token="continuum-observe-token")
    assert_status(status, 200, "Home Assistant supplemental device data")
    entities = payload.get("data", {}).get("entities", [])
    assert_true([item.get("entity_id") for item in entities] == ["sensor.authz_device"], "Home Assistant results must be limited to explicitly allowlisted device entities")
    assert_true(all(item.get("entity_id") != "person.operator" for item in entities), "unrelated Home Assistant personal entities must not be exposed")

    status, payload = request_json(server.base_url, "GET", "/devices/dhcp", token="continuum-observe-token")
    assert_status(status, 200, "read-only DHCP lease and option observations")
    dhcp = payload.get("data", {})
    dhcp_servers = dhcp.get("servers", [])
    assert_true(len(dhcp_servers) == 1 and dhcp_servers[0].get("available") is True, f"verified DHCP collector should be queried: {dhcp_servers}")
    leases = dhcp_servers[0].get("leases", [])
    assert_true(len(leases) == 2, "DHCP collector leases should be returned")
    verified_lease = leases[0]
    assert_true(verified_lease.get("identity_status") == "verified" and verified_lease.get("device_id") == "authz-device", "verified MAC/client ID should reconcile to the registered device")
    assert_true(verified_lease.get("mac_address") == "aa:bb:cc:dd:ee:ff", "lease MAC should be canonicalized")
    assert_true([option.get("code") for option in verified_lease.get("received_options", [])] == [1, 3], "only captured DHCPACK options should be reported as received")
    assert_true([option.get("code") for option in verified_lease.get("advertised_options", [])] == [6], "server policy options must remain distinct from options proven in the DHCPACK")
    assert_true([option.get("code") for option in verified_lease.get("requested_options", [])] == [55], "client-requested options must remain distinct from received options")
    assert_true(leases[1].get("identity_status") == "unknown" and "device_id" not in leases[1], "unknown clients must not be identified from IP address or hostname")
    assert_true("authz-dhcp-token" not in json.dumps(dhcp), "DHCP collector credentials must not be returned")
    assert_true(server.inventory_path is not None, "integration server should retain its inventory path for source identity checks")
    inventory_document = json.loads(server.inventory_path.read_text(encoding="utf-8"))
    inventory_document["dhcp_servers"][0]["server_id"] = "unverified-dhcp-server"
    server.inventory_path.write_text(json.dumps(inventory_document), encoding="utf-8")
    status, payload = request_json(server.base_url, "GET", "/devices/dhcp", token="continuum-observe-token")
    assert_status(status, 200, "mismatched DHCP server identity remains a read-only observation")
    mismatched_sources = payload.get("data", {}).get("servers", [])
    assert_true(
        len(mismatched_sources) == 1 and mismatched_sources[0].get("available") is False
        and mismatched_sources[0].get("error", {}).get("code") == "dhcp_response_invalid",
        "collector responses with a server identifier different from the verified inventory must be rejected",
    )
    assert_true(backend.count_requests("/dhcp/api/v1/dhcp/leases", "GET") == 2, "Continuum must only issue the expected read-only DHCP collector GET requests")
    assert_true(backend.count_requests("/dhcp/api/v1/dhcp/leases", "POST") == 0, "Continuum must not mutate DHCP server state")

    controller_action = {
        "controller_id": "authz-bmc",
        "device_id": "authz-device",
        "action": "warm_restart",
        "request_id": "authz-controller-action-0001",
        "change_id": "AUTHZ-CHANGE-1",
        "reason": "Exercise default-disabled controller action safety.",
        "expected_inventory_revision": "authz-device-inventory-v1",
        "preflight": {
            "observed_at_unix_ms": int(time.time() * 1000),
            "inventory_revision": "authz-device-inventory-v1",
            "dependency_graph_revision": "authz-dependency-graph-v1",
            "expected_power_state": "On",
            "target_identity_verified": True,
            "dependencies_healthy": True,
            "dependents_known": True,
            "affected_services_healthy": True,
            "monitoring_healthy": True,
            "affected_services": ["authz-test-service"],
            "dependency_ids": [],
        },
    }
    status, _ = request_json(
        server.base_url,
        "POST",
        "/devices/controllers/actions",
        token="continuum-observe-token",
        payload=controller_action,
    )
    assert_status(status, 403, "controller power actions require Continuum control access")
    status, payload = request_json(
        server.base_url,
        "POST",
        "/devices/controllers/actions",
        token="continuum-control-token",
        payload=controller_action,
    )
    assert_status(status, 423, "controller power actions are disabled by default")
    assert_true(
        payload.get("error", {}).get("code") == "controller_actions_disabled",
        "default-disabled action must fail closed before contacting a controller",
    )

    status, payload = request_json(server.base_url, "GET", "/connections", token=STATIC_ADMIN_TOKEN)
    assert_status(status, 200, "continuum static admin control route")
    assert_true(payload.get("success") is True, "static admin token should pass continuum control route")

    restart_payload = {
        "namespace": "gail",
        "deployment": "gail",
        "request_id": "authz-test-request-0001",
    }
    status, _ = request_json(
        server.base_url,
        "POST",
        "/k8s/deployment/restart",
        token="continuum-observe-token",
        payload=restart_payload,
    )
    assert_status(status, 403, "deployment restart requires Continuum control access")

    status, payload = request_json(
        server.base_url,
        "GET",
        "/k8s/deployment/recovery-status?cluster_id=authz-test-cluster&namespace=gail&deployment=gail",
        token="continuum-observe-token",
    )
    assert_status(status, 200, "read-only deployment recovery preflight")
    assert_true(payload.get("success") is True, "preflight should return structured state even when blocked")
    assert_true(
        isinstance(payload.get("data", {}).get("eligible"), bool),
        "preflight should report an explicit eligibility decision",
    )
    assert_true(
        payload.get("data", {}).get("cluster_id") == "authz-test-cluster",
        "preflight should identify the active cluster context",
    )

    status, payload = request_json(
        server.base_url,
        "POST",
        "/k8s/deployment/restart",
        token="continuum-control-token",
        payload={
            "namespace": "recovery-authorization-test",
            "cluster_id": "authz-test-cluster",
            "deployment": "definitely-absent",
            "request_id": "recovery-absent-target-0001",
        },
    )
    assert_true(
        status in {404, 409, 502, 503} and payload.get("success") is not True,
        f"an absent or unavailable Deployment must be rejected without reporting restart success (HTTP {status})",
    )
    status, _ = request_json(
        server.base_url,
        "POST",
        "/k8s/deployment/restart",
        token="continuum-control-token",
        payload={
            "namespace": "gail",
            "cluster_id": "different-cluster",
            "deployment": "gail",
            "request_id": "recovery-wrong-cluster-0001",
        },
    )
    assert_status(status, 409, "deployment restart rejects a mismatched cluster identity")


def test_redfish_vendor_diagnostics(server: NmcServerProcess, redfish: RedfishBmcMock) -> None:
    assert_true(server.inventory_path is not None, "integration server should expose its active device inventory")
    inventory_path = server.inventory_path
    original_inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    try:
        for profile_name, sample in redfish.PROFILES.items():
            redfish.select_profile(profile_name)
            inventory = json.loads(json.dumps(original_inventory))
            inventory["devices"][0]["controller_target"] = sample["target"]
            inventory["controllers"][0].update(
                {
                    "endpoint": redfish.base_url,
                    "tls_ca_file": str(redfish.ca_file),
                    "vendor_profile": profile_name,
                }
            )
            inventory_path.write_text(json.dumps(inventory), encoding="utf-8")

            status, payload = request_json(
                server.base_url,
                "GET",
                "/devices/controllers/diagnostics?controller_id=authz-bmc",
                token="continuum-observe-token",
            )
            assert_status(status, 200, f"{profile_name} Redfish diagnostics")
            data = payload.get("data", {})
            assert_true(data.get("vendor_profile") == profile_name, f"{profile_name} inventory profile should be reported")
            assert_true(data.get("detected_vendor") == profile_name, f"{profile_name} should be identified from Redfish data")
            assert_true(data.get("redfish_version") == "1.15.0", "service-root Redfish version should be exposed")
            assert_true(data.get("managers_available") is True, "BMC manager links should be discovered from the service root")
            managers = data.get("managers", [])
            assert_true(len(managers) == 1, f"{profile_name} should expose the linked BMC manager")
            assert_true(managers[0].get("firmware_version") == sample["firmware"], f"{profile_name} firmware should be reported")
            assert_true(managers[0].get("model") == sample["product"], f"{profile_name} manager model should be reported")
            systems = data.get("systems", [])
            assert_true(len(systems) == 1 and systems[0].get("id") == sample["target"], f"{profile_name} system IDs should follow advertised links")
            assert_true(systems[0].get("reset_types") == ["GracefulRestart", "PowerCycle"], "advertised reset capabilities should be reported")
            records = redfish.records()
            assert_true(records and all(method == "GET" for method, _ in records), "read-only vendor diagnostics must issue GET requests only")
            assert_true(all(path.startswith("/redfish/v1/") for _, path in records), "Redfish traversal must stay on validated local service paths")

        redfish.select_profile("hp_ilo")
        redfish.enable_unsafe_systems_link()
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(status, 502, "untrusted Redfish resource link")
        assert_true(payload.get("error", {}).get("code") == "redfish_response_invalid", "unsafe Redfish links must fail closed")
        assert_true(redfish.records() == [("GET", "/redfish/v1/")], "Continuum must not follow an absolute external Redfish link")

        inventory = json.loads(json.dumps(original_inventory))
        inventory["controllers"][0].update({"endpoint": redfish.base_url, "vendor_profile": "hp_ilo"})
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        redfish.select_profile("hp_ilo")
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(status, 502, "untrusted BMC certificate")
        assert_true(payload.get("error", {}).get("code") == "redfish_unavailable", "untrusted certificates must not be bypassed")

        inventory["controllers"][0]["vendor_profile"] = "unsupported_vendor"
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/inventory",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "unsupported BMC vendor profile")
        assert_true(payload.get("error", {}).get("code") == "device_inventory_unavailable", "unknown vendor profiles must invalidate the inventory")
    finally:
        inventory_path.write_text(json.dumps(original_inventory), encoding="utf-8")


def test_redfish_vendor_actions(backend_base_url: str, redfish: RedfishBmcMock) -> None:
    server = NmcServerProcess(backend_base_url, device_power_control_enabled=True)
    server.start()
    try:
        assert_true(server.inventory_path is not None, "action integration server should expose its device inventory")
        inventory_path = server.inventory_path
        original_inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
        try:
            for index, (profile_name, sample) in enumerate(redfish.PROFILES.items(), start=1):
                redfish.select_profile(profile_name)
                redfish.set_reset_capabilities(["GracefulRestart", "PowerCycle"])
                inventory = json.loads(json.dumps(original_inventory))
                inventory["devices"][0]["controller_target"] = sample["target"]
                inventory["controllers"][0].update(
                    {
                        "endpoint": redfish.base_url,
                        "tls_ca_file": str(redfish.ca_file),
                        "vendor_profile": profile_name,
                        "power_actions_enabled": True,
                    }
                )
                inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
                reset_path = f"/redfish/v1/Systems/{sample['target']}/Actions/ComputerSystem.Reset"
                action_payload = {
                    "controller_id": "authz-bmc",
                    "device_id": "authz-device",
                    "action": "warm_restart",
                    "request_id": f"redfish-vendor-action-{index:04d}",
                    "change_id": f"REDFISH-TEST-{index}",
                    "reason": "Exercise Redfish vendor link resolution against an HTTPS mock.",
                    "expected_inventory_revision": "authz-device-inventory-v1",
                    "preflight": {
                        "observed_at_unix_ms": int(time.time() * 1000),
                        "inventory_revision": "authz-device-inventory-v1",
                        "dependency_graph_revision": "authz-dependency-graph-v1",
                        "expected_power_state": "On",
                        "target_identity_verified": True,
                        "dependencies_healthy": True,
                        "dependents_known": True,
                        "affected_services_healthy": True,
                        "monitoring_healthy": True,
                        "affected_services": ["authz-test-service"],
                        "dependency_ids": [],
                    },
                }
                status, payload = request_json(
                    server.base_url,
                    "POST",
                    "/devices/controllers/actions",
                    token="continuum-control-token",
                    payload=action_payload,
                )
                assert_true(status == 200, f"{profile_name} mock Redfish warm restart expected HTTP 200, got {status}: {payload}")
                assert_true(payload.get("success") is True and payload.get("data", {}).get("verified") is True,
                            f"{profile_name} Redfish action should be verified")
                action_records = redfish.records()
                assert_true(action_records.count(("POST", reset_path)) == 1,
                            f"{profile_name} should post to its advertised reset action path")
                assert_true(all(path.startswith("/redfish/v1/") for _, path in action_records),
                            f"{profile_name} action traversal must remain within validated Redfish paths")

            redfish.select_profile("dell_idrac")
            redfish.set_reset_capabilities(["On"])
            inventory = json.loads(json.dumps(original_inventory))
            inventory["devices"][0]["controller_target"] = redfish.PROFILES["dell_idrac"]["target"]
            inventory["controllers"][0].update(
                {
                    "endpoint": redfish.base_url,
                    "tls_ca_file": str(redfish.ca_file),
                    "vendor_profile": "dell_idrac",
                    "power_actions_enabled": True,
                }
            )
            inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
            unsupported_payload = {
                "controller_id": "authz-bmc",
                "device_id": "authz-device",
                "action": "warm_restart",
                "request_id": "redfish-unsupported-reset-0001",
                "change_id": "REDFISH-TEST-CAPABILITY",
                "reason": "Verify unsupported reset types fail before contacting the BMC.",
                "expected_inventory_revision": "authz-device-inventory-v1",
                "preflight": {
                    "observed_at_unix_ms": int(time.time() * 1000),
                    "inventory_revision": "authz-device-inventory-v1",
                    "dependency_graph_revision": "authz-dependency-graph-v1",
                    "expected_power_state": "On",
                    "target_identity_verified": True,
                    "dependencies_healthy": True,
                    "dependents_known": True,
                    "affected_services_healthy": True,
                    "monitoring_healthy": True,
                    "affected_services": ["authz-test-service"],
                    "dependency_ids": [],
                },
            }
            status, payload = request_json(
                server.base_url,
                "POST",
                "/devices/controllers/actions",
                token="continuum-control-token",
                payload=unsupported_payload,
            )
            assert_status(status, 502, "unsupported Redfish reset capability")
            assert_true(payload.get("error", {}).get("code") == "controller_action_failed",
                        "unsupported reset types must be rejected")
            assert_true(all(method == "GET" for method, _ in redfish.records()),
                        "Continuum must not post a reset when the BMC does not advertise GracefulRestart")
        finally:
            inventory_path.write_text(json.dumps(original_inventory), encoding="utf-8")
    finally:
        server.stop()


def test_home_assistant_rejects_plain_http_hostnames(
    server: NmcServerProcess,
    backend: MockBackend,
) -> None:
    assert_true(server.inventory_path is not None, "integration server should expose its active device inventory")
    inventory_path = server.inventory_path
    original_inventory = inventory_path.read_bytes()
    try:
        inventory = json.loads(original_inventory)
        inventory["home_assistant"]["base_url"] = "http://ha.homeassistant.local:8123"
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        previous_requests = backend.count_requests("/ha/api/states", "GET")
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/home-assistant",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "plain HTTP to an inventory hostname must be rejected")
        assert_true(
            payload.get("error", {}).get("code") == "home_assistant_config_invalid",
            "plain HTTP hostnames must fail closed before address resolution",
        )
        assert_true(
            backend.count_requests("/ha/api/states", "GET") == previous_requests,
            "rejected HTTP hostnames must not trigger an outbound request",
        )
    finally:
        inventory_path.write_bytes(original_inventory)


def test_device_inventory_rejects_hard_and_symbolic_links(server: NmcServerProcess) -> None:
    assert_true(server.inventory_path is not None, "integration server should expose its active device inventory")
    inventory_path = server.inventory_path
    saved_path = inventory_path.with_suffix(".saved")
    inventory_path.replace(saved_path)
    try:
        inventory_path.hardlink_to(saved_path)
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/inventory",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "hard-linked inventory must be rejected")
        assert_true(
            payload.get("error", {}).get("code") == "device_inventory_unavailable",
            "the registry loader must reject hard-linked files",
        )
        inventory_path.unlink()

        inventory_path.symlink_to(saved_path)
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/inventory",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "symbolic-link inventory must be rejected")
        assert_true(
            payload.get("error", {}).get("code") == "device_inventory_unavailable",
            "the registry loader must fail closed instead of following a symbolic link",
        )
    finally:
        if inventory_path.is_symlink():
            inventory_path.unlink()
        saved_path.replace(inventory_path)


def test_recovery_fails_closed_without_configured_cluster_identity(backend_base_url: str) -> None:
    server = NmcServerProcess(backend_base_url, cluster_id=None)
    try:
        server.start()
        status, payload = request_json(
            server.base_url,
            "GET",
            "/k8s/deployment/recovery-status?cluster_id=authz-test-cluster&namespace=gail&deployment=gail",
            token="continuum-observe-token",
        )
        assert_status(status, 200, "unconfigured cluster preflight remains read-only")
        blockers = payload.get("data", {}).get("blockers", [])
        assert_true(
            payload.get("data", {}).get("eligible") is False
            and any(item.get("code") == "cluster_identity_unconfigured" for item in blockers),
            "preflight must explain that recovery is blocked when Continuum has no cluster identity",
        )

        status, payload = request_json(
            server.base_url,
            "POST",
            "/k8s/deployment/restart",
            token="continuum-control-token",
            payload={
                "namespace": "gail",
                "cluster_id": "authz-test-cluster",
                "deployment": "gail",
                "request_id": "recovery-no-cluster-0001",
            },
        )
        assert_true(
            status == 503 and payload.get("success") is not True,
            "restart must fail closed before Kubernetes mutation without a configured cluster identity",
        )
    finally:
        server.stop()


def test_recovery_fails_closed_without_authenticated_kubeconfig(backend_base_url: str) -> None:
    server = NmcServerProcess(backend_base_url, load_kubeconfig=False)
    try:
        server.start()
        status, payload = request_json(
            server.base_url,
            "GET",
            "/k8s/deployment/recovery-status?cluster_id=authz-test-cluster&namespace=gail&deployment=gail",
            token="continuum-observe-token",
        )
        assert_status(status, 200, "fallback-client preflight remains read-only")
        blockers = payload.get("data", {}).get("blockers", [])
        assert_true(
            payload.get("data", {}).get("eligible") is False
            and any(item.get("code") == "kubeconfig_unavailable" for item in blockers),
            "recovery preflight must reject the unauthenticated direct-URL fallback",
        )

        status, payload = request_json(
            server.base_url,
            "POST",
            "/k8s/deployment/restart",
            token="continuum-control-token",
            payload={
                "namespace": "gail",
                "cluster_id": "authz-test-cluster",
                "deployment": "gail",
                "request_id": "recovery-no-kubeconfig-0001",
            },
        )
        assert_true(
            status == 503 and payload.get("success") is not True,
            "restart must refuse the unauthenticated direct-URL fallback",
        )
    finally:
        server.stop()


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
    status, payload = request_json(
        server.base_url,
        "GET",
        "/gail/trading/overview",
        token="gail-trading-observe-token",
    )
    assert_status(status, 200, "gail trading overview route allowed with observe access")
    assert_true(
        isinstance((payload.get("data") or {}).get("bridge"), dict),
        "Gail Trading overview should return an aggregated bridge payload with observe access",
    )
    recent_trades = ((payload.get("data") or {}).get("trades") or {}).get("recent") or []
    assert_true(len(recent_trades) == 1, "Gail Trading overview should expose recent trades")
    recent_trade = recent_trades[0]
    assert_true(recent_trade.get("timestamp") == 1_700_000_123, "recent trade timestamp should be canonical epoch seconds")
    assert_true(recent_trade.get("side") == "buy", "recent trade side should be canonicalized from Gail action")
    assert_true(recent_trade.get("action") == "buy", "recent trade action should remain available")
    assert_true(
        backend.count_requests("/v1/trading/status", "GET") == 1
        and backend.count_requests("/v1/trading/portfolio", "GET") == 1
        and backend.count_requests("/v1/trading/positions", "GET") == 1
        and backend.count_requests("/v1/trading/history", "GET") == 1
        and backend.count_requests("/v1/trading/logs", "GET") == 1
        and backend.count_requests("/v1/status/api-issues", "GET") == 1,
        "allowed Gail Trading overview requests should gather each upstream source exactly once",
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
    redfish_tmp_dir = tempfile.TemporaryDirectory(prefix="nmc-redfish-mock-")
    redfish = RedfishBmcMock(pathlib.Path(redfish_tmp_dir.name))
    redfish.start()
    try:
        server.start()
        test_auth_session_reflects_central_identity(server)
        test_auth_session_preserves_service_account_groups(server)
        test_auth_session_supports_static_admin_token(server)
        test_continuum_route_authorisation(server, backend)
        test_redfish_vendor_diagnostics(server, redfish)
        test_redfish_vendor_actions(backend.base_url, redfish)
        test_home_assistant_rejects_plain_http_hostnames(server, backend)
        test_device_inventory_rejects_hard_and_symbolic_links(server)
        test_tracey_route_authorisation(server, backend)
        test_aarnn_route_authorisation(server, backend)
        test_gail_trading_route_authorisation(server, backend)
        test_recovery_fails_closed_without_configured_cluster_identity(backend.base_url)
        test_recovery_fails_closed_without_authenticated_kubeconfig(backend.base_url)
    except AssertionError as exc:
        log_output = server.log_path.read_text(encoding="utf-8", errors="replace") if server.log_path.exists() else ""
        print(f"[server-authz-test] FAILED: {exc}", file=sys.stderr)
        if log_output:
            print("[server-authz-test] nmc_server log:", file=sys.stderr)
            print(log_output, file=sys.stderr)
        return 1
    finally:
        server.stop()
        redfish.stop()
        redfish_tmp_dir.cleanup()
        backend.stop()

    print(
        "[server-authz-test] OK: validated end-to-end Continuum device inventory, DHCP and HP iLO/Dell iDRAC/MegaRAC Redfish diagnostics, "
        "plus Tracey, Gail Trading and AARNN route authorisation against the real nmc_server process."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
