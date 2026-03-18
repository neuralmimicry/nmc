#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# NMC deploy script for Ubuntu 24.04
# - Installs build dependencies
# - Detects/installs Kubernetes (kubeadm + containerd) when missing
# - Detects GPU and installs OpenCL/CUDA tooling when possible
# - Builds and runs nmc_server as a systemd service
# - Installs an auto-update timer (git pull + rebuild)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$SCRIPT_DIR"

# ---------------------------
# Defaults (override with flags or env)
# ---------------------------
INSTALL_DIR="${INSTALL_DIR:-$REPO_DIR}"
RUN_USER="${NMC_RUN_USER:-nmc}"
RUN_GROUP="${NMC_RUN_GROUP:-$RUN_USER}"
BUILD_USER="${NMC_BUILD_USER:-}"
BUILD_GROUP="${NMC_BUILD_GROUP:-}"
PORT="${NMC_PORT:-8080}"
DOCS_ENABLED="${NMC_DOCS_ENABLED:-true}"
MAX_BODY_BYTES="${NMC_MAX_BODY_BYTES:-1048576}"
MAX_LOG_BODY_BYTES="${NMC_LOG_BODY_BYTES:-2048}"
OSHIFT_URL="${NMC_OSHIFT_API_URL:-http://127.0.0.1:8000}"

AUTH_MODE="${NMC_AUTH_MODE:-token}"
AUTH_TOKEN="${NMC_AUTH_TOKEN:-}"
OIDC_INTROSPECTION_URL="${NMC_OIDC_INTROSPECTION_URL:-}"
OIDC_CLIENT_ID="${NMC_OIDC_CLIENT_ID:-}"
OIDC_CLIENT_SECRET="${NMC_OIDC_CLIENT_SECRET:-}"
OIDC_ISSUER="${NMC_OIDC_ISSUER:-}"
OIDC_AUDIENCE="${NMC_OIDC_AUDIENCE:-}"
OIDC_ALLOWED_AUDIENCES="${NMC_OIDC_ALLOWED_AUDIENCES:-}"
OIDC_REQUIRED_SCOPE="${NMC_OIDC_REQUIRED_SCOPE:-}"

K8S_AUTO_INSTALL="${K8S_AUTO_INSTALL:-true}"
K8S_VERSION_SERIES="${K8S_VERSION_SERIES:-}"
K8S_POD_CIDR="${K8S_POD_CIDR:-10.244.0.0/16}"
K8S_SERVICE_CIDR="${K8S_SERVICE_CIDR:-10.96.0.0/12}"
K8S_KUBECONFIG_SRC="${K8S_KUBECONFIG_SRC:-}"
FLANNEL_MANIFEST_URL="${FLANNEL_MANIFEST_URL:-https://raw.githubusercontent.com/flannel-io/flannel/master/Documentation/kube-flannel.yml}"
NVIDIA_DEVICE_PLUGIN_URL="${NVIDIA_DEVICE_PLUGIN_URL:-https://raw.githubusercontent.com/NVIDIA/k8s-device-plugin/master/deployments/static/nvidia-device-plugin.yml}"

GPU_AUTO="${GPU_AUTO:-true}"
ENABLE_NVIDIA_DEVICE_PLUGIN="${ENABLE_NVIDIA_DEVICE_PLUGIN:-true}"

ENABLE_AUTO_UPDATE="${ENABLE_AUTO_UPDATE:-true}"
REPO_URL="${REPO_URL:-}"
REPO_BRANCH="${REPO_BRANCH:-main}"
SKIP_BUILD="false"
DRY_RUN="false"

APT_UPDATED="false"
GPU_VENDOR="none"

# ---------------------------
# Logging (inspired by swarmhpc/scripts/common.sh)
# ---------------------------
LOG_LEVEL_DEBUG=0
LOG_LEVEL_INFO=1
LOG_LEVEL_WARN=2
LOG_LEVEL_ERROR=3
CURRENT_LOG_LEVEL_NAME="${LOG_LEVEL:-INFO}"

