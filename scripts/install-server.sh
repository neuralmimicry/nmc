#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/install-common.sh
source "$SCRIPT_DIR/install-common.sh"

usage() {
  cat <<'USAGE'
Usage: install-server.sh [options]

Install the NMC server Debian package and configure a systemd service on Linux.
The installer prefers local .deb files when available and otherwise downloads the
matching private GitHub release assets.

Options:
  --server-deb-file PATH      Local nmc-server .deb to install.
  --client-deb-file PATH      Local nmc-client .deb to install when client install is enabled.
  --release-version VERSION   Release version to install. Defaults to VERSION or latest release.
  --repo OWNER/NAME           GitHub repository. Default: neuralmimicry/nmc
  --run-user USER             Service user. Default: nmc
  --run-group GROUP           Service group. Default: same as run-user
  --service-name NAME         Systemd service name. Default: nmc-server
  --port PORT                 Listener port. Default: 8080
  --state-dir PATH            Runtime state directory. Default: /var/lib/nmc
  --env-file PATH             Environment file path. Default: /etc/nmc/nmc.env
  --kubeconfig PATH           Source kubeconfig to copy to /etc/nmc/kubeconfig
  --skip-client               Do not install the nmc client package.
  --skip-start                Install and enable the service but do not start it.
  --force-env-file            Overwrite an existing environment file.
  --dry-run                   Show planned actions without changing the system.
  -h, --help                  Show this help text.

Environment:
  GITHUB_TOKEN               Optional GitHub token for private release downloads.
  GH_TOKEN                   Alternative GitHub token variable name.

Examples:
  sudo ./scripts/install-server.sh
  sudo ./scripts/install-server.sh --release-version 0.0.2
  sudo ./scripts/install-server.sh --server-deb-file ./dist/nmc-server_0.0.2_amd64.deb
USAGE
}

DRY_RUN=0
SERVER_DEB_SOURCE_PATH=""
CLIENT_DEB_SOURCE_PATH=""
PACKAGE_RELEASE_VERSION=""
RELEASE_REPO="neuralmimicry/nmc"
RUN_USER="nmc"
RUN_GROUP=""
SERVICE_NAME="nmc-server"
PORT="8080"
STATE_DIR="/var/lib/nmc"
ENV_FILE_PATH="/etc/nmc/nmc.env"
KUBECONFIG_SOURCE_PATH=""
INSTALL_CLIENT=1
SKIP_START=0
FORCE_ENV_FILE=0
RUN_PREFIX=()
CLIENT_PACKAGED_BIN="/usr/bin/nmc"
CLIENT_INSTALL_PATH="/usr/local/bin/nmc"

