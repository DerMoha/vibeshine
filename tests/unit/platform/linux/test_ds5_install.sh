#!/usr/bin/env bash
# Exercise installation outcomes without touching real modules or services.
set -euo pipefail
source "$1"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
SYS_MODULE_ROOT="$work/sys"
SOURCE_DIR="$work/source"
mkdir -p "$SYS_MODULE_ROOT/$MODULE_NAME" "$SOURCE_DIR"
printf 'NEW\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
modinfo() { printf 'NEW\n'; }
modprobe() { return "${load_result:-0}"; }
current_kernel_release() { printf 'test-kernel\n'; }
dkms() { :; }
install_with_dkms() { return "${dkms_result:-0}"; }
install_direct() { touch "$work/direct"; return "${direct_result:-0}"; }

expect_status() {
  local expected=$1 result=0
  shift
  "$@" || result=$?
  [[ $result == "$expected" ]] || { echo "expected $expected, got $result: $*" >&2; exit 1; }
}

# Missing package sources must fail before any install attempt.
expect_status 1 install_module
for file in Makefile build-module dkms.conf vibeshine_ds5_uapi.h \
            vibeshine_ds5.h vibeshine_ds5_udc.c vibeshine_ds5_gadget.c \
            vibeshine_ds5_main.c; do touch "$SOURCE_DIR/$file"; done
expect_status 1 validate_source_tree
chmod 755 "$SOURCE_DIR/build-module"
expect_status 0 install_module
[[ ! -e "$work/direct" ]]

# Loading failure must not be hidden behind a successful DKMS build.
load_result=1
expect_status 1 install_module
load_result=0
printf 'OLD\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 4 install_module
expect_status 4 module_status
printf 'NEW\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 0 module_status

# DKMS failure falls back to a direct build, whose errors also propagate.
dkms_result=1
direct_result=1
expect_status 1 install_module
[[ -f "$work/direct" ]]
direct_result=0
expect_status 0 install_module
load_result=1
expect_status 1 install_module
rm "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 1 module_status
printf 'DualSense install: missing sources, DKMS/direct failures, load failures, and stale modules covered.\n'

# Retire only exact obsolete registrations for the replaced kernel.
MODULE_VERSION=2.0.1
status_output=$'vibeshine-ds5/2.0.0, test-kernel, x86_64: installed\nvibeshine-ds5/2.0.0, fallback-kernel, x86_64: installed\nvibeshine-ds5/2.0.0: broken\nvibeshine-ds5/2.0.1, test-kernel, x86_64: installed\nunrelated/1.0: installed'
remove_result=0
calls="$work/dkms-removals"
dkms() {
  if [[ "$1" == status ]]; then printf '%s\n' "$status_output";
  else printf '%s\n' "$*" >> "$calls"; return "$remove_result"; fi
}
expect_status 0 remove_obsolete_dkms_registrations test-kernel
[[ $(< "$calls") == 'remove -m vibeshine-ds5 -v 2.0.0 -k test-kernel' ]]
remove_result=1
expect_status 1 remove_obsolete_dkms_registrations test-kernel
status_output='vibeshine-ds5/../../unsafe, test-kernel, x86_64: installed'
expect_status 1 remove_obsolete_dkms_registrations test-kernel
printf 'DualSense obsolete registration cleanup is exact, deduplicated and fails closed.\n'

# A failed replacement must not invoke retirement, even if direct fallback fails.
load_result=0
dkms_result=1
direct_result=1
retired=0
remove_obsolete_dkms_registrations() { retired=1; return 0; }
expect_status 1 install_module
[[ $retired == 0 ]]
# Successful replacement precedes cleanup; cleanup failure must not use fallback.
install_with_dkms() { printf 'installed\n' > "$work/replacement"; return 0; }
remove_obsolete_dkms_registrations() {
  [[ -f "$work/replacement" && "$1" == test-kernel ]] || exit 1
  return 1
}
rm -f "$work/direct"
expect_status 1 install_module
[[ ! -e "$work/direct" ]]
printf 'DualSense replacement precedes retirement and preserves other kernels.\n'
