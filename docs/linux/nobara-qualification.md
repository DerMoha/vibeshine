# Nobara 44 qualification

Target: Nobara 44 KDE Plasma, x86_64, Wayland, running kernel
`7.2.9-200.nobara.fc44.x86_64`. Fedora container builds do not validate this
kernel, GPU, compositor or installed machine-service behavior.

## Before installation

From the fork checkout, run this as your normal desktop user:

```bash
bash scripts/linux_support_report.sh > nobara-support-report.txt 2>&1
```

The report records distro/kernel/build-tree metadata, tool versions, active
login manager/session, Secure Boot and SELinux modes, GPU/VAAPI discovery,
DKMS state and executable permissions. It does not read configuration, pairing
files, signing keys or logs. Review it before sharing. Missing tools are
reported; the report does not install anything. `vainfo` only reports the
current user's available VAAPI driver; a successful real H.264 stream is still
required. NVENC readiness requires streaming validation as well.

Confirm the running-kernel build Makefile and exact matching kernel-devel;
Plasma 6 Wayland on local seat0; Plasma Login Manager or SDDM; and a GPU with
hardware H.264 encode support. Resolve existing Sunshine/Apollo package
conflicts explicitly. Record and back up any existing streaming-host settings
and pairings before changing packages. Keep security modes unchanged.

## Native package installation

Use the validated x86_64 RPM from your fork's Fedora build artifacts or a native
build. Hosted artifacts use a validation prerelease version. Do not install a
lower version over an existing production package; the installer rejects
that downgrade. The build/RPM inspection commands are in
[Fedora builds](fedora-build.md).

```bash
sudo bash scripts/linux_install.sh --package /absolute/path/Vibeshine.rpm
```

This operation can terminate an existing VibeShine stream during package setup.
It installs DKMS/build prerequisites, checks the exact kernel build release,
uses DNF, validates privileged inodes, checks driver installation and DKMS, and
configures the existing session controller. Stop on errors, retain their
output, and correct the reported prerequisite. Do not use `--skip-checks`,
`--allowerasing`, `--nodeps`, or security-mode changes to get past a failure.

If Secure Boot key enrollment or a loaded-module update requires a reboot,
follow the driver's diagnostics, reboot, then rerun installation and the report.
The private host is `root:vibeshine` mode 0750 with only
`cap_sys_admin,cap_sys_nice=p`. The broker is `root:root` mode 0700 with only
`cap_kill,cap_setgid,cap_setuid=p`. The public executable and session client
have empty file capability sets. Only the controller is enabled; it activates
the host/socket for the authoritative session.

## Actual-machine test record

Qualification on 2026-10-12 used Nobara 44, KDE Plasma 6.7.5 Wayland,
Plasma Login Manager, kernel `7.2.9-200.nobara.fc44.x86_64`, and an AMD
RX 6900 XT with Mesa 26.2.4. Secure Boot and SELinux were disabled on this
machine; enforcing configurations remain unqualified.

The native CUDA-free build used GCC 16.2.1. All 145 selected `fast|component`
CTest cases and the Fedora/Arch installer regressions passed. CPack RPM
inspection checked assets, services, privileged metadata and DKMS sources.
Native installation and the upgrade from `2.0.0~nobara.validation` to
`2.0.1~nobara.validation` were tested separately from the hosted build.

