#!/usr/bin/env bash
# Isolated package, kernel and session fixtures; no real packages/services change.
set -euo pipefail
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/../../../.." && pwd -P)
source "$repo/scripts/linux_distribution.sh"
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
source "$repo/scripts/linux_install.sh"
trap 'rm -rf "$fixture"' EXIT
calls="$fixture/calls"
fixture_identity='vibeshine x86_64'
fixture_payload=$'/nonexistent-vibeshine-fixture\t33261'
conflicting=0; installed_version=''; candidate_version='0:2.0.0-1'; fixture_comparison=1
package_result=0; create_headers=0
rpm() {
  case "$1" in
    -qp)
      if [[ "$*" == *FILENAMES* ]]; then printf '%s' "$fixture_payload";
      elif [[ "$*" == *EPOCHNUM* ]]; then printf '%s' "$candidate_version";
      else printf '%s' "$fixture_identity"; fi ;;
    -qf) [[ -n "${fixture_owner:-}" ]] || return 1; printf '%s' "$fixture_owner" ;;
    --eval) printf '%s' "$fixture_comparison" ;;
    -q)
      if [[ "$*" == *EPOCHNUM* ]]; then
        [[ -n "$installed_version" ]] || return 1
        printf '%s' "$installed_version"
      else
        ((conflicting))
      fi ;;
    *) return 1 ;;
  esac
}
dnf() {
  printf '%s\n' "$*" >> "$calls"
  if ((create_headers)); then
    mkdir -p "$fixture/modules/$kernel_release/build"
    printf 'kernelrelease:\n\t@echo %s\n' "$kernel_release" > "$fixture/modules/$kernel_release/build/Makefile"
  fi
  return "$package_result"
}
pacman() { :; }
for entry in 'nobara|fedora|fedora|dnf' 'fedora||fedora|dnf' 'cachyos|arch|arch|pacman' 'custom|rhel fedora|fedora|dnf' 'custom|arch linux|arch|pacman'; do
  IFS='|' read -r id like expected manager <<< "$entry"
  printf 'ID="%s"\nID_LIKE="%s"\nVERSION_ID="44"\n' "$id" "$like" > "$fixture/os-release"
  detect_linux_distribution "$fixture/os-release"
  [[ "$os_family" == "$expected" && "$os_version" == 44 ]]
  select_package_manager "$fixture/os-release"
  [[ "$package_manager" == "$manager" ]]
done
printf 'ID=nobara\nID_LIKE=fedora\n' > "$fixture/os-release"
if (skip_checks=1; select_package_manager "$fixture/os-release"); then exit 1; fi
if (requested_version=2.0.0; select_package_manager "$fixture/os-release"); then exit 1; fi
package_manager=dnf
local_package="$fixture/vibeshine.rpm"
touch "$local_package"
parse_args --yes
install_rpm_package
grep -Fx "install -y -- $(readlink -f -- "$local_package")" "$calls"
rm "$calls"
for fixture_identity in 'sunshine x86_64' 'vibeshine aarch64' $'vibeshine x86_64\nextra'; do
  if (install_rpm_package); then exit 1; fi
  [[ ! -e "$calls" ]]
done
fixture_identity='vibeshine x86_64'; conflicting=1
if (install_rpm_package); then exit 1; fi
[[ ! -e "$calls" ]]
conflicting=0; installed_version='0:2.0.1-1'; fixture_comparison=-1
if (install_rpm_package); then exit 1; fi
[[ ! -e "$calls" ]]
fixture_comparison=invalid
if (install_rpm_package); then exit 1; fi
[[ ! -e "$calls" ]]
fixture_comparison=0; installed_version=$candidate_version
install_rpm_package
rm "$calls"
fixture_comparison=1; installed_version='0:1.19.0-1'
install_rpm_package
rm "$calls"
package_result=1
if (install_rpm_package); then exit 1; fi
package_result=0; installed_version=''; rm "$calls"

touch "$fixture/unowned"
fixture_payload="$fixture/unowned"$'\t33261'
if (install_rpm_package); then exit 1; fi
[[ ! -e "$calls" ]]
fixture_owner=unrelated
if (install_rpm_package); then exit 1; fi
[[ ! -e "$calls" ]]
fixture_owner=vibeshine
install_rpm_package
rm "$calls"
fixture_owner=''
fixture_payload=$'/nonexistent-vibeshine-fixture\t33261'

# Never substitute a different kernel's development tree.
kernel_release=7.2.9-200.nobara.fc44.x86_64
create_headers=1
install_rpm_kernel_headers "$fixture/modules"
grep -Fx "install -y kernel-devel-$kernel_release" "$calls"
rm "$calls"; create_headers=0
install_rpm_kernel_headers "$fixture/modules"
[[ ! -e "$calls" ]]
printf 'kernelrelease:\n\t@echo wrong-kernel\n' > "$fixture/modules/$kernel_release/build/Makefile"
if (install_rpm_kernel_headers "$fixture/modules"); then exit 1; fi
[[ ! -e "$calls" ]]
rm "$fixture/modules/$kernel_release/build/Makefile"
if (install_rpm_kernel_headers "$fixture/modules"); then exit 1; fi
rm "$calls"; package_result=1
if (install_rpm_kernel_headers "$fixture/modules"); then exit 1; fi
package_result=0; rm "$calls"