case "$CURRENT_LOG_LEVEL_NAME" in
  DEBUG) CURRENT_LOG_LEVEL=$LOG_LEVEL_DEBUG ;;
  INFO) CURRENT_LOG_LEVEL=$LOG_LEVEL_INFO ;;
  WARN) CURRENT_LOG_LEVEL=$LOG_LEVEL_WARN ;;
  ERROR) CURRENT_LOG_LEVEL=$LOG_LEVEL_ERROR ;;
  *) CURRENT_LOG_LEVEL=$LOG_LEVEL_INFO ;;
esac

if [[ -t 1 ]]; then
  COLOUR_RESET="\033[0m"
  COLOUR_DEBUG="\033[36m"
  COLOUR_INFO="\033[32m"
  COLOUR_WARN="\033[33m"
  COLOUR_ERROR="\033[31m"
else
  COLOUR_RESET=""
  COLOUR_DEBUG=""
  COLOUR_INFO=""
  COLOUR_WARN=""
  COLOUR_ERROR=""
fi

timestamp() { date +"%Y-%m-%d %H:%M:%S"; }

log_debug() { if [[ $CURRENT_LOG_LEVEL -le $LOG_LEVEL_DEBUG ]]; then echo -e "${COLOUR_DEBUG}[$(timestamp)] [DEBUG] $*${COLOUR_RESET}" >&2; fi; }
log_info()  { if [[ $CURRENT_LOG_LEVEL -le $LOG_LEVEL_INFO  ]]; then echo -e "${COLOUR_INFO}[$(timestamp)] [INFO]  $*${COLOUR_RESET}" >&2; fi; }
log_warn()  { if [[ $CURRENT_LOG_LEVEL -le $LOG_LEVEL_WARN  ]]; then echo -e "${COLOUR_WARN}[$(timestamp)] [WARN]  $*${COLOUR_RESET}" >&2; fi; }
log_error() { if [[ $CURRENT_LOG_LEVEL -le $LOG_LEVEL_ERROR ]]; then echo -e "${COLOUR_ERROR}[$(timestamp)] [ERROR] $*${COLOUR_RESET}" >&2; fi; }

die() { log_error "$*"; exit 1; }

# ---------------------------
# Helpers
# ---------------------------
SUDO=""

