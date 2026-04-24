#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/install-common.sh
source "$SCRIPT_DIR/install-common.sh"

usage() {
  cat <<'USAGE'
Usage: install-client.sh [options]

Install the NMC CLI Debian package on Linux.
The installer prefers a local .deb when available and otherwise downloads the
matching private GitHub release asset.

Options:
  --deb-file PATH            Local nmc-client .deb to install.
  --release-version VERSION  Release version to install. Defaults to VERSION or latest release.
  --repo OWNER/NAME          GitHub repository. Default: neuralmimicry/nmc
  --dry-run                  Show planned actions without changing the system.
  -h, --help                 Show this help text.

Environment:
  GITHUB_TOKEN               Optional GitHub token for private release downloads.
  GH_TOKEN                   Alternative GitHub token variable name.

Examples:
  ./scripts/install-client.sh
  sudo ./scripts/install-client.sh --release-version 0.0.1
  sudo ./scripts/install-client.sh --deb-file ./dist/nmc-client_0.0.1_amd64.deb
USAGE
}

DRY_RUN=0
DEB_SOURCE_PATH=""
PACKAGE_RELEASE_VERSION=""
RELEASE_REPO="neuralmimicry/nmc"
RUN_PREFIX=()

while (($#)); do
  case "$1" in
    --deb-file)
      shift
      (($#)) || nmc_die "--deb-file requires a value"
      DEB_SOURCE_PATH="$1"
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

require_sudo() {
  if [[ $(id -u) -ne 0 ]]; then
    command -v sudo >/dev/null 2>&1 || nmc_die "sudo is required for package installation"
    RUN_PREFIX=(sudo)
  fi
}

[[ "$(uname -s)" == "Linux" ]] || nmc_die "this installer currently supports Linux only"
require_sudo

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
DEB_ASSET_NAME="nmc-client_${DEB_PACKAGE_VERSION}_${PACKAGE_ARCH}.deb"

if [[ -n "$DEB_SOURCE_PATH" ]]; then
  [[ -f "$DEB_SOURCE_PATH" ]] || nmc_die "missing local package: $DEB_SOURCE_PATH"
  DEB_SOURCE_PATH="$(readlink -f "$DEB_SOURCE_PATH")"
else
  if DEB_SOURCE_PATH="$(nmc_find_local_deb \
      "nmc-client" \
      "$PACKAGE_RELEASE_VERSION" \
      "$PACKAGE_ARCH" \
      "$SCRIPT_DIR" \
      "$SCRIPT_DIR/dist" \
      "$SCRIPT_DIR/.." \
      "$SCRIPT_DIR/../dist" \
      "$PWD")"; then
    :
  else
    tmp_dir="$(mktemp -d)"
    trap 'rm -rf "$tmp_dir"' EXIT
    DEB_SOURCE_PATH="$tmp_dir/$DEB_ASSET_NAME"
    nmc_log "downloading NMC client package: GitHub release ${PACKAGE_RELEASE_TAG} asset ${DEB_ASSET_NAME}"
    nmc_download_release_asset "$RELEASE_REPO" "$PACKAGE_RELEASE_TAG" "$DEB_ASSET_NAME" "$DEB_SOURCE_PATH" "$TOKEN"
  fi
fi

nmc_log
nmc_log "NMC client installation plan:"
nmc_log "  release:        ${PACKAGE_RELEASE_TAG}"
nmc_log "  package arch:   ${PACKAGE_ARCH}"
nmc_log "  package source: ${DEB_SOURCE_PATH}"
nmc_log

run apt-get update
run apt-get install -y "$DEB_SOURCE_PATH"

if (( ! DRY_RUN )); then
  nmc_log
  nmc_log "installed nmc client: $(command -v nmc || printf '/usr/bin/nmc')"
  if command -v nmc >/dev/null 2>&1; then
    nmc version --no-check || true
  fi
fi
