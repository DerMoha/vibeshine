#!/usr/bin/env bash
# Run only inside the disposable Fedora systemd container created by CI.
# No real display, GPU, user profile or host kernel module tree is mounted.
set -euo pipefail
export LC_ALL=C
[[ $EUID == 0 && $(systemd-detect-virt --container) == systemd-nspawn ]] || {
  echo 'This package lifecycle smoke test requires the isolated CI nspawn container.' >&2
  exit 1
}
package=$1
installer=$2
[[ -f "$package" ]] || exit 1
transaction() {
  local operation=$1; shift
  dnf "$operation" -y "$@" 2>&1 | tee "/tmp/vibeshine-$operation.log"
  if grep -Eiq 'scriptlet.*fail|%pre.*fail|%post.*fail' "/tmp/vibeshine-$operation.log"; then
    echo "RPM $operation scriptlet failed" >&2
    exit 1
  fi
}
transaction install "$package"
# shellcheck source=/dev/null
source "$installer"
check_private_host_security
# A container has no authoritative KDE desktop. Package setup must not activate
# an unsupervised host; the native installer itself would reject this environment.
systemctl is-active --quiet vibeshine.service && exit 1
install -d -o root -g root -m 0755 /etc/vibeshine
install -d -o vibeshine -g vibeshine -m 0700 /var/lib/vibeshine
printf 'policy retention fixture\n' > /etc/vibeshine/lifecycle-fixture
printf 'pairing retention fixture\n' > /var/lib/vibeshine/lifecycle-fixture
chown vibeshine:vibeshine /var/lib/vibeshine/lifecycle-fixture
chmod 0600 /etc/vibeshine/lifecycle-fixture /var/lib/vibeshine/lifecycle-fixture
sha256sum /etc/vibeshine/lifecycle-fixture /var/lib/vibeshine/lifecycle-fixture > /tmp/retention.sha256
# Reinstall exercises the RPM replacement lifecycle (%pre 2, old %preun 1).
# Version-ordering tests are separate; this is explicitly a same-EVR reinstall.
transaction reinstall "$package"
check_private_host_security
sha256sum -c /tmp/retention.sha256
transaction remove vibeshine
if rpm -q vibeshine; then exit 1; fi
if systemctl is-active --quiet vibeshine-session-controller.service; then exit 1; fi
if systemctl is-active --quiet vibeshine.service; then exit 1; fi
sha256sum -c /tmp/retention.sha256
[[ ! -e /usr/bin/vibeshine && ! -e /usr/libexec/vibeshine/vibeshine-host ]]
transaction install "$package"
check_private_host_security
sha256sum -c /tmp/retention.sha256
printf 'PASS: native RPM first install, same-version replacement, removal and reinstall preserve fixture state and privileged metadata.\n'
printf 'Kernel modules and streaming remain unqualified; this container has no KDE/GPU or matching kernel tree.\n'
