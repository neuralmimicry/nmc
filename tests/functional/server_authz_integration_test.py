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
import hashlib
import json
import os
import pathlib
import shlex
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from urllib.parse import parse_qs, urlsplit
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
        self.home_assistant_status = 200
        self.home_assistant_states = [
            {"entity_id": "sensor.authz_device", "state": "online", "last_changed": "2026-10-07T10:00:00Z"},
            {"entity_id": "person.operator", "state": "home"},
        ]
        self.provider_health_status: dict[str, int] = {}
        self.provider_health_sequences: dict[str, list[int]] = {}
        self.provider_policy_update_on_health: tuple[str, pathlib.Path, bytes] | None = None

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

    def set_provider_health_status(self, endpoint: str, status: int) -> None:
        with self._lock:
            self.provider_health_status[endpoint] = status

    def set_provider_health_sequence(self, endpoint: str, statuses: list[int]) -> None:
        with self._lock:
            self.provider_health_sequences[endpoint] = list(statuses)

    def set_provider_policy_update_on_health(
        self,
        endpoint: str,
        policy_path: pathlib.Path,
        contents: bytes,
    ) -> None:
        with self._lock:
            self.provider_policy_update_on_health = (endpoint, policy_path, contents)

    def clear_provider_policy_update_on_health(self) -> None:
        with self._lock:
            self.provider_policy_update_on_health = None

    def count_requests(self, path: str, method: str | None = None) -> int:
        wanted_path = path.strip()
        wanted_method = method.upper() if method else None
        with self._lock:
            return sum(
                1
                for record in self._records
                if record.path == wanted_path and (wanted_method is None or record.method == wanted_method)
            )

    def set_home_assistant_response(self, status: int, states: list[dict[str, object]]) -> None:
        with self._lock:
            self.home_assistant_status = status
            self.home_assistant_states = states

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
            with self._lock:
                status = self.home_assistant_status
                states = list(self.home_assistant_states)
            self._send_json(handler, status, states)
            return

        if path_only.startswith("/provider-health/"):
            policy_update: tuple[str, pathlib.Path, bytes] | None = None
            with self._lock:
                sequence = self.provider_health_sequences.get(path_only, [])
                status = sequence.pop(0) if sequence else self.provider_health_status.get(path_only, 200)
                update = self.provider_policy_update_on_health
                if update is not None and update[0] == path_only:
                    policy_update = update
                    self.provider_policy_update_on_health = None
            if policy_update is not None:
                _, policy_path, contents = policy_update
                policy_path.write_bytes(contents)
                policy_path.chmod(0o600)
            self._send_json(handler, status, {"healthy": status == 200})
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


class TuringPiBmcMock:
    """HTTPS mock for the Turing Pi BMC authentication, power and reset API."""

    def __init__(self, parent_dir: pathlib.Path) -> None:
        self._cert_path = parent_dir / "turingpi-test-cert.pem"
        self._key_path = parent_dir / "turingpi-test-key.pem"
        self._states = {1: True, 2: False, 3: True, 4: False}
        self._action_acknowledged = True
        self._records: list[tuple[str, str, dict[str, str]]] = []
        self._lock = threading.Lock()
        self._httpd: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-keyout", str(self._key_path), "-out", str(self._cert_path),
                "-subj", "/CN=Turing-Pi",
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
            raise RuntimeError("mock Turing Pi BMC is not started")
        host, port = self._httpd.server_address
        return f"https://{host}:{port}"

    @property
    def ca_file(self) -> pathlib.Path:
        return self._cert_path

    @property
    def spki_sha256(self) -> str:
        """Return the certificate public-key pin used by the HTTPS client."""
        public_key = subprocess.run(
            ["openssl", "x509", "-in", str(self._cert_path), "-pubkey", "-noout"],
            check=True,
            capture_output=True,
            timeout=10,
        ).stdout
        der_key = subprocess.run(
            ["openssl", "pkey", "-pubin", "-outform", "DER"],
            input=public_key,
            check=True,
            capture_output=True,
            timeout=10,
        ).stdout
        return hashlib.sha256(der_key).hexdigest()

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

    def set_action_acknowledged(self, acknowledged: bool) -> None:
        with self._lock:
            self._action_acknowledged = acknowledged

    def records(self) -> list[tuple[str, str, dict[str, str]]]:
        with self._lock:
            return [(method, path, dict(params)) for method, path, params in self._records]

    def _send_json(self, handler: BaseHTTPRequestHandler, status: int, payload: object) -> None:
        data = json.dumps(payload).encode("utf-8")
        handler.send_response(status)
        handler.send_header("Content-Type", "application/json")
        handler.send_header("Content-Length", str(len(data)))
        handler.end_headers()
        handler.wfile.write(data)

    def _handle(self, handler: BaseHTTPRequestHandler) -> None:
        parsed = urlsplit(handler.path)
        params = {key: values[-1] for key, values in parse_qs(parsed.query).items() if values}
        with self._lock:
            self._records.append((handler.command, parsed.path, params))

        if handler.command == "POST" and parsed.path == "/api/bmc/authenticate":
            body_length = int(handler.headers.get("Content-Length", "0"))
            request_body = json.loads(handler.rfile.read(body_length) or b"{}")
            if request_body != {"username": "test-bmc-user", "password": "test-bmc-password"}:
                self._send_json(handler, 401, {"error": "unauthorized"})
                return
            self._send_json(handler, 200, {"id": "turingpi-test-token"})
            return

        if (
            handler.command != "GET"
            or parsed.path != "/api/bmc"
            or handler.headers.get("Authorization") != "Bearer turingpi-test-token"
        ):
            self._send_json(handler, 401, {"error": "unauthorized"})
            return

        if params.get("opt") == "get" and params.get("type") == "power":
            with self._lock:
                state = {f"node{slot}": "1" if powered else "0" for slot, powered in self._states.items()}
            self._send_json(handler, 200, {"response": state})
            return

        if params.get("opt") != "set":
            self._send_json(handler, 400, {"error": "invalid_operation"})
            return

        if params.get("type") == "power":
            slot_name = next((key for key in params if key in {"node1", "node2", "node3", "node4"}), "")
            if not slot_name or params[slot_name] not in {"0", "1"}:
                self._send_json(handler, 400, {"error": "invalid_power_request"})
                return
            slot = int(slot_name[-1])
            with self._lock:
                self._states[slot] = params[slot_name] == "1"
                acknowledged = self._action_acknowledged
            self._send_json(handler, 200, {"success": acknowledged})
            return

        if params.get("type") == "reset" and params.get("node", "").isdigit():
            slot = int(params["node"]) + 1
            if slot not in self._states:
                self._send_json(handler, 400, {"error": "invalid_slot"})
                return
            with self._lock:
                acknowledged = self._action_acknowledged
            self._send_json(handler, 200, {"success": acknowledged})
            return

        self._send_json(handler, 400, {"error": "invalid_operation"})


class NmcServerProcess:
    def __init__(
        self,
        backend_base_url: str,
        cluster_id: str | None = "authz-test-cluster",
        load_kubeconfig: bool = True,
        device_power_control_enabled: bool = False,
        recovery_enabled: bool | str | None = None,
        provider_environment: dict[str, str] | None = None,
        device_management_environment: dict[str, str] | None = None,
    ) -> None:
        self._backend_base_url = backend_base_url
        self._cluster_id = cluster_id
        self._load_kubeconfig = load_kubeconfig
        self._device_power_control_enabled = device_power_control_enabled
        self._recovery_enabled = recovery_enabled
        self._provider_environment = dict(provider_environment or {})
        self._device_management_environment = dict(device_management_environment or {})
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
        env.pop("NMC_PROVIDER_DEPENDENCY_POLICY_PATH", None)
        env.pop("NMC_PROVIDER_HEALTH_TIMEOUT_SECONDS", None)
        env.update(self._provider_environment)
        env.update(self._device_management_environment)
        env.pop("NMC_DEVICE_POWER_CONTROL_ENABLED", None)
        if self._device_power_control_enabled:
            env["NMC_DEVICE_POWER_CONTROL_ENABLED"] = "true"
        env.pop("NMC_K8S_CLUSTER_ID", None)
        if self._cluster_id is not None:
            env["NMC_K8S_CLUSTER_ID"] = self._cluster_id
        env.pop("NMC_RECOVERY_ENABLED", None)
        if self._recovery_enabled is not None:
            env["NMC_RECOVERY_ENABLED"] = str(self._recovery_enabled).lower()
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
                        "mac_address": "aa:bb:cc:dd:ee:00",
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


