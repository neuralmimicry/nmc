#!/usr/bin/env bash
set -euo pipefail

nmc_log() {
  printf '%s\n' "$*"
}

nmc_warn() {
  printf 'warning: %s\n' "$*" >&2
}

nmc_die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

nmc_env_first() {
  local name value
  for name in "$@"; do
    value="${!name-}"
    if [[ -n "$value" ]]; then
      printf '%s\n' "$value"
      return 0
    fi
  done
  return 1
}

nmc_proc_status_field() {
  local pid="$1"
  local field="$2"
  [[ "$pid" =~ ^[0-9]+$ ]] || return 1
  awk -v field="$field" '$1 == field ":" { print $2; exit }' "/proc/$pid/status" 2>/dev/null
}

nmc_proc_env_first() {
  local pid="$1"
  shift
  [[ "$pid" =~ ^[0-9]+$ ]] || return 1
  [[ -r "/proc/$pid/environ" ]] || return 1

  local line name
  while IFS= read -r line; do
    for name in "$@"; do
      if [[ "$line" == "$name="* ]]; then
        printf '%s\n' "${line#*=}"
        return 0
      fi
    done
  done < <(tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null)

  return 1
}

nmc_sudo_ancestor_env_token() {
  local target_uid="${SUDO_UID-}"
  local pid="${PPID-}"
  local proc_uid next_pid token

  [[ -n "$target_uid" && "$target_uid" =~ ^[0-9]+$ ]] || return 1

  while [[ "$pid" =~ ^[0-9]+$ ]] && (( pid > 1 )); do
    proc_uid="$(nmc_proc_status_field "$pid" Uid || true)"
    if [[ "$proc_uid" == "$target_uid" ]]; then
      token="$(nmc_proc_env_first "$pid" GITHUB_TOKEN GH_TOKEN 2>/dev/null || true)"
      if [[ -n "$token" ]]; then
        printf '%s\n' "$token"
        return 0
      fi
    fi

    next_pid="$(nmc_proc_status_field "$pid" PPid || true)"
    [[ -n "$next_pid" && "$next_pid" != "$pid" ]] || break
    pid="$next_pid"
  done

  return 1
}

nmc_sudo_user_gh_token() {
  local sudo_user="${SUDO_USER-}"
  local token=""

  [[ -n "$sudo_user" && "$sudo_user" != "root" ]] || return 1
  command -v gh >/dev/null 2>&1 || return 1

  if command -v runuser >/dev/null 2>&1; then
    token="$(runuser -u "$sudo_user" -- gh auth token 2>/dev/null || true)"
  elif command -v sudo >/dev/null 2>&1; then
    token="$(sudo -u "$sudo_user" gh auth token 2>/dev/null || true)"
  fi

  [[ -n "$token" ]] || return 1
  printf '%s\n' "$token"
}

nmc_detect_deb_arch() {
  local arch=""
  if command -v dpkg >/dev/null 2>&1; then
    arch="$(dpkg --print-architecture 2>/dev/null || true)"
  fi

  if [[ -z "$arch" ]]; then
    case "$(uname -m)" in
      x86_64|amd64)
        arch="amd64"
        ;;
      aarch64|arm64)
        arch="arm64"
        ;;
    esac
  fi

  case "$arch" in
    amd64|arm64)
      printf '%s\n' "$arch"
      ;;
    *)
      nmc_die "unsupported Debian architecture '$arch'"
      ;;
  esac
}

nmc_debian_package_version() {
  local version sanitized
  version="$1"
  [[ -n "$version" ]] || nmc_die "Debian package version is empty"
  sanitized=$(
    printf '%s' "$version" \
      | tr '-' '~' \
      | sed -E 's/[^A-Za-z0-9.+:~]+/./g; s/^[^A-Za-z0-9]+//; s/[^A-Za-z0-9]+$//'
  )
  [[ -n "$sanitized" ]] || nmc_die "unable to derive Debian package version from '$version'"
  printf '%s\n' "$sanitized"
}

nmc_read_version_file() {
  local path="$1"
  [[ -r "$path" ]] || return 1
  tr -d '[:space:]' <"$path"
}

