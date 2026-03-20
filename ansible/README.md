# NMC Ansible Deployment

This playbook is the Ansible equivalent of `deploy.sh`.

## Run

```bash
ansible-playbook -i "localhost," -c local ansible/deploy.yml -K
```

## Common overrides

```bash
ansible-playbook -i "localhost," -c local ansible/deploy.yml -K \
  -e nmc_install_dir=/opt/nmc \
  -e nmc_build_user=$USER \
  -e nmc_build_group=$(id -gn) \
  -e nmc_auth_mode=token \
  -e nmc_auth_token=changeme
```

## Repo source options

Use an existing checkout at `nmc_install_dir` (default), or clone/update from git:

```bash
ansible-playbook -i "localhost," -c local ansible/deploy.yml -K \
  -e nmc_repo_url=https://github.com/your-org/nmc.git \
  -e nmc_repo_branch=main \
  -e nmc_install_dir=/opt/nmc
```

## Variables

The playbook includes variables equivalent to deploy script flags/environment variables:

- Core: `nmc_install_dir`, `nmc_run_user`, `nmc_run_group`, `nmc_build_user`, `nmc_build_group`, `nmc_port`, `nmc_skip_build`
- Auth: `nmc_auth_mode`, `nmc_auth_token`, `nmc_oidc_*`
- Kubernetes: `nmc_k8s_auto_install`, `nmc_k8s_version_series`, `nmc_k8s_pod_cidr`, `nmc_k8s_service_cidr`, `nmc_kubeconfig_src`, `nmc_k8s_api_wait_seconds`, `nmc_k8s_restart_on_failure`
- GPU: `nmc_gpu_auto`, `nmc_enable_nvidia_device_plugin`, `nmc_nvidia_preserve_driver_if_present`
- Auto-update: `nmc_enable_auto_update`, `nmc_repo_url`, `nmc_repo_branch`
- Tracey sidecar: `nmc_tracey_sidecar_enabled`, `nmc_tracey_bin`, `nmc_tracey_repo_dir`, `nmc_tracey_build_if_missing`, `nmc_tracey_agent_id`, `nmc_tracey_local_status_addr`, `nmc_tracey_status_listen_addr`, `nmc_tracey_status_public_addr`, `nmc_tracey_discovery_bind_addr`, `nmc_tracey_discovery_broadcast_addr`, `nmc_tracey_discovery_shared_key`

## Recruited node auto-configuration

`ansible/recruited-node.yml` is used by `nmc node recruit --auto-configure` (or `POST /node/recruit` with `auto_configure=true`) to prepare a newly recruited host for tenant workloads.
By default, auto-configure runs with Ansible become enabled; disable with `--no-become` (CLI) or `ansible_become=false` (API).
If `sudo_password`/`become_password` is supplied, the recruiter maps it to `ansible_become_password` for non-interactive privilege escalation.

Example:

```bash
ansible-playbook -i "192.168.1.60," ansible/recruited-node.yml -u ubuntu \
  -e '{"nmc_enable_apps":true,"nmc_enable_podman":true,"nmc_tenant_id":"acme","nmc_tenant_environment":"prod"}'
```

Key vars:
- Capability toggles: `nmc_enable_apps`, `nmc_enable_vm`, `nmc_enable_podman`, `nmc_enable_kubernetes`
- Tenant metadata: `nmc_tenant_id`, `nmc_tenant_name`, `nmc_tenant_environment`
- Node metadata: `nmc_node_host`, `nmc_node_name`, `nmc_node_region`, `nmc_node_type`
- Optional Tracey hints: `nmc_tracey_agent_id`, `nmc_tracey_status_addr`