def write_mock_ipmitool(home_dir: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
    """Create an isolated executable that models only the IPMI commands under test."""
    executable = home_dir / "mock-ipmitool"
    state_path = home_dir / "ipmi-power-state"
    calls_path = home_dir / "ipmi-command-log"
    state_path.write_text("on\n", encoding="utf-8")
    state_path.chmod(0o600)
    executable.write_text(
        f"""#!/bin/sh
set -eu
if [ "${{IPMI_PASSWORD:-}}" != "test-bmc-password" ]; then
  exit 90
fi
printf 'PASSWORD_PRESENT=1 %s\\n' "$*" >> {shlex.quote(str(calls_path))}
state_file={shlex.quote(str(state_path))}
current_state=$(cat "$state_file")
case "$*" in
  *"chassis power status")
    printf 'Chassis Power is %s\\n' "$current_state"
    ;;
  *"chassis status")
    printf 'System Power : %s\\n' "$current_state"
    ;;
  *"chassis power on")
    printf 'on\\n' > "$state_file"
    printf 'Chassis Power Control: Up/On\\n'
    ;;
  *"chassis power off")
    printf 'off\\n' > "$state_file"
    printf 'Chassis Power Control: Down/Off\\n'
    ;;
  *"chassis power reset"|*"chassis power cycle")
    printf 'on\\n' > "$state_file"
    printf 'Chassis Power Control: Reset\\n'
    ;;
  *"sensor list")
    printf 'CPU Temp | 40 degrees C | ok\\n'
    ;;
  *"sel list last 20")
    printf 'No entries\\n'
    ;;
  *)
    exit 64
    ;;
esac
""",
        encoding="utf-8",
    )
    executable.chmod(0o700)
    return executable, state_path, calls_path


def write_mock_aws_cli(home_dir: pathlib.Path) -> pathlib.Path:
    """Install a deterministic fake AWS CLI for provider adapter integration tests."""
    executable = home_dir / "mock-aws"
    executable.write_text(
        """#!/bin/sh
set -eu
if [ -n "${NMC_AUTH_TOKEN:-}" ]; then
  exit 90
fi
printf '%s\\n' "$*" >> "$HOME/aws-provider-calls.txt"
if [ "$1" = "sts" ] && [ "$2" = "get-caller-identity" ]; then
  printf '%s\\n' '{"Account":"123456789012","Arn":"arn:aws:iam::123456789012:role/nmc-test"}'
elif [ "$1" = "ec2" ] && [ "$2" = "describe-instances" ]; then
  if [ -f "$HOME/aws-created-instance" ]; then
    printf '%s\\n' '{"Reservations":[{"Instances":[{"InstanceId":"i-0123456789abcdef0","InstanceType":"t3.small","State":{"Name":"running"},"Placement":{"AvailabilityZone":"eu-west-2a"},"PrivateIpAddress":"10.10.0.7","Tags":[{"Key":"Name","Value":"authz-cloud-vm"}]},{"InstanceId":"i-0abcdef1234567890","InstanceType":"t3.small","State":{"Name":"running"},"Placement":{"AvailabilityZone":"eu-west-2a"},"PrivateIpAddress":"10.10.0.8","Tags":[{"Key":"Name","Value":"authz-created-vm"}]}]}]}'
  else
    printf '%s\\n' '{"Reservations":[{"Instances":[{"InstanceId":"i-0123456789abcdef0","InstanceType":"t3.small","State":{"Name":"running"},"Placement":{"AvailabilityZone":"eu-west-2a"},"PrivateIpAddress":"10.10.0.7","Tags":[{"Key":"Name","Value":"authz-cloud-vm"}]}]}]}'
  fi
elif [ "$1" = "ec2" ] && [ "$2" = "reboot-instances" ]; then
  if [ -f "$HOME/delay-provider-operations" ]; then
    printf '%s\\n' "$*" >> "$HOME/provider-cli-sleepers.txt"
    while [ -f "$HOME/delay-provider-operations" ]; do sleep 0.05; done
  fi
  printf '%s\\n' '{"RebootingInstances":[]}'
elif [ "$1" = "ec2" ] && [ "$2" = "run-instances" ]; then
  touch "$HOME/aws-created-instance"
  printf '%s\\n' '{"Instances":[{"InstanceId":"i-0abcdef1234567890","InstanceType":"t3.small","State":{"Name":"pending"},"Placement":{"AvailabilityZone":"eu-west-2a"},"Tags":[{"Key":"Name","Value":"authz-created-vm"}]}]}'
else
  exit 64
fi
""",
        encoding="utf-8",
    )
    executable.chmod(0o700)
    return executable


def write_mock_provider_cli(home_dir: pathlib.Path, name: str, body: str) -> pathlib.Path:
    """Write a fixture-only provider command executable with no real cloud access."""
    executable = home_dir / f"mock-{name}"
    executable.write_text(body, encoding="utf-8")
    executable.chmod(0o700)
    return executable


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
    payload: Any | None = None,
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


def wait_for_provider_job(server: NmcServerProcess, job_id: str, *, token: str) -> dict[str, Any]:
    deadline = time.monotonic() + 20
    latest: dict[str, Any] = {}
    while time.monotonic() < deadline:
        status, payload = request_json(
            server.base_url,
            "GET",
            f"/providers/compute/jobs/{job_id}",
            token=token,
        )
        assert_status(status, 200, f"provider job {job_id} status")
        latest = payload.get("data", {})
        if latest.get("status") in {"succeeded", "failed", "cancelled", "outcome_unknown"}:
            return latest
        time.sleep(0.05)
    raise AssertionError(f"provider job {job_id} did not reach a terminal state: {latest!r}")


def submit_provider_job(
    server: NmcServerProcess,
    method: str,
    path: str,
    *,
    token: str,
    payload: Any | None = None,
    label: str,
) -> dict[str, Any]:
    status, response = request_json(server.base_url, method, path, token=token, payload=payload)
    assert_status(status, 202, label)
    job = response.get("data", {})
    job_id = job.get("job_id")
    assert_true(
        isinstance(job_id, str) and len(job_id) == 32 and job.get("status_url") == f"/providers/compute/jobs/{job_id}",
        f"{label} should return a pollable job resource; response was {response!r}",
    )
    return job


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


def test_controller_action_rejects_malformed_body_shapes(server: NmcServerProcess) -> None:
    """Malformed JSON values and fields must return structured HTTP 400 errors."""
    for body in ([], 7, {"controller_id": 7}):
        status, payload = request_json(
            server.base_url,
            "POST",
            "/devices/controllers/actions",
            token="continuum-control-token",
            payload=body,
        )
        assert_status(status, 400, "controller action malformed body")
        assert_true(
            (payload.get("error") or {}).get("code") in {"controller_action_invalid", "controller_id_invalid"},
            "malformed controller action bodies must return a structured client error",
        )


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
    assert_true(
        inventory["controllers"][0].get("mac_address") == "aa:bb:cc:dd:ee:00",
        "inventory should expose the registered controller MAC",
    )
    assert_true("AUTHZ_BMC_PASSWORD" not in json.dumps(inventory), "inventory must not reveal credential references")

    status, payload = request_json(server.base_url, "GET", "/devices/home-assistant", token="continuum-observe-token")
    assert_status(status, 200, "Home Assistant supplemental device data")
    entities = payload.get("data", {}).get("entities", [])
    assert_true([item.get("entity_id") for item in entities] == ["sensor.authz_device"], "Home Assistant results must be limited to explicitly allowlisted device entities")
    assert_true(all(item.get("entity_id") != "person.operator" for item in entities), "unrelated Home Assistant personal entities must not be exposed")

    status, payload = request_json(
        server.base_url, "GET", "/devices/home-assistant/reconciliation", token="continuum-observe-token"
    )
    assert_status(status, 200, "read-only Home Assistant inventory reconciliation")
    ha_reconciliation = payload.get("data", {})
    assert_true(ha_reconciliation.get("inventory_revision") == "authz-device-inventory-v1",
                "Home Assistant reconciliation must identify the inventory snapshot it used")
    assert_true(ha_reconciliation.get("summary", {}).get("matched") == 1,
                "the exact allowlisted entity mapping should reconcile to one device")
    assert_true(ha_reconciliation.get("devices", [])[0].get("device_id") == "authz-device"
                and ha_reconciliation.get("devices", [])[0].get("status") == "matched"
                and ha_reconciliation.get("devices", [])[0].get("state") == "online",
                "a matched HA state must be associated only by the configured exact entity id")
    assert_true(not ha_reconciliation.get("unmapped_entities"),
                "personal or otherwise unallowlisted Home Assistant entities must not enter reconciliation")

    status, _ = request_json(server.base_url, "GET", "/devices/home-assistant/reconciliation")
    assert_status(status, 401, "Home Assistant reconciliation must reject unauthenticated requests")

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
    assert_true(
        payload.get("data", {}).get("recovery_enabled") is False
        and any(item.get("code") == "recovery_disabled" for item in payload.get("data", {}).get("blockers", [])),
        "the default-off recovery policy must be visible in the read-only preflight",
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
        status == 403
        and payload.get("success") is not True
        and "disabled by policy" in payload.get("message", ""),
        f"a restart must be suppressed by the default-off recovery policy (HTTP {status})",
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


def test_provider_mutations_require_dependency_policy(
    backend: MockBackend,
    provider_environment: dict[str, str],
) -> None:
    """Prove a missing policy blocks a privileged cloud command before queueing."""
    environment = dict(provider_environment)
    environment.pop("NMC_PROVIDER_DEPENDENCY_POLICY_PATH", None)
    server = NmcServerProcess(backend.base_url, provider_environment=environment)
    try:
        server.start()
        status, payload = request_json(server.base_url, "GET", "/providers/compute", token="continuum-observe-token")
        assert_status(status, 200, "provider status exposes an absent dependency policy")
        assert_true(all(not item.get("dependency_preflight_configured") for item in payload.get("data", [])),
                    "provider status must show that dependency preflight is not configured")
        request = {
            "provider": "aws", "scope": "123456789012", "region": "eu-west-2",
            "instance_id": "i-0123456789abcdef0", "action": "restart",
            "request_id": "authz-provider-no-policy-01",
        }
        status, payload = request_json(
            server.base_url, "POST", "/providers/compute/instances/action",
            token="continuum-control-token", payload=request,
        )
        assert_status(status, 423, "provider mutation must fail closed without dependency policy")
        assert_true(payload.get("error", {}).get("dependency_preflight") == "not-configured"
                    or payload.get("message", "").find("not configured") >= 0,
                    "missing provider dependency policy must be explicit to the operator")
        calls = server._home_dir / "aws-provider-calls.txt"
        assert_true(not calls.exists(), "a missing dependency policy must prevent any provider CLI invocation")
    finally:
        server.stop()

    source_path = pathlib.Path(provider_environment["NMC_PROVIDER_DEPENDENCY_POLICY_PATH"])
    insecure_path = source_path.with_name("provider-dependency-policy-insecure.json")
    insecure_path.write_text(source_path.read_text(encoding="utf-8"), encoding="utf-8")
    insecure_path.chmod(0o644)
    insecure_environment = dict(provider_environment, NMC_PROVIDER_DEPENDENCY_POLICY_PATH=str(insecure_path))
    insecure_server = NmcServerProcess(backend.base_url, provider_environment=insecure_environment)
    try:
        insecure_server.start()
        status, payload = request_json(
            insecure_server.base_url, "POST", "/providers/compute/instances/action",
            token="continuum-control-token",
            payload={
                "provider": "aws", "scope": "123456789012", "region": "eu-west-2",
                "instance_id": "i-0123456789abcdef0", "action": "restart",
                "request_id": "authz-provider-insecure-policy-01",
            },
        )
        assert_status(status, 503, "a group/world-readable dependency policy must be rejected")
        assert_true("insecure" in payload.get("message", "").lower(),
                    "policy file ownership and mode failures should be visible without disclosing its path")
        assert_true(not (insecure_server._home_dir / "aws-provider-calls.txt").exists(),
                    "an insecure policy file must not allow provider CLI execution")
    finally:
        insecure_server.stop()
        insecure_path.unlink(missing_ok=True)


def test_provider_compute_lifecycle(server: NmcServerProcess, backend: MockBackend) -> None:
    """Exercise the three scoped adapters against executable-only provider fixtures."""
    status, _ = request_json(server.base_url, "GET", "/providers/compute")
    assert_status(status, 401, "provider adapter status requires authentication")

    status, payload = request_json(
        server.base_url, "GET", "/providers/compute", token="continuum-observe-token"
    )
    assert_status(status, 200, "provider adapter readiness")
    providers = payload.get("data", [])
    assert_true(
        {item.get("provider") for item in providers if item.get("cli_available")} == {"aws", "gcp", "azure"},
        "the configured AWS, GCP and Azure command fixtures should all be recognised",
    )
    assert_true(all(item.get("dependency_preflight_configured") is True and item.get("mutation_ready") is True for item in providers),
                "provider readiness must include the server-owned dependency policy")

    inventory_job = submit_provider_job(
        server,
        "GET",
        "/providers/compute/instances?provider=aws&scope=123456789012&region=eu-west-2",
        token="continuum-observe-token",
        label="AWS live inventory is accepted as a background job",
    )
    inventory_result = wait_for_provider_job(server, inventory_job["job_id"], token="continuum-observe-token")
    assert_true(inventory_result.get("status") == "succeeded", f"AWS inventory job failed: {inventory_result!r}")
    aws_instances = inventory_result.get("result", {}).get("data", {}).get("instances", [])
    assert_true(
        isinstance(aws_instances, list) and len(aws_instances) == 1,
        f"AWS inventory should contain the exact fixture instance; response was {payload!r}",
    )
    assert_true(
        aws_instances[0].get("id") == "i-0123456789abcdef0"
        and aws_instances[0].get("name") == "authz-cloud-vm"
        and aws_instances[0].get("state") == "running"
        and aws_instances[0].get("private_ip") == "10.10.0.7",
        "AWS inventory should be normalised from the provider response",
    )

    aws_call_log = server._home_dir / "aws-provider-calls.txt"
    call_count_before_scope_rejection = len(aws_call_log.read_text(encoding="utf-8").splitlines())
    status, _ = request_json(
        server.base_url,
        "GET",
        "/providers/compute/instances?provider=aws&scope=999999999999&region=eu-west-2",
        token="continuum-observe-token",
    )
    assert_status(status, 403, "provider scopes outside the server allow-list are rejected")
    assert_true(
        len(aws_call_log.read_text(encoding="utf-8").splitlines()) == call_count_before_scope_rejection,
        "a rejected provider scope must not launch a provider command",
    )

    restart_request = {
        "provider": "aws",
        "scope": "123456789012",
        "region": "eu-west-2",
        "instance_id": "i-0123456789abcdef0",
        "action": "restart",
        "request_id": "authz-provider-restart-0001",
    }
    call_count_before_observe_denial = len(aws_call_log.read_text(encoding="utf-8").splitlines())
    status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-observe-token",
        payload=restart_request,
    )
    assert_status(status, 403, "provider lifecycle actions require Continuum control access")
    assert_true(
        len(aws_call_log.read_text(encoding="utf-8").splitlines()) == call_count_before_observe_denial,
        "an unauthorised provider action must not launch a provider command",
    )

    calls_before_unhealthy_preflight = sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines())
    backend.set_provider_health_status("/provider-health/host-aws", 503)
    unhealthy_restart = dict(restart_request, request_id="authz-provider-host-unhealthy-01")
    unhealthy_job = submit_provider_job(
        server, "POST", "/providers/compute/instances/action", token="continuum-control-token",
        payload=unhealthy_restart, label="an unhealthy target host blocks AWS restart before provider mutation",
    )
    unhealthy_terminal = wait_for_provider_job(server, unhealthy_job["job_id"], token="continuum-control-token")
    assert_true(unhealthy_terminal.get("status") == "failed", "an unhealthy host must fail its provider preflight")
    unhealthy_data = unhealthy_terminal.get("result", {}).get("data", {})
    assert_true(unhealthy_data.get("dependency_preflight") == "blocked" and unhealthy_data.get("host_health") == "failed",
                "failed host health evidence must be explicit in the provider job result")
    assert_true(sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines()) == calls_before_unhealthy_preflight,
                "a failed host health gate must not launch the provider lifecycle command")
    backend.set_provider_health_status("/provider-health/host-aws", 200)

    policy_path = pathlib.Path(server._provider_environment["NMC_PROVIDER_DEPENDENCY_POLICY_PATH"])
    original_policy = policy_path.read_bytes()
    try:
        changed_policy = json.loads(original_policy)
        changed_policy["resources"][0]["host_health"]["id"] = "host-aws-changed-during-preflight"
        backend.set_provider_policy_update_on_health(
            "/provider-health/database", policy_path, json.dumps(changed_policy).encode("utf-8")
        )
        calls_before_policy_race = sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines())
        policy_race_job = submit_provider_job(
            server, "POST", "/providers/compute/instances/action", token="continuum-control-token",
            payload=dict(restart_request, request_id="authz-provider-policy-race-01"),
            label="a policy edit during live preflight invalidates the queued restart",
        )
        policy_race_terminal = wait_for_provider_job(server, policy_race_job["job_id"], token="continuum-control-token")
        assert_true(policy_race_terminal.get("status") == "failed",
                    "a policy edit during dependency probes must fail the provider job")
        assert_true(policy_race_terminal.get("result", {}).get("data", {}).get("dependency_preflight") == "stale",
                    "a policy edit during dependency probes must be reported as a stale binding")
        assert_true(sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines()) == calls_before_policy_race,
                    "a policy edit during dependency probes must prevent the provider restart")
    finally:
        backend.clear_provider_policy_update_on_health()
        policy_path.write_bytes(original_policy)
        policy_path.chmod(0o600)

    try:
        policy = json.loads(original_policy)
        policy["resources"][0]["host_health"]["url"] = backend.base_url.replace("http://", "HTTPS://", 1) + "/provider-health/host-aws"
        policy_path.write_text(json.dumps(policy), encoding="utf-8")
        policy_path.chmod(0o600)
        calls_before_uppercase_https = sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines())
        uppercase_https_job = submit_provider_job(
            server, "POST", "/providers/compute/instances/action", token="continuum-control-token",
            payload=dict(restart_request, request_id="authz-provider-uppercase-https-01"),
            label="uppercase HTTPS health probes must retain certificate validation",
        )
        uppercase_https_terminal = wait_for_provider_job(server, uppercase_https_job["job_id"], token="continuum-control-token")
        assert_true(uppercase_https_terminal.get("status") == "failed",
                    "uppercase HTTPS to a plaintext test endpoint must fail its TLS health check")
        assert_true(sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines()) == calls_before_uppercase_https,
                    "an uppercase HTTPS probe must not downgrade to plaintext and launch the restart")
    finally:
        policy_path.write_bytes(original_policy)
        policy_path.chmod(0o600)

    calls_before_unhealthy_service = sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines())
    backend.set_provider_health_status("/provider-health/service-aws", 503)
    unhealthy_service_restart = dict(restart_request, request_id="authz-provider-service-down-01")
    unhealthy_service_job = submit_provider_job(
        server, "POST", "/providers/compute/instances/action", token="continuum-control-token",
        payload=unhealthy_service_restart, label="an unhealthy dependent service blocks AWS restart before provider mutation",
    )
    unhealthy_service_terminal = wait_for_provider_job(server, unhealthy_service_job["job_id"], token="continuum-control-token")
    assert_true(unhealthy_service_terminal.get("status") == "failed", "an unhealthy dependent service must fail its preflight")
    unhealthy_service_data = unhealthy_service_terminal.get("result", {}).get("data", {})
    assert_true(unhealthy_service_data.get("dependency_preflight") == "blocked"
                and unhealthy_service_data.get("dependent_service_health") == "failed",
                "failed dependent-service health evidence must be explicit")
    assert_true(sum("ec2 reboot-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines()) == calls_before_unhealthy_service,
                "an unhealthy dependent service must prevent the provider command")
    backend.set_provider_health_status("/provider-health/service-aws", 200)

    restart_job = submit_provider_job(
        server,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=restart_request,
        label="approved AWS restart is accepted as a background job",
    )
    restart_terminal = wait_for_provider_job(server, restart_job["job_id"], token="continuum-control-token")
    assert_true(restart_terminal.get("status") == "succeeded", f"AWS restart job failed: {restart_terminal!r}")
    action_result = restart_terminal.get("result", {}).get("data", {})
    assert_true(action_result.get("provider_state_verified") is True,
                "a successful provider action must report its provider-state verification")
    assert_true(action_result.get("host_health") == "healthy"
                and action_result.get("dependent_service_health") == "healthy"
                and action_result.get("dependency_preflight") == "passed",
                "a successful provider restart must include live dependency, host and dependent-service health")
    assert_true("ec2 reboot-instances" in aws_call_log.read_text(encoding="utf-8"),
                "AWS restart must invoke the provider CLI operation")

    calls_before_blocked_stop = sum("ec2 stop-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines())
    status, payload = request_json(
        server.base_url, "POST", "/providers/compute/instances/action", token="continuum-control-token",
        payload=dict(restart_request, action="stop", request_id="authz-provider-stop-dependency-01"),
    )
    assert_status(status, 409, "stopping a host with registered dependent services requires an approved maintenance plan")
    assert_true(sum("ec2 stop-instances" in line for line in aws_call_log.read_text(encoding="utf-8").splitlines()) == calls_before_blocked_stop,
                "a blocked stop must not invoke the provider CLI")

    backend.set_provider_health_status("/provider-health/service-aws", 503)
    backend.set_provider_health_sequence("/provider-health/service-aws", [200])
    service_recovery_job = submit_provider_job(
        server, "POST", "/providers/compute/instances/action", token="continuum-control-token",
        payload=dict(restart_request, request_id="authz-provider-post-service-failure-01"),
        label="provider restart is not successful while a dependent service fails to recover",
    )
    service_recovery_terminal = wait_for_provider_job(server, service_recovery_job["job_id"], token="continuum-control-token")
    assert_true(service_recovery_terminal.get("status") == "failed",
                "a provider-state success with an unhealthy dependent service must remain a failed job")
    service_recovery_data = service_recovery_terminal.get("result", {}).get("data", {})
    assert_true(service_recovery_data.get("provider_state_verified") is True
                and service_recovery_data.get("host_health") == "healthy"
                and service_recovery_data.get("dependent_service_health") == "failed",
                "the terminal result must distinguish verified provider state from failed service recovery")
    backend.set_provider_health_status("/provider-health/service-aws", 200)
    backend.set_provider_health_sequence("/provider-health/service-aws", [])

    aws_call_count_after_restart = len(aws_call_log.read_text(encoding="utf-8").splitlines())
    replay = submit_provider_job(
        server,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=restart_request,
        label="replaying an AWS request returns its existing job",
    )
    assert_true(replay.get("job_id") == restart_job.get("job_id"),
                "an idempotent retry must return the original provider job")
    assert_true(len(aws_call_log.read_text(encoding="utf-8").splitlines()) == aws_call_count_after_restart,
                "an idempotent retry must not invoke AWS a second time")

    case_normalised_replay = submit_provider_job(
        server,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=dict(restart_request, provider="AWS", action="RESTART"),
        label="case-normalised AWS retry returns its existing job",
    )
    assert_true(case_normalised_replay.get("job_id") == restart_job.get("job_id"),
                "provider and action casing must not bypass mutation idempotency")
    assert_true(len(aws_call_log.read_text(encoding="utf-8").splitlines()) == aws_call_count_after_restart,
                "case-normalised retries must not invoke AWS a second time")

    original_policy = policy_path.read_bytes()
    try:
        policy = json.loads(original_policy)
        policy["resources"][0]["host_health"]["id"] = "host-aws-revised-without-revision-bump"
        policy_path.write_text(json.dumps(policy), encoding="utf-8")
        policy_path.chmod(0o600)
        status, unchanged_revision_replay = request_json(
            server.base_url,
            "POST",
            "/providers/compute/instances/action",
            token="continuum-control-token",
            payload=restart_request,
        )
        assert_status(status, 409, "an unchanged revision label cannot conceal changed policy contents")
        assert_true(unchanged_revision_replay.get("success") is not True,
                    "changed policy contents must invalidate an idempotent replay even when the revision label is unchanged")
        assert_true(len(aws_call_log.read_text(encoding="utf-8").splitlines()) == aws_call_count_after_restart,
                    "a policy-content mismatch must not repeat the provider mutation")
    finally:
        policy_path.write_bytes(original_policy)
        policy_path.chmod(0o600)

    conflicting_restart = dict(restart_request, instance_id="i-1123456789abcdef0")
    status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=conflicting_restart,
    )
    assert_status(status, 409, "reusing an AWS idempotency key for different work is rejected")
    malformed_idempotency = dict(restart_request, request_id=123)
    status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=malformed_idempotency,
    )
    assert_status(status, 400, "non-string provider idempotency fields are rejected")
    calls_before_oversized_request = len(aws_call_log.read_text(encoding="utf-8").splitlines())
    status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=dict(restart_request, note="x" * (256 * 1024)),
    )
    assert_status(status, 413, "provider operation payloads above 256 KiB are rejected")
    assert_true(len(aws_call_log.read_text(encoding="utf-8").splitlines()) == calls_before_oversized_request,
                "an oversized provider operation must not launch a provider command")

    state_path = server.inventory_path.parent / "provider-compute-jobs.json"
    assert_true(state_path.exists(), "provider job state should be persisted beside the device inventory")
    assert_true(state_path.stat().st_mode & 0o777 == 0o600,
                "provider job history should be owner-readable only")
    durable_state = json.loads(state_path.read_text(encoding="utf-8"))
    assert_true(any(item.get("job_id") == restart_job["job_id"] for item in durable_state.get("jobs", [])),
                "completed provider jobs should survive process restarts")

    reload_environment = dict(server._provider_environment)
    reload_environment["NMC_PROVIDER_JOB_STATE_PATH"] = str(state_path)
    restarted_server = NmcServerProcess(server._backend_base_url, provider_environment=reload_environment)
    try:
        restarted_server.start()
        reloaded_job = wait_for_provider_job(
            restarted_server,
            restart_job["job_id"],
            token="continuum-observe-token",
        )
        assert_true(reloaded_job.get("status") == "succeeded",
                    "a completed provider job should remain inspectable after a server restart")
    finally:
        restarted_server.stop()

    interrupted_job_id = "c" * 32
    interrupted_state_path = server.inventory_path.parent / "provider-compute-interrupted.json"
    interrupted_state_path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "updated_at_ms": int(time.time() * 1000),
                "jobs": [
                    {
                        "job_id": interrupted_job_id,
                        "operation": "action",
                        "provider": "aws",
                        "scope": "123456789012",
                        "idempotency_key": "authz-provider-interrupted-01",
                        "request_fingerprint": "0" * 64,
                        "status": "running",
                        "created_at_ms": int(time.time() * 1000),
                        "started_at_ms": int(time.time() * 1000),
                        "completed_at_ms": 0,
                        "result": {},
                    }
                ],
                "idempotency": [],
            }
        ),
        encoding="utf-8",
    )
    interrupted_state_path.chmod(0o600)
    recovery_environment = dict(server._provider_environment)
    recovery_environment["NMC_PROVIDER_JOB_STATE_PATH"] = str(interrupted_state_path)
    recovery_server = NmcServerProcess(server._backend_base_url, provider_environment=recovery_environment)
    try:
        recovery_server.start()
        recovered = wait_for_provider_job(
            recovery_server,
            interrupted_job_id,
            token="continuum-observe-token",
        )
        assert_true(recovered.get("status") == "outcome_unknown",
                    "a provider operation interrupted while running must be marked outcome-unknown, never replayed")
    finally:
        recovery_server.stop()

    call_count_before_delete_gate = len(aws_call_log.read_text(encoding="utf-8").splitlines())
    delete_request = dict(restart_request, action="delete", request_id="authz-provider-delete-0001")
    status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=delete_request,
    )
    assert_status(status, 423, "provider deletion requires its separate destructive-action gate")
    assert_true(
        "terminate-instances" not in aws_call_log.read_text(encoding="utf-8")
        and len(aws_call_log.read_text(encoding="utf-8").splitlines()) == call_count_before_delete_gate,
        "a disabled delete must not reach AWS",
    )

    created_spec = {
        "name": "authz-created-vm",
        "region": "eu-west-2",
        "health_profile": "authz-create-profile",
        "image_id": "ami-01234567",
        "instance_type": "t3.small",
        "subnet_id": "subnet-01234567",
        "security_group_ids": ["sg-01234567"],
    }
    create_job = submit_provider_job(
        server,
        "POST",
        "/providers/compute/instances/create",
        token="continuum-control-token",
        payload={
            "provider": "aws",
            "scope": "123456789012",
            "spec": created_spec,
            "idempotency_key": "authz-provider-create-0001",
        },
        label="AWS create is accepted as a background job",
    )
    create_terminal = wait_for_provider_job(server, create_job["job_id"], token="continuum-control-token")
    assert_true(create_terminal.get("status") == "succeeded", f"AWS create job failed: {create_terminal!r}")
    create_result = create_terminal.get("result", {}).get("data", {})
    assert_true(create_result.get("provider_state_verified") is True
                and create_result.get("host_health") == "healthy"
                and create_result.get("dependent_service_health") == "healthy"
                and create_result.get("dependency_preflight") == "passed",
                "provider creation must finish only after provider state and configured health checks pass")
    assert_true("ec2 run-instances" in aws_call_log.read_text(encoding="utf-8"),
                "AWS create must invoke the provider CLI operation")

    gcp_scope = "demo-project-123"
    gcp_inventory_job = submit_provider_job(
        server,
        "GET",
        f"/providers/compute/instances?provider=gcp&scope={gcp_scope}&region=europe-west2",
        token="continuum-observe-token",
        label="GCP inventory is accepted as a background job",
    )
    gcp_inventory = wait_for_provider_job(server, gcp_inventory_job["job_id"], token="continuum-observe-token")
    assert_true(gcp_inventory.get("status") == "succeeded", f"GCP inventory job failed: {gcp_inventory!r}")
    gcp_instances = gcp_inventory.get("result", {}).get("data", {}).get("instances", [])
    assert_true(len(gcp_instances) == 1 and gcp_instances[0].get("id") == "worker-01"
                and gcp_instances[0].get("zone") == "europe-west2-a",
                "GCP inventory should map project instances and zones")
    gcp_action_job = submit_provider_job(
        server,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload={
            "provider": "gcp",
            "scope": gcp_scope,
            "zone": "europe-west2-a",
            "instance_id": "worker-01",
            "action": "restart",
            "request_id": "authz-provider-gcp-restart-01",
        },
        label="GCP restart is accepted as a background job",
    )
    gcp_action = wait_for_provider_job(server, gcp_action_job["job_id"], token="continuum-control-token")
    assert_true(gcp_action.get("status") == "succeeded", f"GCP action job failed: {gcp_action!r}")
    assert_true(gcp_action.get("result", {}).get("data", {}).get("provider_state_verified") is True,
                "GCP lifecycle completion must include a provider-state check")

    azure_scope = "00000000-0000-0000-0000-000000000000"
    azure_id = f"/subscriptions/{azure_scope}/resourceGroups/rg-test/providers/Microsoft.Compute/virtualMachines/worker-az"
    azure_inventory_job = submit_provider_job(
        server,
        "GET",
        f"/providers/compute/instances?provider=azure&scope={azure_scope}&region=uksouth",
        token="continuum-observe-token",
        label="Azure inventory is accepted as a background job",
    )
    azure_inventory = wait_for_provider_job(server, azure_inventory_job["job_id"], token="continuum-observe-token")
    assert_true(azure_inventory.get("status") == "succeeded", f"Azure inventory job failed: {azure_inventory!r}")
    azure_instances = azure_inventory.get("result", {}).get("data", {}).get("instances", [])
    assert_true(len(azure_instances) == 1 and azure_instances[0].get("id") == azure_id,
                "Azure inventory should preserve the canonical resource identifier")
    azure_action_job = submit_provider_job(
        server,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload={
            "provider": "azure",
            "scope": azure_scope,
            "region": "uksouth",
            "instance_id": azure_id,
            "action": "restart",
            "request_id": "authz-provider-azure-restart-01",
        },
        label="Azure restart is accepted as a background job",
    )
    azure_action = wait_for_provider_job(server, azure_action_job["job_id"], token="continuum-control-token")
    assert_true(azure_action.get("status") == "succeeded", f"Azure action job failed: {azure_action!r}")
    assert_true(azure_action.get("result", {}).get("data", {}).get("provider_state_verified") is True,
                "Azure lifecycle completion must include a provider-state check")

    delay_flag = server._home_dir / "delay-provider-operations"
    sleepers_path = server._home_dir / "provider-cli-sleepers.txt"
    delay_flag.write_text("hold provider workers while checking queue bounds\n", encoding="utf-8")
    active_jobs: list[dict[str, Any]] = []
    for index in range(2):
        active_jobs.append(
            submit_provider_job(
                server,
                "POST",
                "/providers/compute/instances/action",
                token="continuum-control-token",
                payload=dict(restart_request, request_id=f"authz-provider-active-{index:04d}"),
                label=f"slow AWS worker {index} is accepted promptly",
            )
        )

    # Each worker must verify provider identity and inventory before reaching
    # the held lifecycle command; allow the slower self-hosted CI runner time
    # to start both process trees before concluding that execution is serial.
    sleep_deadline = time.monotonic() + 20
    while time.monotonic() < sleep_deadline:
        if sleepers_path.exists() and len(sleepers_path.read_text(encoding="utf-8").splitlines()) >= 2:
            break
        time.sleep(0.02)
    sleeper_count = len(sleepers_path.read_text(encoding="utf-8").splitlines()) if sleepers_path.exists() else 0
    assert_true(
        sleeper_count >= 2,
        "both provider workers should be occupied by the slow fixture; "
        f"delay_flag={delay_flag.exists()}, sleepers={sleeper_count}, "
        f"provider_calls={aws_call_log.read_text(encoding='utf-8').splitlines()[-12:]!r}",
    )
    for job in active_jobs:
        active_http_status, active_payload = request_json(
            server.base_url,
            "GET",
            f"/providers/compute/jobs/{job['job_id']}",
            token="continuum-control-token",
        )
        assert_status(active_http_status, 200, "active provider operation remains inspectable")
        active_status = active_payload.get("data", {})
        assert_true(active_status.get("status") == "running",
                    f"accepted provider worker entered an unexpected state: {active_status!r}")

    health_status, health_payload = request_json(server.base_url, "GET", "/health")
    assert_status(health_status, 200, "health stays responsive while provider workers are occupied")
    assert_true(health_payload.get("success") is True, "health response remains valid during provider work")

    queued_jobs: list[dict[str, Any]] = []
    for index in range(32):
        queued_jobs.append(
            submit_provider_job(
                server,
                "POST",
                "/providers/compute/instances/action",
                token="continuum-control-token",
                payload=dict(restart_request, request_id=f"authz-provider-queued-{index:04d}"),
                label=f"bounded provider queue item {index}",
            )
        )
    overflow = dict(restart_request, request_id="authz-provider-overflow-0001")
    overflow_status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=overflow,
    )
    assert_status(overflow_status, 429, "provider queue rejects work beyond its configured bound")

    delay_flag.unlink()
    for job in active_jobs + queued_jobs:
        terminal = wait_for_provider_job(server, job["job_id"], token="continuum-control-token")
        assert_true(terminal.get("status") == "succeeded",
                    f"accepted bounded-queue operation should finish successfully: {terminal!r}")

    status, _ = request_json(
        server.base_url,
        "POST",
        "/providers/compute/instances/action",
        token="continuum-control-token",
        payload=dict(restart_request, instance_id="i-0123456789abcdef0;touch"),
    )
    assert_status(status, 400, "provider identifiers reject shell-style input")

    status, _ = request_json(
        server.base_url,
        "POST",
        "/vm/create",
        token="continuum-control-token",
        payload={"name": "legacy-fake-vm"},
    )
    assert_status(status, 410, "legacy in-memory VM create no longer reports provider success")


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


