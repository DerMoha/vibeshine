#!/usr/bin/env bash
# Collect prerequisite/runtime metadata. No packages, services, firewall rules,
# configuration, keys, pairings, or security settings are changed or printed.
set -uo pipefail
export LC_ALL=C
section() { printf '\n== %s ==\n' "$*"; }
inspect() {
  if command -v "$1" >/dev/null 2>&1; then
    if ! "$@"; then printf '(command failed: %s)\n' "$*"; fi
  else
    printf '(not installed: %s)\n' "$1"
  fi
}
[[ $(uname -s) == Linux ]] || { printf 'Run this report on the Linux target.\n' >&2; exit 1; }
section 'Distribution and kernel'
if [[ -r /etc/os-release ]]; then
  # shellcheck source=/dev/null
  source /etc/os-release
  printf 'ID=%s VERSION_ID=%s ID_LIKE=%s\n' "${ID:-}" "${VERSION_ID:-}" "${ID_LIKE:-}"
fi
inspect uname -r
inspect uname -m
kernel=$(uname -r)
section 'Running-kernel build tree'
inspect readlink -f "/lib/modules/$kernel/build"
if [[ -f "/lib/modules/$kernel/build/Makefile" ]]; then
  printf 'Makefile exists\n'
else
  printf 'Makefile missing\n'
fi
if [[ -r "/lib/modules/$kernel/build/include/config/kernel.release" ]]; then
  head -n 1 "/lib/modules/$kernel/build/include/config/kernel.release"
fi
section 'Prerequisite versions'
for tool in gcc g++ cmake ninja dkms; do inspect "$tool" --version; done
if command -v rpm >/dev/null 2>&1; then
  inspect rpm -q "kernel-devel-$kernel" plasma-workspace plasma-login-manager sddm dkms vibeshine sunshine apollo vibepollo
fi
section 'Display manager and active seat0 session'
inspect systemctl show display-manager.service --property=Id --property=ActiveState
session=$(loginctl show-seat seat0 --property=ActiveSession --value 2>/dev/null || true)
if [[ "$session" =~ ^[A-Za-z0-9_-]+$ ]]; then
  inspect loginctl show-session "$session" --property=Type --property=Desktop --property=Class \
    --property=Seat --property=Active --property=Remote --property=Service
else
  printf 'No active seat0 session detected\n'
fi
section 'Security modes'
inspect mokutil --sb-state
inspect getenforce
section 'GPU and encoder discovery'
inspect lspci -nn
if command -v nvidia-smi >/dev/null 2>&1; then
  inspect nvidia-smi --query-gpu=name,driver_version --format=csv,noheader
fi
if command -v vainfo >/dev/null 2>&1 && command -v timeout >/dev/null 2>&1; then
  inspect timeout 15 vainfo
else
  printf 'vainfo unavailable; hardware H.264 encoding remains unverified\n'
fi
section 'Virtual-display driver'
inspect dkms status -k "$kernel"
inspect modinfo -k "$kernel" -F version vibeshine_drm
if [[ -r /sys/module/vibeshine_drm/version ]]; then
  printf 'Loaded DRM version: '; head -n 1 /sys/module/vibeshine_drm/version
fi
section 'Machine service status'
inspect systemctl show vibeshine-session-controller.service vibeshine.service \
  --property=Id --property=ActiveState --property=SubState --property=UnitFileState --property=Result
section 'Privileged executable metadata'
for path in /usr/bin/vibeshine /usr/libexec/vibeshine/vibeshine-session-exec \
  /usr/libexec/vibeshine/vibeshine-host /usr/libexec/vibeshine/vibeshine-session-broker; do
  if [[ -e "$path" ]]; then
    inspect stat -c '%U:%G:%a:%h:%F %n' "$path"
    inspect getcap -n "$path"
  fi
done
section 'Firewall status'
inspect systemctl is-active firewalld
if command -v firewall-cmd >/dev/null 2>&1; then
  inspect firewall-cmd --get-active-zones
  inspect firewall-cmd --query-service=vibeshine
fi
printf '\nReport complete. Review it before sharing. It does not prove pairing or streaming readiness.\n'