require_root() {
  if [[ "$(id -u)" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
      SUDO="sudo"
    else
      die "This script needs root privileges. Install sudo or run as root."
    fi
  fi
}

run() {
  if [[ "$DRY_RUN" == "true" ]]; then
    log_info "[dry-run] $*"
    return 0
  fi
  "$@"
}

run_root() {
  if [[ -n "$SUDO" ]]; then
    run $SUDO "$@"
  else
    run "$@"
  fi
}

run_as_user() {
  if [[ "$(id -u)" -eq 0 && "$RUN_USER" != "root" ]]; then
    if command -v sudo >/dev/null 2>&1; then
      run sudo -u "$RUN_USER" -H -- "$@"
    else
      run runuser -u "$RUN_USER" -- "$@"
    fi
  else
    run "$@"
  fi
}

run_as_build_user() {
  if [[ "$(id -u)" -eq 0 && "$BUILD_USER" != "root" ]]; then
    if command -v sudo >/dev/null 2>&1; then
      run sudo -u "$BUILD_USER" -H -- "$@"
    else
      run runuser -u "$BUILD_USER" -- "$@"
    fi
  else
    run "$@"
  fi
}

retry() {
  local max=5
  local delay=2
  local attempt=1
  while true; do
    if "$@"; then
      return 0
    fi
    if [[ $attempt -ge $max ]]; then
      return 1
    fi
    log_warn "Command failed (attempt $attempt/$max). Retrying in ${delay}s..."
    sleep "$delay"
    attempt=$((attempt + 1))
    delay=$((delay * 2))
  done
}

apt_update_once() {
  if [[ "$APT_UPDATED" != "true" ]]; then
    retry run_root apt-get update -y
    APT_UPDATED="true"
  fi
}

apt_install() {
  apt_update_once
  retry run_root apt-get install -y --no-install-recommends "$@"
}

usage() {
  cat <<'USAGE'
Usage: ./deploy.sh [options]

Core options:
  --install-dir PATH              Install/build directory (default: current repo)
  --run-user USER                 Service user (default: nmc)
  --run-group GROUP               Service group (default: nmc)
  --build-user USER               Build user (default: sudo user or run-user)
  --port PORT                     Server port (default: 8080)
  --auth-mode MODE                token|oidc|off (default: token)
  --auth-token TOKEN              Token for token mode
  --oidc-introspection-url URL    OIDC introspection URL
  --oidc-client-id ID             OIDC client ID
  --oidc-client-secret SECRET     OIDC client secret
  --oidc-issuer ISSUER            OIDC issuer
  --oidc-audience AUD             OIDC audience (single or CSV)
  --oidc-allowed-audiences CSV    OIDC allowed audiences
  --oidc-required-scope CSV       OIDC required scope(s)
  --oshift-url URL                OpenShift portal API base URL

Kubernetes:
  --k8s-install                   Install/init Kubernetes if missing (default)
  --no-k8s-install                Do not install Kubernetes
  --k8s-version-series v1.X       Kubernetes repo series (auto-detect, fallback v1.30)
  --k8s-pod-cidr CIDR             Pod CIDR (default: 10.244.0.0/16)
  --k8s-service-cidr CIDR         Service CIDR (default: 10.96.0.0/12)
  --kubeconfig PATH               Use existing kubeconfig (copied to /etc/nmc/kubeconfig)

GPU / OpenCL / CUDA:
  --gpu-auto                      Detect and install GPU stack (default)
  --no-gpu                        Skip GPU/OpenCL/CUDA setup
  --enable-nvidia-device-plugin   Apply NVIDIA device plugin (default: on when GPU detected)
  --disable-nvidia-device-plugin  Skip device plugin

Updates:
  --enable-auto-update            Install systemd timer (default)
  --disable-auto-update           Skip update timer

Repo:
  --repo-url URL                  Clone repo to --install-dir if missing
  --branch BRANCH                 Git branch for updates (default: main)
  --skip-build                    Skip build step

Other:
  --dry-run                       Show actions without executing
  -h, --help                      Show this help
USAGE
}

# ---------------------------
# Argument parsing
# ---------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    --run-user) RUN_USER="$2"; shift 2 ;;
    --run-group) RUN_GROUP="$2"; shift 2 ;;
    --build-user) BUILD_USER="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --auth-mode) AUTH_MODE="$2"; shift 2 ;;
    --auth-token) AUTH_TOKEN="$2"; shift 2 ;;
    --oidc-introspection-url) OIDC_INTROSPECTION_URL="$2"; shift 2 ;;
    --oidc-client-id) OIDC_CLIENT_ID="$2"; shift 2 ;;
    --oidc-client-secret) OIDC_CLIENT_SECRET="$2"; shift 2 ;;
    --oidc-issuer) OIDC_ISSUER="$2"; shift 2 ;;
    --oidc-audience) OIDC_AUDIENCE="$2"; shift 2 ;;
    --oidc-allowed-audiences) OIDC_ALLOWED_AUDIENCES="$2"; shift 2 ;;
    --oidc-required-scope) OIDC_REQUIRED_SCOPE="$2"; shift 2 ;;
    --oshift-url) OSHIFT_URL="$2"; shift 2 ;;

    --k8s-install) K8S_AUTO_INSTALL="true"; shift ;;
    --no-k8s-install) K8S_AUTO_INSTALL="false"; shift ;;
    --k8s-version-series) K8S_VERSION_SERIES="$2"; shift 2 ;;
    --k8s-pod-cidr) K8S_POD_CIDR="$2"; shift 2 ;;
    --k8s-service-cidr) K8S_SERVICE_CIDR="$2"; shift 2 ;;
    --kubeconfig) K8S_KUBECONFIG_SRC="$2"; shift 2 ;;

    --gpu-auto) GPU_AUTO="true"; shift ;;
    --no-gpu) GPU_AUTO="false"; shift ;;
    --enable-nvidia-device-plugin) ENABLE_NVIDIA_DEVICE_PLUGIN="true"; shift ;;
    --disable-nvidia-device-plugin) ENABLE_NVIDIA_DEVICE_PLUGIN="false"; shift ;;

    --enable-auto-update) ENABLE_AUTO_UPDATE="true"; shift ;;
    --disable-auto-update) ENABLE_AUTO_UPDATE="false"; shift ;;

    --repo-url) REPO_URL="$2"; shift 2 ;;
    --branch) REPO_BRANCH="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD="true"; shift ;;

    --dry-run) DRY_RUN="true"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac

