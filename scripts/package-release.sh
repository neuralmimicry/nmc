#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: package-release.sh [options]

Package NMC release artifacts.

Options:
  --component client|server   Logical component name.
  --version VERSION           Version label for the packaged artifact.
  --input PATH                Built binary to package.
  --platform NAME             Platform suffix in output names.
  --output-dir DIR            Directory to receive packaged artifacts.
  --rename NAME               Optional binary name inside the archive and package.
  --deb-arch ARCH             Also build a Debian package for linux using ARCH (amd64 or arm64).
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

validate_deb_arch() {
  case "$1" in
    amd64|arm64)
      ;;
    *)
      die "unsupported Debian architecture: $1"
      ;;
  esac
}

debian_package_version() {
  local version sanitized
  version="$1"
  [[ -n "$version" ]] || die "Debian package version is empty"
  sanitized=$(
    printf '%s' "$version" \
      | tr '-' '~' \
      | sed -E 's/[^A-Za-z0-9.+:~]+/./g; s/^[^A-Za-z0-9]+//; s/[^A-Za-z0-9]+$//'
  )
  [[ -n "$sanitized" ]] || die "unable to derive Debian package version from '$version'"
  printf '%s\n' "$sanitized"
}

compute_deb_depends() {
  if ! command -v dpkg-shlibdeps >/dev/null 2>&1; then
    printf '\n'
    return
  fi

  local binary="$1"
  shift

  local work_dir stage_root binary_stage_path output depends extra_dir extra_stage_dir extra_stage_file
  local -a extra_args=() analyze_args=()

  work_dir="$(mktemp -d)"
  stage_root="$work_dir/root"
  binary_stage_path="$stage_root/usr/bin/$(basename "$binary")"

  install -d -m 0755 "$work_dir/debian" "$(dirname "$binary_stage_path")"
  cat >"$work_dir/debian/control" <<'EOF'
Source: nmc
Section: admin
Priority: optional
Maintainer: NeuralMimicry <opensource@neuralmimicry.ai>
Standards-Version: 4.7.0
Package: nmc
Architecture: any
Description: temporary shlibdeps metadata carrier
EOF

  install -m 0755 "$binary" "$binary_stage_path"
  analyze_args+=("-e" "./${binary_stage_path#$work_dir/}")
  for extra_dir in "$@"; do
    [[ -n "$extra_dir" && -d "$extra_dir" ]] || continue
    extra_stage_dir="$stage_root/usr/lib/$(basename "$extra_dir")"
    install -d -m 0755 "$extra_stage_dir"
    cp -a "$extra_dir"/. "$extra_stage_dir"/ 2>/dev/null || true
    extra_args+=("-l./${extra_stage_dir#$work_dir/}")
    while IFS= read -r extra_stage_file; do
      analyze_args+=("-e" "./${extra_stage_file#$work_dir/}")
    done < <(find "$extra_stage_dir" -type f -name '*.so*' | sort)
  done

  output=$(
    cd "$work_dir"
    dpkg-shlibdeps --ignore-missing-info -O -Tdebian/substvars \
      "${analyze_args[@]}" "${extra_args[@]}" 2>/dev/null || true
  )
  rm -rf "$work_dir"
  depends="$(printf '%s\n' "$output" | sed -n 's/^shlibs:Depends=//p' | tail -n 1)"
  printf '%s\n' "$depends" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//; s/, ,/, /g; s/^, //; s/, $//'
}

resolve_server_runtime_library() {
  local binary="$1"
  local pattern="$2"
  ldd "$binary" | awk -v pattern="$pattern" '$1 ~ pattern && $3 ~ /^\// { print $3; exit }'
}

bundle_library_file() {
  local source_path="$1"
  local dest_dir="$2"

  [[ -n "$source_path" && -f "$source_path" ]] || return 1

  install -d -m 0755 "$dest_dir"
  install -m 0644 "$source_path" "$dest_dir/$(basename "$source_path")"
  printf '%s\n' "$(basename "$source_path")"
}