def build_controller_action_payload(
    action: str,
    *,
    request_id: str,
    change_id: str,
    expected_power_state: str,
) -> dict[str, Any]:
    return {
        "controller_id": "authz-bmc",
        "device_id": "authz-device",
        "action": action,
        "request_id": request_id,
        "change_id": change_id,
        "reason": "Exercise the isolated controller protocol mock.",
        "expected_inventory_revision": "authz-device-inventory-v1",
        "preflight": {
            "observed_at_unix_ms": int(time.time() * 1000),
            "inventory_revision": "authz-device-inventory-v1",
            "dependency_graph_revision": "authz-dependency-graph-v1",
            "expected_power_state": expected_power_state,
            "target_identity_verified": True,
            "dependencies_healthy": True,
            "dependents_known": True,
            "affected_services_healthy": True,
            "monitoring_healthy": True,
            "affected_services": ["authz-test-service"],
            "dependency_ids": [],
        },
    }


def test_ipmi_diagnostics_and_actions(backend_base_url: str) -> None:
    mock_dir = tempfile.TemporaryDirectory(prefix="nmc-ipmi-mock-")
    mock_home = pathlib.Path(mock_dir.name)
    executable, state_path, calls_path = write_mock_ipmitool(mock_home)
    server = NmcServerProcess(
        backend_base_url,
        device_power_control_enabled=True,
        device_management_environment={"NMC_IPMITOOL_PATH": str(executable)},
    )
    try:
        server.start()
        assert_true(server.inventory_path is not None, "IPMI integration server should expose its inventory")
        inventory_path = server.inventory_path
        original_inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
        inventory = json.loads(json.dumps(original_inventory))
        inventory["controllers"][0].update(
            {"protocol": "ipmi", "endpoint": "127.0.0.1", "power_actions_enabled": True}
        )
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        inventory_path.chmod(0o600)

        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_true(status == 200,
                    f"IPMI mock diagnostics expected HTTP 200, got {status}: {payload.get('error')}")
        diagnostics = payload.get("data", {})
        assert_true("System Power : on" in diagnostics.get("chassis_status", ""),
                    "IPMI chassis state should be collected")
        assert_true(diagnostics.get("sensors", {}).get("available") is True,
                    "IPMI sensor diagnostics should be available")
        assert_true(diagnostics.get("system_event_log", {}).get("available") is True,
                    "IPMI SEL diagnostics should be available")

        actions = [
            ("warm_restart", "On", "ipmi-warm-restart-0001"),
            ("power_off", "On", "ipmi-power-off-0001"),
        ]
        for action_index, (action, expected_state, request_id) in enumerate(actions):
            action_request = build_controller_action_payload(
                action,
                request_id=request_id,
                change_id=f"IPMI-{request_id}",
                expected_power_state=expected_state,
            )
            action_status, action_payload = request_json(
                server.base_url,
                "POST",
                "/devices/controllers/actions",
                token="continuum-control-token",
                payload=action_request,
            )
            assert_status(action_status, 200, f"IPMI {action}")
            assert_true(action_payload.get("data", {}).get("verified") is True,
                        f"IPMI {action} must verify the resulting chassis state")
            if action_index == 0:
                calls_after_action = calls_path.read_text(encoding="utf-8").splitlines()
                replay_status, replay_payload = request_json(
                    server.base_url,
                    "POST",
                    "/devices/controllers/actions",
                    token="continuum-control-token",
                    payload=action_request,
                )
                assert_status(replay_status, 200, "IPMI exact idempotent replay")
                assert_true(replay_payload == action_payload,
                            "an exact controller-action retry should return the original response")
                assert_true(calls_path.read_text(encoding="utf-8").splitlines() == calls_after_action,
                            "an exact controller-action retry must not contact the BMC again")

                changed_intent = dict(action_request)
                changed_intent["action"] = "power_off"
                changed_status, changed_payload = request_json(
                    server.base_url,
                    "POST",
                    "/devices/controllers/actions",
                    token="continuum-control-token",
                    payload=changed_intent,
                )
                assert_status(changed_status, 409, "IPMI request id reused for changed intent")
                assert_true(changed_payload.get("error", {}).get("code") == "controller_action_idempotency_conflict",
                            "changed controller-action intent must return a clear idempotency conflict")
                assert_true(calls_path.read_text(encoding="utf-8").splitlines() == calls_after_action,
                            "changed intent with an existing request id must be rejected before contacting the BMC")

        state_after_power_off = state_path.read_text(encoding="utf-8").strip()
        assert_true(state_after_power_off == "off", "IPMI power-off should change only the mock chassis state")
        calls_before_mismatch = calls_path.read_text(encoding="utf-8").splitlines()
        mismatch_status, mismatch_payload = request_json(
            server.base_url,
            "POST",
            "/devices/controllers/actions",
            token="continuum-control-token",
            payload=build_controller_action_payload(
                "warm_restart",
                request_id="ipmi-state-mismatch-0001",
                change_id="IPMI-STATE-MISMATCH",
                expected_power_state="On",
            ),
        )
        assert_status(mismatch_status, 502, "IPMI actual state must match preflight evidence")
        assert_true(mismatch_payload.get("error", {}).get("code") == "controller_action_failed",
                    "a mismatched IPMI state must fail the action")
        calls_after_mismatch = calls_path.read_text(encoding="utf-8").splitlines()
        assert_true(len(calls_after_mismatch) == len(calls_before_mismatch) + 1,
                    "state mismatch should perform one status query and no IPMI mutation")

        for action, expected_state, request_id in [
            ("power_on", "Off", "ipmi-power-on-0001"),
            ("cold_restart", "On", "ipmi-cold-restart-0001"),
        ]:
            action_status, action_payload = request_json(
                server.base_url,
                "POST",
                "/devices/controllers/actions",
                token="continuum-control-token",
                payload=build_controller_action_payload(
                    action,
                    request_id=request_id,
                    change_id=f"IPMI-{request_id}",
                    expected_power_state=expected_state,
                ),
            )
            assert_status(action_status, 200, f"IPMI {action}")
            assert_true(action_payload.get("data", {}).get("verified") is True,
                        f"IPMI {action} must verify the resulting chassis state")

        recorded_calls = calls_path.read_text(encoding="utf-8").splitlines()
        assert_true(recorded_calls and all("PASSWORD_PRESENT=1" in line for line in recorded_calls),
                    "IPMI password must reach the child only through its environment")
        assert_true(all("-I lanplus -H 127.0.0.1 -U test-bmc-user -E" in line for line in recorded_calls),
                    "IPMI must use the fixed LANplus argument vector and exact registered target")
        assert_true("test-bmc-password" not in "\n".join(recorded_calls),
                    "IPMI password must never appear in child argv or diagnostic logs")
        assert_true(sum("chassis power reset" in line for line in recorded_calls) == 1,
                    "only the requested warm restart may issue an IPMI reset")
        assert_true(sum("chassis power off" in line for line in recorded_calls) == 1
                    and sum("chassis power on" in line for line in recorded_calls) == 1
                    and sum("chassis power cycle" in line for line in recorded_calls) == 1,
                    "IPMI power-on, power-off and cold-restart commands must map to fixed argv")
    finally:
        server.stop()
        mock_dir.cleanup()

    rejected_dir = tempfile.TemporaryDirectory(prefix="nmc-ipmi-symlink-")
    rejected_home = pathlib.Path(rejected_dir.name)
    trusted_executable, _, trusted_calls_path = write_mock_ipmitool(rejected_home)
    symlink_path = rejected_home / "untrusted-ipmitool"
    symlink_path.symlink_to(trusted_executable)
    rejected_server = NmcServerProcess(
        backend_base_url,
        device_management_environment={"NMC_IPMITOOL_PATH": str(symlink_path)},
    )
    try:
        rejected_server.start()
        assert_true(rejected_server.inventory_path is not None, "IPMI path-policy server should expose its inventory")
        inventory = json.loads(rejected_server.inventory_path.read_text(encoding="utf-8"))
        inventory["controllers"][0].update({"protocol": "ipmi", "endpoint": "127.0.0.1"})
        rejected_server.inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        rejected_server.inventory_path.chmod(0o600)
        status, payload = request_json(
            rejected_server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "IPMI executable symlink must fail closed")
        assert_true(payload.get("error", {}).get("code") == "ipmitool_unavailable",
                    "an unsafe configured IPMI executable should be reported unavailable")
        assert_true(not trusted_calls_path.exists() or not trusted_calls_path.read_text(encoding="utf-8").strip(),
                    "Continuum must not follow an NMC_IPMITOOL_PATH symlink")
    finally:
        rejected_server.stop()

    unsafe_parent = rejected_home / "world-writable-parent"
    unsafe_parent.mkdir(mode=0o700)
    unsafe_parent.chmod(0o777)
    unsafe_executable, _, unsafe_calls_path = write_mock_ipmitool(unsafe_parent)
    unsafe_server = NmcServerProcess(
        backend_base_url,
        device_management_environment={"NMC_IPMITOOL_PATH": str(unsafe_executable)},
    )
    try:
        unsafe_server.start()
        assert_true(unsafe_server.inventory_path is not None,
                    "IPMI directory-policy server should expose its inventory")
        inventory = json.loads(unsafe_server.inventory_path.read_text(encoding="utf-8"))
        inventory["controllers"][0].update({"protocol": "ipmi", "endpoint": "127.0.0.1"})
        unsafe_server.inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        unsafe_server.inventory_path.chmod(0o600)
        status, payload = request_json(
            unsafe_server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "IPMI executable under a writable directory must fail closed")
        assert_true(payload.get("error", {}).get("code") == "ipmitool_unavailable",
                    "an IPMI executable under an unsafe parent should be reported unavailable")
        assert_true(not unsafe_calls_path.exists() or not unsafe_calls_path.read_text(encoding="utf-8").strip(),
                    "Continuum must reject an IPMI executable under a writable directory")
    finally:
        unsafe_server.stop()
        unsafe_parent.chmod(0o700)
        rejected_dir.cleanup()