done

# ---------------------------
# Core checks
# ---------------------------
require_root

case "$AUTH_MODE" in
  token|oidc|off) ;;
  *) die "Invalid --auth-mode: $AUTH_MODE (expected token|oidc|off)" ;;
esac

if [[ "$AUTH_MODE" == "token" && -z "$AUTH_TOKEN" ]]; then
  log_warn "NMC_AUTH_TOKEN is empty; token auth will be disabled"
fi
if [[ "$AUTH_MODE" == "oidc" && -z "$OIDC_INTROSPECTION_URL" ]]; then
  log_warn "NMC_OIDC_INTROSPECTION_URL is empty; OIDC auth will fail closed"
fi

if ! command -v systemctl >/dev/null 2>&1; then
  die "systemctl not found. This script expects systemd."
fi

if [[ -z "$BUILD_USER" ]]; then
  if [[ -n "${SUDO_USER:-}" && "${SUDO_USER:-}" != "root" ]]; then
    BUILD_USER="$SUDO_USER"
  else
    BUILD_USER="$RUN_USER"
  fi
fi

if [[ -z "$BUILD_GROUP" ]]; then
  if id -u "$BUILD_USER" >/dev/null 2>&1; then
    BUILD_GROUP="$(id -gn "$BUILD_USER")"
  else
    BUILD_GROUP="$RUN_GROUP"
  fi
fi

if [[ ! -d "$INSTALL_DIR" ]]; then
  run_root mkdir -p "$INSTALL_DIR"
fi

# Normalize INSTALL_DIR for consistent systemd paths
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"

# Ensure run user/group exist
if ! getent group "$RUN_GROUP" >/dev/null 2>&1; then
  log_info "Creating group $RUN_GROUP"
  run_root groupadd --system "$RUN_GROUP"
fi

if ! id -u "$RUN_USER" >/dev/null 2>&1; then
  log_info "Creating user $RUN_USER"
  run_root useradd --system --home "$INSTALL_DIR" --shell /usr/sbin/nologin --gid "$RUN_GROUP" "$RUN_USER"
fi

if ! id -u "$BUILD_USER" >/dev/null 2>&1; then
  die "Build user '$BUILD_USER' does not exist. Create it or pass --build-user."
fi

# Validate repo (or clone)
if [[ -n "$REPO_URL" ]]; then
  if [[ -d "$INSTALL_DIR/.git" ]]; then
    log_info "Using existing repo at $INSTALL_DIR"
    REPO_DIR="$INSTALL_DIR"
  else
    log_info "Cloning repo into $INSTALL_DIR"
    run_root chown -R "$BUILD_USER:$BUILD_GROUP" "$INSTALL_DIR"
    run_as_build_user git clone -b "$REPO_BRANCH" "$REPO_URL" "$INSTALL_DIR"
    REPO_DIR="$INSTALL_DIR"
  fi
else
  if [[ -f "$REPO_DIR/nmc_server/CMakeLists.txt" ]]; then
    INSTALL_DIR="$REPO_DIR"
  else
    die "Run this script from the nmc repo root or provide --repo-url"
  fi
fi

BUILD_DIR="$REPO_DIR/nmc_server/build"