bundle_server_deb_runtime_libs() {
  local binary="$1"
  local dest_dir="$2"
  local libpqxx_path=""

  libpqxx_path="$(resolve_server_runtime_library "$binary" "^libpqxx-[^[:space:]]*[.]so")"
  if [[ -n "$libpqxx_path" ]]; then
    bundle_library_file "$libpqxx_path" "$dest_dir"
  fi
}

filter_bundled_deb_depends() {
  local depends="$1"
  shift

  local bundled_lib
  for bundled_lib in "$@"; do
    case "$bundled_lib" in
      libpqxx-*.so*)
        depends="$(printf '%s' "$depends" | sed -E 's/(^|, )libpqxx-[^,]+( \([^)]*\))?(, )?/\1/g')"
        ;;
    esac
  done

  printf '%s\n' "$depends" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//; s/, ,/, /g; s/^, //; s/, $//'
}

install_linux_helpers_into_dir() {
  local dest_dir="$1"
  install -m 0755 "$REPO_ROOT/scripts/install-common.sh" "$dest_dir/install-common.sh"
  install -m 0755 "$REPO_ROOT/scripts/install-client.sh" "$dest_dir/install-client.sh"
  if [[ "$COMPONENT" == "server" ]]; then
    install -m 0755 "$REPO_ROOT/scripts/install-server.sh" "$dest_dir/install-server.sh"
  fi
}