| Test | Evidence to retain | Status |
| --- | --- | --- |
| Exact kernel-devel and DKMS build | Installer output, DKMS status, build logs on failure | Passed: both modules installed for target kernel |
| DRM module loads | Installed and loaded version match; no load errors | Passed: installed/loaded version 2.0.1 after reboot |
| Service/controller session binding | System service status and relevant journal excerpt | Passed: ready desktop session 2, host active/running |
| HTTPS Web UI | Browser reaches `https://localhost:47990` | HTTP 200 verified; interactive login pending |
| Moonlight pairing | PIN accepted; client reconnects | Passed: Moonlight 6.2.0 on macOS, reconnect after controller restart |
| Hardware H.264 | Host log identifies hardware encoder; real video stream | Passed: live 1280x720 H.264 using h264_vaapi |
| Audio and input | Audible audio; keyboard/mouse/controller in session | Owner confirmed mouse, keyboard and audible audio on Mac; Steam Deck controller check pending |
| Virtual display | Create/remove display; original topology restored | Virtual-1 captured; all eight virtual outputs disabled after quitting Desktop; physical baseline verification pending |
| Service restart | Warn viewer, restart controller, reconnect, inspect recovery | Passed: stopped test stream, restarted controller, paired client reconnected |
| Reboot | Driver/controller recover; paired client reconnects | Passed: new boot ID, both services active, paired Mac reconnects with H.264 video and KRunner keyboard input |
| Upgrade | Install a higher native version; configuration/pairings retained | Passed: 2.0.0 to 2.0.1; configuration and pairing files retained; paired client reconnects after reboot |
| Removal/reinstall | DNF removal stops services; reinstall retains state/pairings | Passed on Nobara: normal DNF removal, native installer reinstall, configuration/pairing checksums retained, paired Mac streams again |


Configuration, apps, machine profile and pairing state matched saved SHA-256
checksums after native removal and reinstall. Moonlight 6.2 on macOS reconnected
without a new PIN. The owner confirmed mouse, keyboard and audible audio.
The observed idle desktop ran around 19–20 FPS; this does not establish 60 FPS
performance under game load. Steam Deck controller/game testing remains pending.

Nobara's `make kernelrelease` reports only `7.2.9`; its prepared
`include/config/kernel.release` and `include/generated/utsrelease.h` report the
full release. Both prepared values must match the running kernel. DKMS warned
that `/usr/sbin/weak-modules` was absent, but exact running-kernel installation
succeeded. Future-kernel updates still require qualification.

## Repeating runtime and lifecycle checks

Inspect the discrete GPU with `vainfo --display drm --device
/dev/dri/by-path/pci-0000:03:00.0-render` (from `libva-utils`). VAAPI encoding
entrypoints alone do not prove a working stream. Inspect services with
`sudo journalctl -u vibeshine-session-controller.service -u vibeshine.service
-b --no-pager`; review logs before sharing.

Before lifecycle tests, warn the viewer, stop the services, and take a private
root-readable backup of `/etc/vibeshine` and `/var/lib/vibeshine`. These contain
credentials and pairings. Test a genuinely higher RPM version, reboot and
reconnect the paired client. Then use normal `dnf remove vibeshine`, verify
services stopped and state remains, reinstall a trusted RPM and reconnect.
Do not invoke reset or purge during retention testing.

For controller qualification, pair a Steam Deck as an additional client and
test sticks, buttons, triggers and rumble in a controller-supported game.
Check physical output topology before/after streaming. Non-NVIDIA pre-login
streaming is not supported; this AMD desktop test does not qualify greeter
streaming or NVIDIA hardware.

## Native Mac resolution regression (2026-10-25)

On kernel 7.2.11 with Mesa 26.2.5, the virtual driver's native shmem scanout
at 3024×1964 failed AMD EGL DMA-BUF import with `EGL_BAD_ALLOC` (0x3003).
An independent probe reproduced failure at pitch 12096 and success with the
same visible dimensions at pitch 12288. 1920×1080 imported successfully.
The driver fix aligns native allocations to a 256-byte pitch while preserving
visible dimensions and leaving imported GPU buffers unchanged. Callback tests
cover multiple widths/depths, overflow and allocator failure. Driver builds
passed against target kernels 7.2.9 and 7.2.11; live validation of the updated
module is pending a full power-off/power-on cycle. This host cannot reliably
warm reboot (owner reports a DRAM diagnostic LED); do not use `reboot`.
