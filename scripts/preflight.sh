#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: preflight.sh [options]

Run the canonical NMC local/CI verification gate.

Options:
  --ci              CI mode; currently runs the full gate.
  --skip-server     Skip the server build and server authorisation integration test.
  -h, --help        Show this help text.
USAGE
}

log() {
  printf '%s\n' "$*"
}

run() {
  printf '+ '
  printf '%q ' "$@"
  printf '\n'
  "$@"
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

CI_MODE=0
SKIP_SERVER=0

while (($#)); do
  case "$1" in
    --ci)
      CI_MODE=1
      ;;
    --skip-server)
      SKIP_SERVER=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_VERSION="${NMC_VERSION:-$(bash "$REPO_ROOT/scripts/derive-version.sh" --build-version)}"
CLIENT_BUILD_DIR="$REPO_ROOT/nmc_client/build"
SERVER_BUILD_DIR="$REPO_ROOT/nmc_server/build"
CLIENT_BIN="$CLIENT_BUILD_DIR/nmc"
SERVER_BIN="$SERVER_BUILD_DIR/nmc_server"

cd "$REPO_ROOT"

log "validating shell scripts"
run bash -n \
  deploy.sh \
  scripts/derive-version.sh \
  scripts/install-common.sh \
  scripts/install-client.sh \
  scripts/install-server.sh \
  scripts/package-release.sh \
  scripts/preflight.sh

log "checking PowerShell packaging script for CI-hostile parameter names"
if grep -nE '^[[:space:]]*\[[^]]+\][[:space:]]*\$Input([,[:space:]]|$)' scripts/package-release.ps1; then
  die "scripts/package-release.ps1 must not declare a parameter named Input; it collides with PowerShell's automatic input variable"
fi

if command -v pwsh >/dev/null 2>&1; then
  log "validating PowerShell packaging script"
  run pwsh -NoLogo -NoProfile -Command \
    '$null = [System.Management.Automation.Language.Parser]::ParseFile("scripts/package-release.ps1", [ref]$null, [ref]$null)'
fi

log "running contract tests"
while IFS= read -r test_file; do
  run python3 "$test_file"
done < <(find tests/contracts -maxdepth 1 -type f -name '*_test.py' | sort)

log "configuring client"
run cmake -S nmc_client -B "$CLIENT_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNMC_CLIENT_VERSION="$BUILD_VERSION"

log "building client"
run cmake --build "$CLIENT_BUILD_DIR" --config Release --parallel

log "smoke testing client version surface"
run "$CLIENT_BIN" version --no-check

log "running functional client integration test"
run env NMC_BIN="$CLIENT_BIN" python3 tests/functional/client_cli_integration_test.py

if (( SKIP_SERVER )); then
  log "skipping server verification"
  exit 0
fi

log "configuring server"
run cmake -S nmc_server -B "$SERVER_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNMC_SERVER_VERSION="$BUILD_VERSION"

log "building server"
run cmake --build "$SERVER_BUILD_DIR" --config Release --parallel

log "running functional server authorisation integration test"
run env NMC_SERVER_BIN="$SERVER_BIN" python3 tests/functional/server_authz_integration_test.py

if (( CI_MODE )); then
  log "ci preflight completed"
else
  log "preflight completed"
fi