# Ensure build user can read repo; if not, try switching to repo owner.
if ! run_as_build_user test -r "$REPO_DIR/nmc_server/CMakeLists.txt"; then
  repo_owner="$(stat -c %U "$REPO_DIR" 2>/dev/null || true)"
  if [[ -n "$repo_owner" && "$repo_owner" != "$BUILD_USER" ]]; then
    log_warn "Build user '$BUILD_USER' lacks access to repo; switching to '$repo_owner'"
    if id -u "$repo_owner" >/dev/null 2>&1; then
      BUILD_USER="$repo_owner"
      BUILD_GROUP="$(id -gn "$BUILD_USER")"
    else
      die "Repo owner '$repo_owner' is not a valid user"
    fi
  else
    die "Build user '$BUILD_USER' lacks access to repo. Use --build-user."
  fi
fi

if ! run_as_user test -x "$REPO_DIR"; then
  log_warn "Service user '$RUN_USER' cannot access repo path; switching service user to '$BUILD_USER'"
  RUN_USER="$BUILD_USER"
  RUN_GROUP="$BUILD_GROUP"
fi

# ---------------------------
# Install base dependencies
# ---------------------------
log_info "Installing base dependencies"
apt_install \
  build-essential cmake git pkg-config libssl-dev libspdlog-dev \
  autoconf automake libtool ca-certificates curl gpg lsb-release \
  pciutils

# ---------------------------
# GPU detection and setup
# ---------------------------

detect_gpu() {
  if command -v nvidia-smi >/dev/null 2>&1 || [[ -d /proc/driver/nvidia/gpus ]]; then
    GPU_VENDOR="nvidia"
    return
  fi

  if command -v lspci >/dev/null 2>&1; then
    local pci
    pci="$(lspci -nn | tr 'A-Z' 'a-z')"
    if echo "$pci" | grep -q "nvidia"; then
      GPU_VENDOR="nvidia"
      return
    fi
    if echo "$pci" | grep -q "amd\|advanced micro devices\|ati"; then
      GPU_VENDOR="amd"
      return
    fi
    if echo "$pci" | grep -q "intel"; then
      GPU_VENDOR="intel"
      return
    fi
  fi

  if [[ -d /dev/dri ]]; then
    GPU_VENDOR="intel"
    return
  fi
}

install_opencl_cuda() {
  local vendor="$1"
  log_info "GPU detected: $vendor"

  case "$vendor" in
    nvidia)
      log_info "Installing NVIDIA drivers and CUDA toolkit"
      apt_install ubuntu-drivers-common || true
      run_root ubuntu-drivers autoinstall || log_warn "ubuntu-drivers autoinstall failed"
      apt_install nvidia-cuda-toolkit || log_warn "nvidia-cuda-toolkit install failed"
      apt_install ocl-icd-opencl-dev clinfo || log_warn "OpenCL tools install failed"
      log_warn "A reboot may be required for NVIDIA drivers to load."
      ;;
    amd)
      log_info "Installing OpenCL tooling for AMD"
      apt_install ocl-icd-opencl-dev clinfo mesa-opencl-icd || log_warn "OpenCL tools install failed"
      ;;
    intel)
      log_info "Installing OpenCL tooling for Intel"
      apt_install ocl-icd-opencl-dev clinfo intel-opencl-icd || log_warn "OpenCL tools install failed"
      ;;
    *)
      log_info "No GPU detected; skipping OpenCL/CUDA setup"
      ;;
  esac
}

if [[ "$GPU_AUTO" == "true" ]]; then
  detect_gpu
  if [[ "$GPU_VENDOR" != "none" ]]; then
    install_opencl_cuda "$GPU_VENDOR"
  else
    log_info "GPU auto-detect found no GPU"
  fi
else
  log_info "GPU setup disabled"
fi

# ---------------------------
# Kubernetes detection and install
# ---------------------------

k8s_available() {
  if ! command -v kubectl >/dev/null 2>&1; then
    return 1
  fi
  if [[ -n "$K8S_KUBECONFIG_SRC" && -f "$K8S_KUBECONFIG_SRC" ]]; then
    kubectl --kubeconfig="$K8S_KUBECONFIG_SRC" get nodes >/dev/null 2>&1
    return $?
  fi
  if [[ -f /etc/kubernetes/admin.conf ]]; then
    kubectl --kubeconfig=/etc/kubernetes/admin.conf get nodes >/dev/null 2>&1
    return $?
  fi
  kubectl get nodes >/dev/null 2>&1
}