nmc_find_version_file() {
  local script_dir="$1"
  local candidate
  for candidate in \
    "$script_dir/VERSION" \
    "$script_dir/../VERSION" \
    "$PWD/VERSION"
  do
    if [[ -r "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

nmc_find_local_deb() {
  local package_name="$1"
  local release_version="$2"
  local arch="$3"
  shift 3

  local deb_version filename dir candidate
  deb_version="$(nmc_debian_package_version "$release_version")"
  filename="${package_name}_${deb_version}_${arch}.deb"

  for dir in "$@"; do
    [[ -n "$dir" && -d "$dir" ]] || continue
    candidate="$dir/$filename"
    if [[ -f "$candidate" ]]; then
      (
        cd "$dir"
        printf '%s/%s\n' "$PWD" "$filename"
      )
      return 0
    fi
  done

  return 1
}

nmc_github_token() {
  local token=""

  token="$(nmc_env_first GITHUB_TOKEN GH_TOKEN 2>/dev/null || true)"
  if [[ -n "$token" ]]; then
    printf '%s\n' "$token"
    return 0
  fi

  if [[ $(id -u) -eq 0 && -n "${SUDO_USER-}" ]]; then
    token="$(nmc_sudo_ancestor_env_token 2>/dev/null || true)"
    if [[ -n "$token" ]]; then
      printf '%s\n' "$token"
      return 0
    fi

    token="$(nmc_sudo_user_gh_token 2>/dev/null || true)"
    if [[ -n "$token" ]]; then
      printf '%s\n' "$token"
      return 0
    fi
  fi

  return 1
}

nmc_resolve_latest_release_version() {
  local repo="$1"
  local token="${2:-}"

  command -v python3 >/dev/null 2>&1 \
    || nmc_die "python3 is required to resolve the latest private GitHub release"

  python3 - "$repo" "$token" <<'PY'
import json
import sys
import urllib.error
import urllib.request

repo, token = sys.argv[1:3]
headers = {
    "Accept": "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
    "User-Agent": "nmc-installer",
}
if token:
    headers["Authorization"] = f"Bearer {token}"

request = urllib.request.Request(
    f"https://api.github.com/repos/{repo}/releases/latest",
    headers=headers,
)
try:
    with urllib.request.urlopen(request) as response:
        payload = json.load(response)
except urllib.error.HTTPError as exc:
    sys.stderr.write(f"error: failed to resolve latest release for {repo}: HTTP {exc.code}\n")
    raise SystemExit(1)

tag = payload.get("tag_name", "")
if not isinstance(tag, str) or not tag:
    sys.stderr.write(f"error: release metadata for {repo} did not include tag_name\n")
    raise SystemExit(1)
print(tag[1:] if tag.startswith("v") else tag)
PY
}

nmc_gh_authenticated() {
  command -v gh >/dev/null 2>&1 || return 1
  if [[ -n "${1:-}" ]]; then
    GH_TOKEN="$1" gh auth status >/dev/null 2>&1
  else
    gh auth status >/dev/null 2>&1
  fi
}

nmc_download_release_asset_via_gh() {
  local repo="$1"
  local tag="$2"
  local asset_name="$3"
  local out="$4"
  local token="${5:-}"

  if [[ -n "$token" ]]; then
    GH_TOKEN="$token" gh release download "$tag" \
      --repo "$repo" \
      --pattern "$asset_name" \
      --output "$out" \
      --clobber
  else
    gh release download "$tag" \
      --repo "$repo" \
      --pattern "$asset_name" \
      --output "$out" \
      --clobber
  fi
}

nmc_download_release_asset_via_api() {
  local repo="$1"
  local tag="$2"
  local asset_name="$3"
  local token="$4"
  local out="$5"

  command -v python3 >/dev/null 2>&1 \
    || nmc_die "python3 is required for token-authenticated GitHub downloads"

  python3 - "$repo" "$tag" "$asset_name" "$token" "$out" <<'PY'
import json
import sys
import urllib.error
import urllib.request

repo, tag, asset_name, token, out = sys.argv[1:]
base_headers = {
    "Accept": "application/vnd.github+json",
    "Authorization": f"Bearer {token}",
    "X-GitHub-Api-Version": "2022-11-28",
    "User-Agent": "nmc-installer",
}
release_request = urllib.request.Request(
    f"https://api.github.com/repos/{repo}/releases/tags/{tag}",
    headers=base_headers,
)
try:
    with urllib.request.urlopen(release_request) as response:
        payload = json.load(response)
except urllib.error.HTTPError as exc:
    sys.stderr.write(f"error: failed to fetch release metadata for {repo}@{tag}: HTTP {exc.code}\n")
    raise SystemExit(1)

asset_url = None
for asset in payload.get("assets", []):
    if asset.get("name") == asset_name:
        asset_url = asset.get("url")
        break

if not asset_url:
    sys.stderr.write(f"error: release {tag} does not contain asset {asset_name}\n")
    raise SystemExit(1)

asset_request = urllib.request.Request(
    asset_url,
    headers={
        "Accept": "application/octet-stream",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "nmc-installer",
    },
)
try:
    with urllib.request.urlopen(asset_request) as response, open(out, "wb") as handle:
        handle.write(response.read())
except urllib.error.HTTPError as exc:
    sys.stderr.write(f"error: failed to download asset {asset_name}: HTTP {exc.code}\n")
    raise SystemExit(1)
PY
}

nmc_download_release_asset() {
  local repo="$1"
  local tag="$2"
  local asset_name="$3"
  local out="$4"
  local token="${5:-}"

  if command -v gh >/dev/null 2>&1; then
    if nmc_gh_authenticated "$token"; then
      if nmc_download_release_asset_via_gh "$repo" "$tag" "$asset_name" "$out" "$token"; then
        return 0
      fi
      nmc_warn "gh release download failed for ${asset_name}; falling back"
    fi
  fi

  if [[ -n "$token" ]]; then
    nmc_download_release_asset_via_api "$repo" "$tag" "$asset_name" "$token" "$out"
    return 0
  fi

  nmc_die "failed to download ${asset_name}; authenticate gh or set GITHUB_TOKEN or GH_TOKEN for private releases (when invoking via sudo, run without sudo or use sudo --preserve-env=GITHUB_TOKEN,GH_TOKEN)"
}
