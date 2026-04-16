# NMC Ansible Deployment

`ansible/deploy.yml` is the Ansible equivalent of `deploy.sh` for provisioning and running `nmc_server` on a host that uses `systemd`.

## What The Playbook Does

The playbook can:
- install build and runtime dependencies
- build `nmc_server`
- install the `nmc_server` systemd unit
- install an optional auto-update timer
- optionally bootstrap Kubernetes prerequisites
- optionally detect GPU hardware and install related tooling
- optionally install and run a local Tracey sidecar
- render `/etc/nmc/nmc.env` and the associated service templates

## Run

```bash
ansible-playbook -i "localhost," -c local ansible/deploy.yml -K
```

## Common Overrides

```bash
ansible-playbook -i "localhost," -c local ansible/deploy.yml -K \
  -e nmc_install_dir=/opt/nmc \
  -e nmc_build_user=$USER \
  -e nmc_build_group=$(id -gn) \
  -e nmc_auth_mode=token \
  -e nmc_auth_token=changeme \
  -e nmc_gail_base_url=https://gail.neuralmimicry.ai \
  -e nmc_gail_api_token=changeme \
  -e nmc_docs_enabled=true
```

## Repo Source Options

Use an existing checkout at `nmc_install_dir` (default), or clone/update from git:

```bash
ansible-playbook -i "localhost," -c local ansible/deploy.yml -K \
  -e nmc_repo_url=https://github.com/your-org/nmc.git \
  -e nmc_repo_branch=main \
  -e nmc_install_dir=/opt/nmc
```

## Variable Groups

### Core

- `nmc_install_dir`
- `nmc_repo_url`
- `nmc_repo_branch`
- `nmc_skip_build`
- `nmc_run_user`
- `nmc_run_group`
- `nmc_build_user`
- `nmc_build_group`
- `nmc_port`
- `nmc_docs_enabled`
- `nmc_max_body_bytes`
- `nmc_log_body_bytes`
- `nmc_oshift_api_url`

### Auth

- `nmc_auth_mode`
- `nmc_auth_token`
- `nmc_oidc_introspection_url`
- `nmc_oidc_client_id`
- `nmc_oidc_client_secret`
- `nmc_oidc_issuer`
- `nmc_oidc_audience`
- `nmc_oidc_allowed_audiences`
- `nmc_oidc_required_scope`

### Gail

- `nmc_gail_base_url`
- `nmc_gail_api_token`

### Kubernetes

- `nmc_k8s_auto_install`
- `nmc_k8s_version_series`
- `nmc_k8s_pod_cidr`
- `nmc_k8s_service_cidr`
- `nmc_kubeconfig_src`
- `nmc_flannel_manifest_url`
- `nmc_nvidia_device_plugin_url`
- `nmc_k8s_api_wait_seconds`
- `nmc_k8s_restart_on_failure`

### GPU

- `nmc_gpu_auto`
- `nmc_enable_nvidia_device_plugin`
- `nmc_nvidia_preserve_driver_if_present`

### Updates

- `nmc_enable_auto_update`

### Tracey sidecar

- `nmc_tracey_sidecar_enabled`
- `nmc_tracey_bin`
- `nmc_tracey_repo_dir`
- `nmc_tracey_repo_url`
- `nmc_tracey_repo_branch`
- `nmc_tracey_build_if_missing`
- `nmc_tracey_agent_id`
- `nmc_tracey_local_status_addr`
- `nmc_tracey_status_listen_addr`
- `nmc_tracey_status_public_addr`
- `nmc_tracey_discovery_bind_addr`
- `nmc_tracey_discovery_broadcast_addr`
- `nmc_tracey_discovery_shared_key`
- `nmc_tracey_config_path`
- `nmc_tracey_state_dir`
- `nmc_tracey_cve_root`

## Recruited Node Auto-Configuration

`ansible/recruited-node.yml` is used by:
- `nmc node recruit --auto-configure`
- `POST /node/recruit` with `auto_configure=true`

Behavior:
- Ansible become is enabled by default.
- Disable become with `--no-become` on the CLI or `ansible_become=false` in the API payload.
- If `sudo_password` or `become_password` is supplied, the recruiter maps it to `ansible_become_password` for non-interactive privilege escalation.

Example:

```bash
ansible-playbook -i "192.168.1.60," ansible/recruited-node.yml -u ubuntu \
  -e '{"nmc_enable_apps":true,"nmc_enable_podman":true,"nmc_tenant_id":"acme","nmc_tenant_environment":"prod"}'
```

Key vars:
- capability toggles: `nmc_enable_apps`, `nmc_enable_vm`, `nmc_enable_podman`, `nmc_enable_kubernetes`, `nmc_enable_openstack`
- tenant metadata: `nmc_tenant_id`, `nmc_tenant_name`, `nmc_tenant_environment`
- node metadata: `nmc_node_host`, `nmc_node_name`, `nmc_node_region`, `nmc_node_type`
- capability source list: `nmc_requested_capabilities`
- optional Gail bootstrap: `nmc_gail_base_url`, `nmc_gail_api_token`
- optional Tracey hints: `nmc_tracey_agent_id`, `nmc_tracey_status_addr`