detect_k8s_series() {
  if [[ -n "$K8S_VERSION_SERIES" ]]; then
    echo "$K8S_VERSION_SERIES"
    return
  fi
  if command -v curl >/dev/null 2>&1; then
    local stable
    stable="$(curl -fsSL https://dl.k8s.io/release/stable.txt || true)"
    if [[ -n "$stable" ]]; then
      stable="${stable#v}"
      echo "v${stable%.*}"
      return
    fi
  fi
  echo "v1.30"
}

install_kubernetes() {
  log_info "Installing Kubernetes via kubeadm"

  apt_install containerd

  run_root mkdir -p /etc/modules-load.d /etc/sysctl.d
  cat <<EOF | run_root tee /etc/modules-load.d/k8s.conf >/dev/null
overlay
br_netfilter
EOF
  run_root modprobe overlay || true
  run_root modprobe br_netfilter || true

  cat <<EOF | run_root tee /etc/sysctl.d/k8s.conf >/dev/null
net.bridge.bridge-nf-call-iptables = 1
net.bridge.bridge-nf-call-ip6tables = 1
net.ipv4.ip_forward = 1
EOF
  run_root sysctl --system

  run_root swapoff -a || true
  if [[ -f /etc/fstab ]]; then
    run_root sed -i.bak '/\sswap\s/s/^/#/' /etc/fstab
  fi

  run_root mkdir -p /etc/apt/keyrings
  local series
  series="$(detect_k8s_series)"
  log_info "Using Kubernetes repo series: $series"

  if retry run_root bash -c "curl -fsSL https://pkgs.k8s.io/core:/stable:/$series/deb/Release.key | gpg --dearmor -o /etc/apt/keyrings/kubernetes-apt-keyring.gpg"; then
    run_root bash -c "echo 'deb [signed-by=/etc/apt/keyrings/kubernetes-apt-keyring.gpg] https://pkgs.k8s.io/core:/stable:/$series/deb/ /' > /etc/apt/sources.list.d/kubernetes.list"
  else
    log_warn "Failed to configure pkgs.k8s.io repo; falling back to apt.kubernetes.io"
    retry run_root bash -c "curl -fsSL https://packages.cloud.google.com/apt/doc/apt-key.gpg | gpg --dearmor -o /etc/apt/keyrings/kubernetes-archive-keyring.gpg"
    run_root bash -c "echo 'deb [signed-by=/etc/apt/keyrings/kubernetes-archive-keyring.gpg] https://apt.kubernetes.io/ kubernetes-xenial main' > /etc/apt/sources.list.d/kubernetes.list"
  fi

  APT_UPDATED="false"
  apt_install kubelet kubeadm kubectl
  run_root apt-mark hold kubelet kubeadm kubectl

  run_root mkdir -p /etc/containerd
  if [[ ! -f /etc/containerd/config.toml ]]; then
    run_root containerd config default | run_root tee /etc/containerd/config.toml >/dev/null
  fi
  run_root sed -i 's/SystemdCgroup = false/SystemdCgroup = true/' /etc/containerd/config.toml
  run_root systemctl enable --now containerd

  run_root systemctl enable --now kubelet

  if [[ ! -f /etc/kubernetes/admin.conf ]]; then
    log_info "Initializing single-node Kubernetes control plane"
    run_root kubeadm init --pod-network-cidr="$K8S_POD_CIDR" --service-cidr="$K8S_SERVICE_CIDR"
  else
    log_info "Kubernetes already initialized"
  fi

  log_info "Applying Flannel CNI"
  retry run_root kubectl --kubeconfig=/etc/kubernetes/admin.conf apply -f "$FLANNEL_MANIFEST_URL"

  run_root kubectl --kubeconfig=/etc/kubernetes/admin.conf taint nodes --all node-role.kubernetes.io/control-plane- >/dev/null 2>&1 || true
  run_root kubectl --kubeconfig=/etc/kubernetes/admin.conf taint nodes --all node-role.kubernetes.io/master- >/dev/null 2>&1 || true

  log_info "Waiting for node readiness"
  run_root kubectl --kubeconfig=/etc/kubernetes/admin.conf wait --for=condition=Ready node --all --timeout=120s || true
}

