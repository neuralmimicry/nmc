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