while (($#)); do
  case "$1" in
    --server-deb-file)
      shift
      (($#)) || nmc_die "--server-deb-file requires a value"
      SERVER_DEB_SOURCE_PATH="$1"
      ;;
    --client-deb-file)
      shift
      (($#)) || nmc_die "--client-deb-file requires a value"
      CLIENT_DEB_SOURCE_PATH="$1"
      ;;
    --release-version)
      shift
      (($#)) || nmc_die "--release-version requires a value"
      PACKAGE_RELEASE_VERSION="$1"
      ;;
    --repo)
      shift
      (($#)) || nmc_die "--repo requires a value"
      RELEASE_REPO="$1"
      ;;
    --run-user)
      shift
      (($#)) || nmc_die "--run-user requires a value"
      RUN_USER="$1"
      ;;
    --run-group)
      shift
      (($#)) || nmc_die "--run-group requires a value"
      RUN_GROUP="$1"
      ;;
    --service-name)
      shift
      (($#)) || nmc_die "--service-name requires a value"
      SERVICE_NAME="$1"
      ;;
    --port)
      shift
      (($#)) || nmc_die "--port requires a value"
      PORT="$1"
      ;;
    --state-dir)
      shift
      (($#)) || nmc_die "--state-dir requires a value"
      STATE_DIR="$1"
      ;;
    --env-file)
      shift
      (($#)) || nmc_die "--env-file requires a value"
      ENV_FILE_PATH="$1"
      ;;
    --kubeconfig)
      shift
      (($#)) || nmc_die "--kubeconfig requires a value"
      KUBECONFIG_SOURCE_PATH="$1"
      ;;
    --skip-client)
      INSTALL_CLIENT=0
      ;;
    --skip-start)
      SKIP_START=1
      ;;
    --force-env-file)
      FORCE_ENV_FILE=1
      ;;
    --dry-run)
      DRY_RUN=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      nmc_die "unknown option: $1"
      ;;
  esac
  shift
done

run() {
  if ((DRY_RUN)); then
    printf '+'
    for arg in "${RUN_PREFIX[@]}" "$@"; do
      printf ' %q' "$arg"
    done
    printf '\n'
    return 0
  fi

  if ((${#RUN_PREFIX[@]})); then
    "${RUN_PREFIX[@]}" "$@"
  else
    "$@"
  fi
}

write_file() {
  local path="$1"
  local mode="$2"
  local content="$3"

  if ((DRY_RUN)); then
    printf '\n>>> %s (mode %s)\n%s\n' "$path" "$mode" "$content"
    return 0
  fi

  local tmp
  tmp="$(mktemp)"
  printf '%s\n' "$content" >"$tmp"
  run install -D -m "$mode" "$tmp" "$path"
  rm -f "$tmp"
}

require_sudo() {
  if [[ $(id -u) -ne 0 ]]; then
    command -v sudo >/dev/null 2>&1 || nmc_die "sudo is required for package installation"
    RUN_PREFIX=(sudo)
  fi
}

reconcile_client_command_path() {
  local packaged_bin="$1"
  local install_path="$2"
  local backup_path=""
  local resolved_target=""

  [[ -x "$packaged_bin" ]] || nmc_die "expected packaged client at $packaged_bin after installation"
  [[ "$install_path" != "$packaged_bin" ]] || return 0

  if [[ -L "$install_path" ]]; then
    resolved_target="$(readlink -f "$install_path" 2>/dev/null || true)"
    if [[ "$resolved_target" == "$packaged_bin" ]]; then
      return 0
    fi
  fi

  run install -d -m 0755 "$(dirname "$install_path")"

  if [[ -e "$install_path" && ! -L "$install_path" ]]; then
    backup_path="${install_path}.pre-package.$(date +%Y%m%d%H%M%S)"
    run mv "$install_path" "$backup_path"
    nmc_warn "moved existing $install_path to $backup_path so the packaged client is used"
  fi

  run ln -sfn "$packaged_bin" "$install_path"
}

ensure_group() {
  local group="$1"
  if ! getent group "$group" >/dev/null 2>&1; then
    run groupadd --system "$group"
  fi
}

ensure_user() {
  local user="$1"
  local group="$2"
  local home="$3"
  if ! id -u "$user" >/dev/null 2>&1; then
    run useradd --system --home "$home" --shell /usr/sbin/nologin --gid "$group" "$user"
  fi
}

validate_port() {
  [[ "$PORT" =~ ^[0-9]+$ ]] || nmc_die "port must be numeric"
  ((PORT >= 1 && PORT <= 65535)) || nmc_die "port must be in the range 1-65535"
}

prepare_kubeconfig() {
  local effective_source="$KUBECONFIG_SOURCE_PATH"

  if [[ -z "$effective_source" && -f /etc/nmc/kubeconfig ]]; then
    return 0
  fi
  if [[ -z "$effective_source" && -f /etc/kubernetes/admin.conf ]]; then
    effective_source="/etc/kubernetes/admin.conf"
  fi
  [[ -n "$effective_source" ]] || nmc_die "no kubeconfig source was found; pass --kubeconfig or provision /etc/kubernetes/admin.conf"
  [[ -f "$effective_source" ]] || nmc_die "kubeconfig not found: $effective_source"

  run install -D -m 0600 -o "$RUN_USER" -g "$RUN_GROUP" "$effective_source" /etc/nmc/kubeconfig
}

[[ "$(uname -s)" == "Linux" ]] || nmc_die "this installer currently supports Linux only"
command -v systemctl >/dev/null 2>&1 || nmc_die "systemctl is required"
require_sudo
validate_port

if [[ -z "$RUN_GROUP" ]]; then
  RUN_GROUP="$RUN_USER"
fi

PACKAGE_ARCH="$(nmc_detect_deb_arch)"
TOKEN="$(nmc_github_token || true)"

if [[ -z "$PACKAGE_RELEASE_VERSION" ]]; then
  if version_file="$(nmc_find_version_file "$SCRIPT_DIR" 2>/dev/null)"; then
    PACKAGE_RELEASE_VERSION="$(nmc_read_version_file "$version_file")"
  else
    PACKAGE_RELEASE_VERSION="$(nmc_resolve_latest_release_version "$RELEASE_REPO" "$TOKEN")"
  fi
fi

PACKAGE_RELEASE_TAG="v${PACKAGE_RELEASE_VERSION}"
DEB_PACKAGE_VERSION="$(nmc_debian_package_version "$PACKAGE_RELEASE_VERSION")"
SERVER_DEB_ASSET_NAME="nmc-server_${DEB_PACKAGE_VERSION}_${PACKAGE_ARCH}.deb"
CLIENT_DEB_ASSET_NAME="nmc-client_${DEB_PACKAGE_VERSION}_${PACKAGE_ARCH}.deb"

cleanup_tmp_dir() {
  if [[ -n "${TMP_DIR:-}" && -d "${TMP_DIR:-}" ]]; then
    rm -rf "$TMP_DIR"
  fi
}
trap cleanup_tmp_dir EXIT

resolve_deb_source() {
  local package_name="$1"
  local explicit_path="$2"
  local asset_name="$3"
  local result_var="$4"
  local resolved

  if [[ -n "$explicit_path" ]]; then
    [[ -f "$explicit_path" ]] || nmc_die "missing local package: $explicit_path"
    resolved="$(readlink -f "$explicit_path")"
  elif resolved="$(nmc_find_local_deb \
      "$package_name" \
      "$PACKAGE_RELEASE_VERSION" \
      "$PACKAGE_ARCH" \
      "$SCRIPT_DIR" \
      "$SCRIPT_DIR/dist" \
      "$SCRIPT_DIR/.." \
      "$SCRIPT_DIR/../dist" \
      "$PWD")"; then
    :
  else
    if [[ -z "${TMP_DIR:-}" ]]; then
      TMP_DIR="$(mktemp -d)"
    fi
    resolved="$TMP_DIR/$asset_name"
    nmc_log "downloading ${package_name} package: GitHub release ${PACKAGE_RELEASE_TAG} asset ${asset_name}"
    nmc_download_release_asset "$RELEASE_REPO" "$PACKAGE_RELEASE_TAG" "$asset_name" "$resolved" "$TOKEN"
  fi

  printf -v "$result_var" '%s' "$resolved"
}

resolve_deb_source "nmc-server" "$SERVER_DEB_SOURCE_PATH" "$SERVER_DEB_ASSET_NAME" SERVER_DEB_SOURCE_PATH
if ((INSTALL_CLIENT)); then
  resolve_deb_source "nmc-client" "$CLIENT_DEB_SOURCE_PATH" "$CLIENT_DEB_ASSET_NAME" CLIENT_DEB_SOURCE_PATH
fi

nmc_log
nmc_log "NMC server installation plan:"
nmc_log "  release:        ${PACKAGE_RELEASE_TAG}"
nmc_log "  package arch:   ${PACKAGE_ARCH}"
nmc_log "  server package: ${SERVER_DEB_SOURCE_PATH}"
if ((INSTALL_CLIENT)); then
  nmc_log "  client package: ${CLIENT_DEB_SOURCE_PATH}"
else
  nmc_log "  client package: skipped"
fi
nmc_log "  service user:   ${RUN_USER}:${RUN_GROUP}"
nmc_log "  service name:   ${SERVICE_NAME}.service"
nmc_log "  state dir:      ${STATE_DIR}"
nmc_log "  env file:       ${ENV_FILE_PATH}"
nmc_log "  docs dir:       /usr/share/nmc-server/docs"
nmc_log

run apt-get update
run apt-get install -y "$SERVER_DEB_SOURCE_PATH"
if ((INSTALL_CLIENT)); then
  run apt-get install -y "$CLIENT_DEB_SOURCE_PATH"
  reconcile_client_command_path "$CLIENT_PACKAGED_BIN" "$CLIENT_INSTALL_PATH"
fi

ensure_group "$RUN_GROUP"
ensure_user "$RUN_USER" "$RUN_GROUP" "$STATE_DIR"
run install -d -m 0755 -o "$RUN_USER" -g "$RUN_GROUP" "$STATE_DIR" "$STATE_DIR/logs" /etc/nmc
prepare_kubeconfig

if [[ ! -e "$ENV_FILE_PATH" || "$FORCE_ENV_FILE" -eq 1 ]]; then
  write_file "$ENV_FILE_PATH" 0600 "# Optional environment overrides for ${SERVICE_NAME}.service
# NMC_AUTH_MODE=token
# NMC_AUTH_TOKEN=change-me
# NMC_OIDC_INTROSPECTION_URL=
# NMC_OIDC_CLIENT_ID=
# NMC_OIDC_CLIENT_SECRET=
# NMC_OIDC_ISSUER=
# NMC_OIDC_AUDIENCE=
# NMC_OIDC_ALLOWED_AUDIENCES=
# NMC_OIDC_REQUIRED_SCOPE=
# NMC_CENTRAL_AUTH_SESSION_URL=
# NMC_CENTRAL_AUTH_LOGIN_URL=
# NMC_CENTRAL_AUTH_CACHE_TTL_MS=15000
# NMC_CENTRAL_AUTH_TIMEOUT_MS=3000
# NMC_CENTRAL_AUTH_TLS_VERIFY=true
# NMC_OSHIFT_API_URL=http://127.0.0.1:8000
# NMC_GAIL_BASE_URL=
# NMC_GAIL_API_TOKEN=
# NMC_TRACEY_BOOTSTRAP_LOCAL_AGENT=true
# NMC_TRACEY_LOCAL_AGENT_ID=tracey-continuum-local
# NMC_TRACEY_LOCAL_STATUS_ADDR=http://127.0.0.1:48000
# NMC_TRACEY_STATE_ROOT=/var/lib/nmc/tracey-state
# NMC_TRACEY_PERSIST_FLUSH_MS=5000
# NMC_TRACEY_POSTGRES_DSN=
# NMC_POSTGRES_DSN=
else
  nmc_log "keeping existing environment file: ${ENV_FILE_PATH}"
fi

write_file "/etc/systemd/system/${SERVICE_NAME}.service" 0644 "[Unit]
Description=NMC Server
After=network-online.target
Wants=network-online.target

[Service]
User=${RUN_USER}
Group=${RUN_GROUP}
WorkingDirectory=${STATE_DIR}
Environment=NMC_DOCS_DIR=/usr/share/nmc-server/docs
Environment=NMC_LOG_FILE=${STATE_DIR}/logs/nmc_server.log
EnvironmentFile=-${ENV_FILE_PATH}
ExecStart=/usr/bin/nmc_server --port ${PORT} -k /etc/nmc/kubeconfig
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target"

run systemctl daemon-reload
run systemctl enable "${SERVICE_NAME}.service"
if (( ! SKIP_START )); then
  run systemctl restart "${SERVICE_NAME}.service"
fi

nmc_log "service installed: ${SERVICE_NAME}.service"
if ((SKIP_START)); then
  nmc_log "start skipped; run: systemctl start ${SERVICE_NAME}.service"
else
  nmc_log "check status with: systemctl status ${SERVICE_NAME}.service"
fi