configure_kubeconfig_for_nmc() {
  local src="$1"
  local dst="/etc/nmc/kubeconfig"
  run_root mkdir -p /etc/nmc
  run_root install -m 600 -o "$RUN_USER" -g "$RUN_GROUP" "$src" "$dst"
  log_info "Kubeconfig installed at $dst"
}

if k8s_available; then
  log_info "Kubernetes detected and available"
else
  if [[ "$K8S_AUTO_INSTALL" == "true" ]]; then
    install_kubernetes
  else
    die "Kubernetes not available and --no-k8s-install was set"
  fi
fi

if [[ -n "$K8S_KUBECONFIG_SRC" ]]; then
  [[ -f "$K8S_KUBECONFIG_SRC" ]] || die "Kubeconfig not found: $K8S_KUBECONFIG_SRC"
  configure_kubeconfig_for_nmc "$K8S_KUBECONFIG_SRC"
elif [[ -f /etc/kubernetes/admin.conf ]]; then
  configure_kubeconfig_for_nmc /etc/kubernetes/admin.conf
else
  die "No kubeconfig available for nmc_server"
fi

# Optionally install NVIDIA device plugin if GPU present
if [[ "$GPU_AUTO" == "true" && "$GPU_VENDOR" == "nvidia" && "$ENABLE_NVIDIA_DEVICE_PLUGIN" == "true" ]]; then
  if [[ -f /etc/kubernetes/admin.conf ]]; then
    log_info "Applying NVIDIA device plugin"
    run_root kubectl --kubeconfig=/etc/kubernetes/admin.conf apply -f "$NVIDIA_DEVICE_PLUGIN_URL" || \
      log_warn "Failed to apply NVIDIA device plugin"
  else
    log_warn "Skipping NVIDIA device plugin (no local /etc/kubernetes/admin.conf)"
  fi
fi

# ---------------------------
# Build nmc_server
# ---------------------------
if [[ "$SKIP_BUILD" == "false" ]]; then
  log_info "Building nmc_server (build user: $BUILD_USER)"
  run_root mkdir -p "$BUILD_DIR"
  run_root chown -R "$BUILD_USER:$BUILD_GROUP" "$BUILD_DIR"
  run_as_build_user cmake -S "$REPO_DIR/nmc_server" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  run_as_build_user cmake --build "$BUILD_DIR" -- -j"$(nproc)"
  run_root chmod -R a+rX "$BUILD_DIR"
  run_root mkdir -p "$BUILD_DIR/logs"
  run_root chown -R "$RUN_USER:$RUN_GROUP" "$BUILD_DIR/logs"
else
  log_info "Skipping build step"
fi

# ---------------------------
# Create systemd service + env
# ---------------------------
log_info "Configuring systemd service"

run_root mkdir -p /etc/nmc

cat <<EOF | run_root tee /etc/nmc/nmc.env >/dev/null
NMC_AUTH_MODE=$AUTH_MODE
NMC_AUTH_TOKEN=$AUTH_TOKEN
NMC_OSHIFT_API_URL=$OSHIFT_URL
NMC_DOCS_ENABLED=$DOCS_ENABLED
NMC_MAX_BODY_BYTES=$MAX_BODY_BYTES
NMC_LOG_BODY_BYTES=$MAX_LOG_BODY_BYTES
NMC_OIDC_INTROSPECTION_URL=$OIDC_INTROSPECTION_URL
NMC_OIDC_CLIENT_ID=$OIDC_CLIENT_ID
NMC_OIDC_CLIENT_SECRET=$OIDC_CLIENT_SECRET
NMC_OIDC_ISSUER=$OIDC_ISSUER
NMC_OIDC_AUDIENCE=$OIDC_AUDIENCE
NMC_OIDC_ALLOWED_AUDIENCES=$OIDC_ALLOWED_AUDIENCES
NMC_OIDC_REQUIRED_SCOPE=$OIDC_REQUIRED_SCOPE
EOF