# Logind, rather than sudo's environment, establishes the active local session.
# Override only the file existence check in the copied function for macOS tests.
eval "$(declare -f check_fedora_desktop | sed 's@\[\[ -x /usr/bin/kwin_wayland \]\]@true@')"
plasma_version=6.7.5; login_manager=plasmalogin.service; session_type=wayland
session_remote=no; session_service=plasmalogin; session_desktop=KDE
session_class=user; active_session=3
rpm() { printf '%s' "$plasma_version"; }
systemctl() {
  if [[ "$1" == show ]]; then printf '%s' "$login_manager"; else return 0; fi
}
loginctl() {
  if [[ "$1" == show-seat ]]; then printf '%s' "$active_session"; return; fi
  printf 'Type=%s\nActive=yes\nSeat=seat0\nRemote=%s\nClass=%s\nService=%s\nDesktop=%s\n' \
    "$session_type" "$session_remote" "$session_class" "$session_service" "$session_desktop"
}
check_failures=0
check_fedora_desktop
[[ "$check_failures" == 0 ]]
for scenario in old_plasma wrong_manager x11 remote unsupported_service missing_session; do
  plasma_version=6.7.5; login_manager=plasmalogin.service; session_type=wayland
  session_remote=no; session_service=plasmalogin; active_session=3
  case "$scenario" in
    old_plasma) plasma_version=5.27.0 ;; wrong_manager) login_manager=gdm.service ;;
    x11) session_type=x11 ;; remote) session_remote=yes ;;
    unsupported_service) session_service=login ;; missing_session) active_session='' ;;
  esac
  check_failures=0
  check_fedora_desktop
  ((check_failures > 0))
done

# Preflight rejection must precede all dependency/package mutations.
(
  select_package_manager() { package_manager=dnf; }
  require_root() { :; }; parse_args() { :; }; run_checks() { :; }
  validate_rpm_package() { die 'fixture rejected RPM'; }
  rm -f "$calls"
  if (main); then exit 1; fi
  [[ ! -e "$calls" ]]
)

status_text="vibeshine-drm/2.0.0, $kernel_release, x86_64: installed"
dkms() { printf '%s\n' "$status_text"; }
check_dkms_state
status_text="vibeshine-drm/2.0.0, wrong-kernel, x86_64: installed"
if (check_dkms_state); then exit 1; fi
status_text="vibeshine-drm/2.0.0, $kernel_release, x86_64: built"
if (check_dkms_state); then exit 1; fi
printf 'Fedora distribution, RPM upgrades/conflicts, kernel tree, session and DKMS checks passed.\n'

# Every privileged inode must have exactly the shipped metadata. A failure to
# inspect capabilities must fail even on the public capability-free executable.
(
  security_root="$fixture/security"
  for name in usr/bin/vibeshine usr/libexec/vibeshine/vibeshine-session-exec \
    usr/libexec/vibeshine/vibeshine-host usr/libexec/vibeshine/vibeshine-session-broker; do
    mkdir -p "${security_root}/${name%/*}"
    touch "$security_root/$name"; chmod 755 "$security_root/$name"
  done
  security_failure=''
  stat() {
    case "${!#}" in
      */vibeshine-host) printf 'root:vibeshine:750:1' ;;
      */vibeshine-session-broker) printf 'root:root:700:1' ;;
      *) printf 'root:root:755:1' ;;
    esac
  }
  getcap() {
    case "$security_failure:${!#}" in
      query:*) return 1 ;;
      public:*/bin/vibeshine) printf '%s cap_sys_admin=p' "${!#}"; return ;;
      host:*/vibeshine-host) return 0 ;;
    esac
    case "${!#}" in
      */vibeshine-host) printf '%s cap_sys_admin,cap_sys_nice=p' "${!#}" ;;
      */vibeshine-session-broker) printf '%s cap_kill,cap_setgid,cap_setuid=p' "${!#}" ;;
    esac
    return 0
  }
  check_private_host_security "$security_root"
  for security_failure in query public host; do
    if (check_private_host_security "$security_root"); then exit 1; fi
  done
  security_failure=''
  rm "$security_root/usr/libexec/vibeshine/vibeshine-host"
  if (check_private_host_security "$security_root"); then exit 1; fi
)
printf 'Privileged-file regression checks passed.\n'

# A pending MOK enrollment must not proceed to driver loading or setup, and
# must not emit the normal installation-complete summary.
(
  trace="$fixture/enrollment-trace"
  parse_args() { :; }; require_root() { :; }; run_checks() { :; }
  select_package_manager() { package_manager=dnf; }
  validate_rpm_package() { :; }; dnf() { :; }; dkms() { :; }
  install_kernel_headers() { :; }; check_session_restart() { :; }
  install_vibeshine() { :; }; check_private_host_security() { :; }
  install_virtual_driver() { enrollment_pending=1; }
  install_dualsense_driver() { printf 'ds5\n' >> "$trace"; }
  repair_machine_setup() { printf 'setup\n' >> "$trace"; }
  print_summary() { printf 'summary\n' >> "$trace"; }
  if (main); then exit 1; fi
  [[ ! -e "$trace" ]]
)
printf 'Pending enrollment cannot report completed installation.\n'