create_debian_package() {
  local deb_name deb_version deb_stage_root deb_root deb_path depends docs_root bundled_lib
  local -a bundled_libs=()

  [[ "$PLATFORM" == linux* ]] || die "--deb-arch is only supported for linux platforms"
  validate_deb_arch "$DEB_ARCH"
  command -v dpkg-deb >/dev/null 2>&1 || die "dpkg-deb is required when --deb-arch is used"

  deb_name="nmc-${COMPONENT}"
  deb_version="$(debian_package_version "$VERSION")"
  deb_stage_root="$OUTPUT_DIR/.deb-stage-${COMPONENT}"
  deb_root="$deb_stage_root/root"
  deb_path="$OUTPUT_DIR/${deb_name}_${deb_version}_${DEB_ARCH}.deb"
  docs_root="$deb_root/usr/share/doc/${deb_name}"

  rm -rf "$deb_stage_root"
  install -d -m 0755 \
    "$deb_root/DEBIAN" \
    "$deb_root/usr/bin" \
    "$docs_root"

  install -m 0755 "$INPUT" "$deb_root/usr/bin/$RENAME"
  install -m 0644 "$REPO_ROOT/README.md" "$docs_root/README.md"

  if [[ -f "$REPO_ROOT/VERSION" ]]; then
    install -m 0644 "$REPO_ROOT/VERSION" "$docs_root/VERSION"
  fi

  if [[ "$COMPONENT" == "server" ]]; then
    install -d -m 0755 "$deb_root/usr/share/nmc-server" "$deb_root/usr/lib/nmc-server"
    cp -R "$REPO_ROOT/nmc_server/src/docs" "$deb_root/usr/share/nmc-server/docs"

    while IFS= read -r bundled_lib; do
      [[ -n "$bundled_lib" ]] || continue
      bundled_libs+=("$bundled_lib")
    done < <(bundle_server_deb_runtime_libs "$deb_root/usr/bin/$RENAME" "$deb_root/usr/lib/nmc-server")

    if ((${#bundled_libs[@]})); then
      command -v patchelf >/dev/null 2>&1 \
        || die "patchelf is required to bundle nmc-server runtime libraries"
      patchelf --set-rpath '$ORIGIN/../lib/nmc-server' "$deb_root/usr/bin/$RENAME"
    fi
  fi

  depends="$(compute_deb_depends "$deb_root/usr/bin/$RENAME" "$deb_root/usr/lib/nmc-server")"
  if ((${#bundled_libs[@]})); then
    depends="$(filter_bundled_deb_depends "$depends" "${bundled_libs[@]}")"
  fi

  {
    printf 'Package: %s\n' "$deb_name"
    printf 'Version: %s\n' "$deb_version"
    printf 'Section: admin\n'
    printf 'Priority: optional\n'
    printf 'Architecture: %s\n' "$DEB_ARCH"
    if [[ -n "$depends" ]]; then
      printf 'Depends: %s\n' "$depends"
    fi
    printf 'Maintainer: NeuralMimicry <opensource@neuralmimicry.ai>\n'
    printf 'Homepage: https://github.com/neuralmimicry/nmc\n'
    if [[ "$COMPONENT" == "client" ]]; then
      printf 'Description: NeuralMimicry Continuum CLI client\n'
      printf ' The nmc-client package installs the nmc command-line interface.\n'
    else
      printf 'Description: NeuralMimicry Continuum control-plane server\n'
      printf ' The nmc-server package installs the server binary and bundled docs.\n'
    fi
  } >"$deb_root/DEBIAN/control"

  dpkg-deb --build --root-owner-group "$deb_root" "$deb_path" >/dev/null
  artifacts+=("$deb_path")
  rm -rf "$deb_stage_root"
}

COMPONENT=""
VERSION=""
INPUT=""
PLATFORM=""
OUTPUT_DIR=""
RENAME=""
DEB_ARCH=""

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
    --deb-arch)
      shift
      (($#)) || die "--deb-arch requires a value"
      DEB_ARCH="$1"
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

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)"
INPUT="$(CDPATH='' cd -- "$(dirname -- "$INPUT")" && pwd)/$(basename -- "$INPUT")"
[[ -f "$INPUT" ]] || die "missing input binary: $INPUT"

if [[ -z "$RENAME" ]]; then
  if [[ "$COMPONENT" == "client" ]]; then
    RENAME="nmc"
  else
    RENAME="nmc_server"
  fi
fi

ARCHIVE_BASENAME="nmc-${COMPONENT}-${VERSION}-${PLATFORM}"
OUTPUT_DIR="$(mkdir -p "$OUTPUT_DIR" && cd "$OUTPUT_DIR" && pwd)"
STAGE_ROOT="$OUTPUT_DIR/.stage-${COMPONENT}"
PAYLOAD_DIR="$STAGE_ROOT/$ARCHIVE_BASENAME"
ARCHIVE_PATH="$OUTPUT_DIR/${ARCHIVE_BASENAME}.tar.gz"
CHECKSUM_PATH="$OUTPUT_DIR/${ARCHIVE_BASENAME}.sha256.txt"

rm -rf "$STAGE_ROOT"
mkdir -p "$PAYLOAD_DIR"
install -m 0755 "$INPUT" "$PAYLOAD_DIR/$RENAME"
install -m 0644 "$REPO_ROOT/README.md" "$PAYLOAD_DIR/README.md"
if [[ -f "$REPO_ROOT/VERSION" ]]; then
  install -m 0644 "$REPO_ROOT/VERSION" "$PAYLOAD_DIR/VERSION"
fi

if [[ "$COMPONENT" == "server" ]]; then
  cp -R "$REPO_ROOT/nmc_server/src/docs" "$PAYLOAD_DIR/docs"
fi

case "$PLATFORM" in
  linux*)
    install_linux_helpers_into_dir "$PAYLOAD_DIR"
    ;;
esac

tar -C "$STAGE_ROOT" -czf "$ARCHIVE_PATH" "$ARCHIVE_BASENAME"
artifacts=("$ARCHIVE_PATH")

if [[ -n "$DEB_ARCH" ]]; then
  create_debian_package
fi

checksum_cmd="$(sha256_tool)"
(
  cd "$OUTPUT_DIR"
  relative_artifacts=()
  for artifact in "${artifacts[@]}"; do
    relative_artifacts+=("$(basename "$artifact")")
  done
  $checksum_cmd "${relative_artifacts[@]}" >"$(basename "$CHECKSUM_PATH")"
)

log
log "packaged NMC release artifacts:"
for artifact in "${artifacts[@]}" "$CHECKSUM_PATH"; do
  log "  $artifact"
done

rm -rf "$STAGE_ROOT"
