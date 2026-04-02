#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: package-release.sh [options]

Package an NMC Unix release artifact.

Options:
  --component client|server   Logical component name.
  --version VERSION           Version label for the packaged artifact.
  --input PATH                Built binary to package.
  --platform NAME             Platform suffix in output names.
  --output-dir DIR            Directory to receive packaged artifacts.
  --rename NAME               Optional binary name inside the archive.
  -h, --help                  Show this help text.
USAGE
}

log() {
  printf '%s\n' "$*"
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

sha256_tool() {
  if command -v sha256sum >/dev/null 2>&1; then
    printf 'sha256sum\n'
  elif command -v shasum >/dev/null 2>&1; then
    printf 'shasum -a 256\n'
  else
    die "sha256sum or shasum is required"
  fi
}

COMPONENT=
VERSION=
INPUT=
PLATFORM=
OUTPUT_DIR=
RENAME=

while (($#)); do
  case "$1" in
    --component)
      shift
      (($#)) || die "--component requires a value"
      COMPONENT="$1"
      ;;
    --version)
      shift
      (($#)) || die "--version requires a value"
      VERSION="$1"
      ;;
    --input)
      shift
      (($#)) || die "--input requires a value"
      INPUT="$1"
      ;;
    --platform)
      shift
      (($#)) || die "--platform requires a value"
      PLATFORM="$1"
      ;;
    --output-dir)
      shift
      (($#)) || die "--output-dir requires a value"
      OUTPUT_DIR="$1"
      ;;
    --rename)
      shift
      (($#)) || die "--rename requires a value"
      RENAME="$1"
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

[[ "$COMPONENT" == "client" || "$COMPONENT" == "server" ]] || die "--component must be client or server"
[[ -n "$VERSION" ]] || die "--version is required"
[[ -n "$INPUT" ]] || die "--input is required"
[[ -n "$PLATFORM" ]] || die "--platform is required"
[[ -n "$OUTPUT_DIR" ]] || die "--output-dir is required"

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)
INPUT=$(CDPATH='' cd -- "$(dirname -- "$INPUT")" && pwd)/$(basename -- "$INPUT")
[[ -f "$INPUT" ]] || die "missing input binary: $INPUT"

if [[ -z "$RENAME" ]]; then
  if [[ "$COMPONENT" == "client" ]]; then
    RENAME="nmc"
  else
    RENAME="nmc_server"
  fi
fi

ARCHIVE_BASENAME="nmc-${COMPONENT}-${VERSION}-${PLATFORM}"
OUTPUT_DIR=$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)
STAGE_ROOT="$OUTPUT_DIR/.stage"
PAYLOAD_DIR="$STAGE_ROOT/$ARCHIVE_BASENAME"
ARCHIVE_PATH="$OUTPUT_DIR/${ARCHIVE_BASENAME}.tar.gz"
CHECKSUM_PATH="$OUTPUT_DIR/${ARCHIVE_BASENAME}.sha256.txt"

rm -rf "$STAGE_ROOT"
mkdir -p "$PAYLOAD_DIR"
install -m 0755 "$INPUT" "$PAYLOAD_DIR/$RENAME"
if [[ -f "$REPO_ROOT/README.md" ]]; then
  install -m 0644 "$REPO_ROOT/README.md" "$PAYLOAD_DIR/README.md"
fi

tar -C "$STAGE_ROOT" -czf "$ARCHIVE_PATH" "$ARCHIVE_BASENAME"
checksum_cmd=$(sha256_tool)
(
  cd "$OUTPUT_DIR"
  $checksum_cmd "$(basename "$ARCHIVE_PATH")" >"$(basename "$CHECKSUM_PATH")"
)

log
log "packaged NMC release artifacts:"
log "  $ARCHIVE_PATH"
log "  $CHECKSUM_PATH"

rm -rf "$STAGE_ROOT"
