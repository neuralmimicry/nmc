from __future__ import annotations

import pathlib
import sys
from typing import Iterable


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
CORE_DIR = REPO_ROOT / "nmc_server" / "src" / "Core"
PRIMARY_SERVER_FILE = CORE_DIR / "APIRoutes.cpp"
ROUTE_SOURCE_FILES = tuple(sorted(CORE_DIR.glob("APIRoutes*.cpp")))
ROUTE_SUPPORT_FILES = tuple(sorted(CORE_DIR.glob("APIRoutes*.inl")))
AGGREGATED_ROUTE_SOURCE_FILES = ROUTE_SOURCE_FILES + tuple(
    path for path in ROUTE_SUPPORT_FILES if path not in ROUTE_SOURCE_FILES
)


def read_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[contract-test] failed to read {path}: {exc}", file=sys.stderr)
        sys.exit(1)


def read_primary_server_source() -> str:
    return read_text(PRIMARY_SERVER_FILE)


def read_route_registration_source(files: Iterable[pathlib.Path] = AGGREGATED_ROUTE_SOURCE_FILES) -> str:
    sources = [read_text(path) for path in files]
    if not sources:
        print("[contract-test] no APIRoutes source files were detected", file=sys.stderr)
        sys.exit(1)
    return "\n".join(sources)