cat <<EOF | run_root tee /etc/systemd/system/nmc-server.service >/dev/null
[Unit]
Description=NMC Server
After=network-online.target
Wants=network-online.target

[Service]
User=$RUN_USER
Group=$RUN_GROUP
WorkingDirectory=$BUILD_DIR
EnvironmentFile=-/etc/nmc/nmc.env
ExecStart=$BUILD_DIR/nmc_server --port $PORT --kubeconfig /etc/nmc/kubeconfig
Restart=on-failure
RestartSec=5
NoNewPrivileges=true
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

run_root systemctl daemon-reload
run_root systemctl enable --now nmc-server.service

# ---------------------------
# Auto-update timer
# ---------------------------
if [[ "$ENABLE_AUTO_UPDATE" == "true" ]]; then
  if [[ ! -d "$REPO_DIR/.git" ]]; then
    log_warn "Auto-update enabled but repo is not a git checkout; skipping update timer"
  else
    log_info "Installing auto-update timer"

  cat <<EOF | run_root tee /etc/nmc/nmc-update.conf >/dev/null
REPO_DIR=$REPO_DIR
REPO_BRANCH=$REPO_BRANCH
UPDATE_USER=$BUILD_USER
EOF

  cat <<'EOF' | run_root tee /usr/local/bin/nmc-update >/dev/null
#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

CONFIG=/etc/nmc/nmc-update.conf
if [[ -f "$CONFIG" ]]; then
  # shellcheck disable=SC1090
  source "$CONFIG"
else
  echo "Update config not found: $CONFIG" >&2
  exit 1
fi

: "${REPO_DIR:?Missing REPO_DIR}"
: "${REPO_BRANCH:=main}"
: "${UPDATE_USER:=nmc}"

run_as_user() {
  if [[ "$(id -u)" -eq 0 && "$UPDATE_USER" != "root" ]]; then
    if command -v sudo >/dev/null 2>&1; then
      sudo -u "$UPDATE_USER" -H -- "$@"
    else
      runuser -u "$UPDATE_USER" -- "$@"
    fi
  else
    "$@"
  fi
}

cd "$REPO_DIR"

if [[ -n "$(git status --porcelain)" ]]; then
  echo "Working tree dirty; skipping update" >&2
  exit 0
fi

git fetch origin "$REPO_BRANCH"
LOCAL="$(git rev-parse HEAD)"
REMOTE="$(git rev-parse "origin/$REPO_BRANCH")"

if [[ "$LOCAL" == "$REMOTE" ]]; then
  exit 0
fi

git checkout "$REPO_BRANCH"
git pull --ff-only origin "$REPO_BRANCH"

run_as_user cmake -S "$REPO_DIR/nmc_server" -B "$REPO_DIR/nmc_server/build" -DCMAKE_BUILD_TYPE=Release
run_as_user cmake --build "$REPO_DIR/nmc_server/build" -- -j"$(nproc)"

systemctl restart nmc-server.service
EOF

  run_root chmod 755 /usr/local/bin/nmc-update

  cat <<'EOF' | run_root tee /etc/systemd/system/nmc-update.service >/dev/null
[Unit]
Description=Update and rebuild NMC server

[Service]
Type=oneshot
ExecStart=/usr/local/bin/nmc-update
EOF

  cat <<'EOF' | run_root tee /etc/systemd/system/nmc-update.timer >/dev/null
[Unit]
Description=Daily NMC server update

[Timer]
OnCalendar=daily
Persistent=true

[Install]
WantedBy=timers.target
EOF

    run_root systemctl daemon-reload
    run_root systemctl enable --now nmc-update.timer
  fi
else
  log_info "Auto-update timer disabled"
fi

# ---------------------------
# Summary
# ---------------------------
log_info "Deployment complete"
log_info "NMC server: systemctl status nmc-server"
log_info "Docs: http://<host>:$PORT/docs (if enabled)"
log_info "Config: /etc/nmc/nmc.env"
log_info "Kubeconfig: /etc/nmc/kubeconfig"