def test_turingpi_diagnostics_and_actions(backend_base_url: str) -> None:
    mock_dir = tempfile.TemporaryDirectory(prefix="nmc-turingpi-mock-")
    mock = TuringPiBmcMock(pathlib.Path(mock_dir.name))
    mock.start()
    server = NmcServerProcess(backend_base_url, device_power_control_enabled=True)
    try:
        server.start()
        assert_true(server.inventory_path is not None, "Turing Pi integration server should expose its inventory")
        inventory_path = server.inventory_path
        original_inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
        inventory = json.loads(json.dumps(original_inventory))
        inventory["devices"][0]["turingpi_slot"] = 3
        inventory["controllers"][0].update(
            {
                "protocol": "turingpi",
                "endpoint": mock.base_url,
                "tls_spki_sha256": mock.spki_sha256,
                "power_actions_enabled": True,
                "cold_restart_mode": "bmc_reset",
            }
        )
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        inventory_path.chmod(0o600)

        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(status, 200, "Turing Pi HTTPS diagnostics")
        nodes = payload.get("data", {}).get("nodes", {})
        assert_true(nodes == {"slot1": "On", "slot2": "Off", "slot3": "On", "slot4": "Off"},
                    "Turing Pi diagnostics should expose all four authenticated slot states")
        records_before_unsupported = mock.records()

        unsupported_status, unsupported_payload = request_json(
            server.base_url,
            "POST",
            "/devices/controllers/actions",
            token="continuum-control-token",
            payload=build_controller_action_payload(
                "warm_restart",
                request_id="turingpi-warm-restart-0001",
                change_id="TURINGPI-WARM-RESTART",
                expected_power_state="On",
            ),
        )
        assert_status(unsupported_status, 502, "Turing Pi must not invent warm-restart semantics")
        assert_true(unsupported_payload.get("error", {}).get("code") == "controller_action_failed",
                    "unsupported Turing Pi warm restart must fail explicitly")
        assert_true(mock.records() == records_before_unsupported,
                    "unsupported Turing Pi warm restart must not contact the controller")

        for action, expected_state, request_id in [
            ("power_off", "On", "turingpi-power-off-0001"),
            ("power_on", "Off", "turingpi-power-on-0001"),
            ("cold_restart", "On", "turingpi-cold-restart-0001"),
        ]:
            action_status, action_payload = request_json(
                server.base_url,
                "POST",
                "/devices/controllers/actions",
                token="continuum-control-token",
                payload=build_controller_action_payload(
                    action,
                    request_id=request_id,
                    change_id=f"TURINGPI-{request_id}",
                    expected_power_state=expected_state,
                ),
            )
            assert_status(action_status, 200, f"Turing Pi {action}")
            assert_true(action_payload.get("data", {}).get("verified") is True,
                        f"Turing Pi {action} must verify the targeted slot state")

        writes = [params for method, path, params in mock.records()
                  if method == "GET" and path == "/api/bmc" and params.get("opt") == "set"]
        assert_true(
            {tuple(sorted(params.items())) for params in writes} == {
                tuple(sorted({"opt": "set", "type": "power", "node3": "0"}.items())),
                tuple(sorted({"opt": "set", "type": "power", "node3": "1"}.items())),
                tuple(sorted({"opt": "set", "type": "reset", "node": "2"}.items())),
            },
            "Turing Pi actions must target only slot 3 with the exact supported API semantics",
        )

        mock.set_action_acknowledged(False)
        failed_status, failed_payload = request_json(
            server.base_url,
            "POST",
            "/devices/controllers/actions",
            token="continuum-control-token",
            payload=build_controller_action_payload(
                "power_off",
                request_id="turingpi-no-ack-0001",
                change_id="TURINGPI-NO-ACK",
                expected_power_state="On",
            ),
        )
        assert_status(failed_status, 502, "Turing Pi HTTP success without application acknowledgement")
        assert_true(failed_payload.get("error", {}).get("code") == "controller_action_failed",
                    "an unacknowledged Turing Pi operation must not be reported as successful")

        records_before_bad_pin = mock.records()
        inventory["controllers"][0]["tls_spki_sha256"] = "0" * 64
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        inventory_path.chmod(0o600)
        bad_pin_status, bad_pin_payload = request_json(
            server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(bad_pin_status, 502, "Turing Pi must reject an unrecognised public-key pin")
        assert_true(bad_pin_payload.get("error", {}).get("code") == "turingpi_authentication_failed",
                    "a TLS pin mismatch must fail before Turing Pi authentication")
        assert_true(mock.records() == records_before_bad_pin,
                    "a TLS pin mismatch must not send credentials or an API request to the controller")

        inventory["controllers"][0]["tls_spki_sha256"] = "not-a-sha256-pin"
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        inventory_path.chmod(0o600)
        malformed_pin_status, malformed_pin_payload = request_json(
            server.base_url,
            "GET",
            "/devices/controllers/diagnostics?controller_id=authz-bmc",
            token="continuum-observe-token",
        )
        assert_status(malformed_pin_status, 503, "Turing Pi must reject malformed public-key pins")
        assert_true(malformed_pin_payload.get("error", {}).get("code") == "device_inventory_unavailable",
                    "a malformed TLS pin must invalidate the controller inventory")
        assert_true(mock.records() == records_before_bad_pin,
                    "a malformed TLS pin must not send credentials or an API request to the controller")
    finally:
        server.stop()
        mock.stop()
        mock_dir.cleanup()


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


def test_home_assistant_reconciliation_reports_mapping_drift(
    server: NmcServerProcess,
    backend: MockBackend,
) -> None:
    assert_true(server.inventory_path is not None, "integration server should expose its active device inventory")
    inventory_path = server.inventory_path
    original_inventory = inventory_path.read_bytes()
    original_status = backend.home_assistant_status
    original_states = list(backend.home_assistant_states)
    try:
        unconfigured_inventory = json.loads(original_inventory)
        unconfigured_inventory.pop("home_assistant", None)
        inventory_path.write_text(json.dumps(unconfigured_inventory), encoding="utf-8")
        inventory_path.chmod(0o600)
        previous_ha_requests = backend.count_requests("/ha/api/states", "GET")
        status, payload = request_json(
            server.base_url, "GET", "/devices/home-assistant/reconciliation", token="continuum-observe-token"
        )
        assert_status(status, 200, "unconfigured Home Assistant reconciliation")
        unconfigured_data = payload.get("data", {})
        assert_true(unconfigured_data.get("configured") is False and unconfigured_data.get("available") is False,
                    "missing HA credentials/configuration must be reported explicitly")
        assert_true(unconfigured_data.get("devices", [])[0].get("status") == "not_configured",
                    "registered HA mappings without a configured source must not appear matched")
        assert_true(backend.count_requests("/ha/api/states", "GET") == previous_ha_requests,
                    "unconfigured HA reconciliation must not make an outbound request")

        inventory = json.loads(original_inventory)
        source_device = inventory["devices"][0]
        for device_id, entity_id in (
            ("authz-device-duplicate-map", "sensor.authz_device"),
            ("authz-device-missing-state", "sensor.missing_device"),
            ("authz-device-not-allowlisted", "sensor.not_allowlisted"),
        ):
            device = {
                "id": device_id,
                "kind": "home_device",
                "environment": "test",
                "criticality": "low",
                "addresses": [],
                "mac_addresses": [],
                "dhcp_client_ids": [],
                "services": [],
                "depends_on": [],
                "affected_services": [],
                "home_assistant_entity": entity_id,
            }
            inventory["devices"].append(device)
        inventory["home_assistant"]["entities"].extend(
            ["sensor.missing_device", "sensor.unmapped", "sensor.unmapped_duplicate"]
        )
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        inventory_path.chmod(0o600)
        backend.set_home_assistant_response(
            200,
            [
                {"entity_id": "sensor.authz_device", "state": "online"},
                {"entity_id": "sensor.orphan", "state": "private-data-must-not-leak"},
                {"entity_id": "sensor.unmapped", "state": "private-data-must-not-leak"},
                {"entity_id": "sensor.unmapped_duplicate", "state": "duplicate-private-data-must-not-leak"},
                {"entity_id": "sensor.unmapped_duplicate", "state": "duplicate-private-data-must-not-leak"},
                {"entity_id": "person.operator", "state": "home"},
            ],
        )

        status, payload = request_json(
            server.base_url, "GET", "/devices/home-assistant/reconciliation", token="continuum-observe-token"
        )
        assert_status(status, 200, "Home Assistant mapping-drift reconciliation")
        data = payload.get("data", {})
        statuses = {item.get("device_id"): item.get("status") for item in data.get("devices", [])}
        assert_true(statuses.get("authz-device") == "ambiguous"
                    and statuses.get("authz-device-duplicate-map") == "ambiguous",
                    "duplicate inventory mappings must be reported as ambiguous")
        assert_true(statuses.get("authz-device-missing-state") == "missing",
                    "an allowlisted entity with no current HA state must be reported missing")
        assert_true(statuses.get("authz-device-not-allowlisted") == "not_configured",
                    "an inventory mapping outside the explicit HA allowlist must be reported not_configured")
        assert_true(all("state" not in item for item in data.get("devices", [])
                        if item.get("status") == "ambiguous"),
                    "ambiguous mappings must not expose a state assigned to an uncertain device")
        assert_true(data.get("unmapped_entities") == [{"entity_id": "sensor.unmapped", "status": "unmapped"}],
                    "allowlisted but unregistered entities must be reported without their state")
        assert_true(data.get("ambiguous_entities") == [{"entity_id": "sensor.unmapped_duplicate", "status": "ambiguous"}],
                    "duplicated allowlisted but unregistered entities must be reported as ambiguous without state values")
        assert_true("private-data-must-not-leak" not in json.dumps(data)
                    and "duplicate-private-data-must-not-leak" not in json.dumps(data),
                    "unmapped Home Assistant states must not expose state values")

        backend.set_home_assistant_response(503, [])
        status, payload = request_json(
            server.base_url, "GET", "/devices/home-assistant/reconciliation", token="continuum-observe-token"
        )
        assert_status(status, 503, "unavailable Home Assistant reconciliation source")
        assert_true(payload.get("error", {}).get("code") == "home_assistant_unavailable",
                    "source outages must propagate as unavailable, never as an empty successful reconciliation")
    finally:
        inventory_path.write_bytes(original_inventory)
        backend.set_home_assistant_response(original_status, original_states)


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


def test_device_inventory_rejects_invalid_controller_mac(server: NmcServerProcess) -> None:
    """Controller identities must not be projected from malformed MAC values."""
    assert_true(server.inventory_path is not None, "integration server should expose its active device inventory")
    inventory_path = server.inventory_path
    original_inventory = inventory_path.read_bytes()
    inventory = json.loads(original_inventory)
    inventory["controllers"][0]["mac_address"] = "not-a-mac"
    try:
        inventory_path.write_text(json.dumps(inventory), encoding="utf-8")
        status, payload = request_json(
            server.base_url,
            "GET",
            "/devices/inventory",
            token="continuum-observe-token",
        )
        assert_status(status, 503, "invalid controller MAC must block inventory")
        assert_true(
            payload.get("error", {}).get("code") == "device_inventory_unavailable",
            "invalid controller MAC must fail closed",
        )
    finally:
        inventory_path.write_bytes(original_inventory)


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


def test_recovery_requires_an_explicit_valid_enable_value(backend_base_url: str) -> None:
    for configured_value, expected_enabled in (("true", True), ("yes", False)):
        server = NmcServerProcess(backend_base_url, recovery_enabled=configured_value)
        try:
            server.start()
            status, payload = request_json(
                server.base_url,
                "GET",
                "/k8s/deployment/recovery-status?cluster_id=authz-test-cluster&namespace=gail&deployment=gail",
                token="continuum-observe-token",
            )
            assert_status(status, 200, "read-only recovery policy preflight")
            data = payload.get("data", {})
            assert_true(
                data.get("recovery_enabled") is expected_enabled,
                f"NMC_RECOVERY_ENABLED={configured_value!r} should resolve to {expected_enabled}",
            )
            blockers = data.get("blockers", [])
            has_policy_blocker = any(item.get("code") == "recovery_disabled" for item in blockers)
            assert_true(
                has_policy_blocker is (not expected_enabled),
                "the read-only preflight should explain policy suppression when the recovery gate is closed",
            )
            restart_status, restart_payload = request_json(
                server.base_url,
                "POST",
                "/k8s/deployment/restart",
                token="continuum-control-token",
                payload={
                    "namespace": "recovery-authorization-test",
                    "cluster_id": "authz-test-cluster",
                    "deployment": "definitely-absent",
                    "request_id": "recovery-absent-target-0002",
                },
            )
            if expected_enabled:
                assert_true(
                    restart_status in {404, 409, 502, 503} and restart_payload.get("success") is not True,
                    "an enabled recovery policy must still reject an absent or unavailable Deployment",
                )
            else:
                assert_true(
                    restart_status == 403 and restart_payload.get("success") is not True,
                    "an invalid enable value must refuse restart before contacting Kubernetes",
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
    provider_tmp_dir = tempfile.TemporaryDirectory(prefix="nmc-provider-mocks-")
    provider_home = pathlib.Path(provider_tmp_dir.name)
    aws_cli = write_mock_aws_cli(provider_home)
    gcp_cli = write_mock_provider_cli(
        provider_home,
        "gcloud",
        """#!/bin/sh
set -eu
if [ -n "${NMC_AUTH_TOKEN:-}" ]; then exit 90; fi
if [ "$1" = "projects" ] && [ "$2" = "describe" ]; then
  printf '%s\\n' '{"projectId":"demo-project-123"}'
elif [ "$1" = "compute" ] && [ "$2" = "instances" ] && [ "$3" = "list" ]; then
  printf '%s\\n' '[{"name":"worker-01","machineType":"zones/europe-west2-a/machineTypes/e2-standard-2","zone":"https://compute.googleapis.com/compute/v1/projects/demo-project-123/zones/europe-west2-a","status":"RUNNING","networkInterfaces":[{"networkIP":"10.20.0.7"}]}]'
elif [ "$1" = "compute" ] && [ "$2" = "instances" ] && [ "$3" = "describe" ]; then
  printf '%s\\n' '{"name":"worker-01","machineType":"zones/europe-west2-a/machineTypes/e2-standard-2","zone":"https://compute.googleapis.com/compute/v1/projects/demo-project-123/zones/europe-west2-a","status":"RUNNING","networkInterfaces":[{"networkIP":"10.20.0.7"}]}'
elif [ "$1" = "compute" ] && [ "$2" = "instances" ] && [ "$3" = "reset" ]; then
  printf '%s\\n' '{}'
else
  exit 64
fi
""",
    )
    azure_cli = write_mock_provider_cli(
        provider_home,
        "az",
        """#!/bin/sh
set -eu
if [ -n "${NMC_AUTH_TOKEN:-}" ]; then exit 90; fi
if [ "$1" = "account" ] && [ "$2" = "show" ]; then
  printf '%s\\n' '{"id":"00000000-0000-0000-0000-000000000000","name":"NMC test"}'
elif [ "$1" = "vm" ] && [ "$2" = "list" ]; then
  printf '%s\\n' '[{"id":"/subscriptions/00000000-0000-0000-0000-000000000000/resourceGroups/rg-test/providers/Microsoft.Compute/virtualMachines/worker-az","name":"worker-az","location":"uksouth","hardwareProfile":{"vmSize":"Standard_B2s"},"powerState":"VM running","resourceGroup":"rg-test","privateIps":"10.30.0.7"}]'
elif [ "$1" = "vm" ] && [ "$2" = "restart" ]; then
  printf '%s\\n' '{"status":"Accepted"}'
else
  exit 64
fi
""",
    )
    health_probe = lambda name: {
        "id": name,
        "url": f"{backend.base_url}/provider-health/{name}",
        "expected_status": 200,
    }
    dependency_policy = {
        "schema_version": 1,
        "dependency_graph_revision": "authz-provider-graph-v1",
        "resources": [
            {
                "provider": "aws", "scope": "123456789012", "instance_id": "i-0123456789abcdef0",
                "location": "eu-west-2", "host_health": health_probe("host-aws"),
                "dependencies": [health_probe("database")],
                "dependent_services": [health_probe("service-aws")], "allowed_actions": ["restart", "stop"],
            },
            {
                "provider": "gcp", "scope": "demo-project-123", "instance_id": "worker-01",
                "location": "europe-west2-a", "host_health": health_probe("host-gcp"),
                "dependencies": [health_probe("database")],
                "dependent_services": [health_probe("service-gcp")], "allowed_actions": ["restart"],
            },
            {
                "provider": "azure", "scope": "00000000-0000-0000-0000-000000000000",
                "instance_id": "/subscriptions/00000000-0000-0000-0000-000000000000/resourceGroups/rg-test/providers/Microsoft.Compute/virtualMachines/worker-az",
                "location": "uksouth", "host_health": health_probe("host-azure"),
                "dependencies": [], "dependent_services": [health_probe("service-azure")],
                "allowed_actions": ["restart"],
            },
        ],
        "create_profiles": [
            {
                "provider": "aws", "scope": "123456789012", "profile_id": "authz-create-profile",
                "location": "eu-west-2", "host_health": health_probe("host-created"),
                "dependencies": [health_probe("database")],
                "dependent_services": [health_probe("service-created")],
            }
        ],
    }
    policy_path = provider_home / "provider-dependency-policy.json"
    policy_path.write_text(json.dumps(dependency_policy), encoding="utf-8")
    policy_path.chmod(0o600)
    provider_environment = {
        "NMC_PROVIDER_AWS_CLI_PATH": str(aws_cli),
        "NMC_PROVIDER_GCP_CLI_PATH": str(gcp_cli),
        "NMC_PROVIDER_AZURE_CLI_PATH": str(azure_cli),
        "NMC_AWS_ALLOWED_SCOPES": "123456789012",
        "NMC_GCP_ALLOWED_SCOPES": "demo-project-123",
        "NMC_AZURE_ALLOWED_SCOPES": "00000000-0000-0000-0000-000000000000",
        "NMC_PROVIDER_MUTATIONS_ENABLED": "true",
        "NMC_PROVIDER_ALLOW_DELETE": "false",
        "NMC_PROVIDER_DEPENDENCY_POLICY_PATH": str(policy_path),
        "NMC_PROVIDER_HEALTH_TIMEOUT_SECONDS": "2",
    }
    test_provider_mutations_require_dependency_policy(backend, provider_environment)
    server = NmcServerProcess(backend.base_url, provider_environment=provider_environment)
    redfish_tmp_dir = tempfile.TemporaryDirectory(prefix="nmc-redfish-mock-")
    redfish = RedfishBmcMock(pathlib.Path(redfish_tmp_dir.name))
    redfish.start()
    try:
        server.start()
        test_auth_session_reflects_central_identity(server)
        test_auth_session_preserves_service_account_groups(server)
        test_auth_session_supports_static_admin_token(server)
        test_continuum_route_authorisation(server, backend)
        test_provider_compute_lifecycle(server, backend)
        test_controller_action_rejects_malformed_body_shapes(server)
        test_redfish_vendor_diagnostics(server, redfish)
        test_redfish_vendor_actions(backend.base_url, redfish)
        test_ipmi_diagnostics_and_actions(backend.base_url)
        test_turingpi_diagnostics_and_actions(backend.base_url)
        test_home_assistant_reconciliation_reports_mapping_drift(server, backend)
        test_home_assistant_rejects_plain_http_hostnames(server, backend)
        test_device_inventory_rejects_hard_and_symbolic_links(server)
        test_tracey_route_authorisation(server, backend)
        test_aarnn_route_authorisation(server, backend)
        test_gail_trading_route_authorisation(server, backend)
        test_recovery_fails_closed_without_configured_cluster_identity(backend.base_url)
        test_recovery_fails_closed_without_authenticated_kubeconfig(backend.base_url)
        test_recovery_requires_an_explicit_valid_enable_value(backend.base_url)
    except AssertionError as exc:
        log_output = server.log_path.read_text(encoding="utf-8", errors="replace") if server.log_path.exists() else ""
        print(f"[server-authz-test] FAILED: {exc}", file=sys.stderr)
        if log_output:
            print("[server-authz-test] nmc_server log:", file=sys.stderr)
            print(log_output, file=sys.stderr)
        return 1
    finally:
        server.stop()
        provider_tmp_dir.cleanup()
        redfish.stop()
        redfish_tmp_dir.cleanup()
        backend.stop()

    print(
        "[server-authz-test] OK: validated end-to-end Continuum device inventory, DHCP, HP iLO/Dell iDRAC/MegaRAC Redfish, IPMI and Turing Pi diagnostics/actions, "
        "plus Tracey, Gail Trading and AARNN route authorisation against the real nmc_server process."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
