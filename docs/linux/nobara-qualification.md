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

After prerequisite installation, the updated installer preflight passed over
SSH. The DRM module also compiled as an unprivileged user against the target
kernel; `modinfo` reported the exact running-kernel vermagic. This was a direct
module build, not DKMS registration, installation or loading. BTF generation
was skipped because the development tree did not provide `vmlinux`.

Nobara's prepared development tree reports only `7.2.9` from
`make kernelrelease`. Its `include/config/kernel.release` and generated
`UTS_RELEASE` both contain the full target release; the installer requires both
prepared values to match the running kernel.

The owner's pre-install report on 2026-10-12 confirmed Nobara 44 with the
target kernel, an existing build Makefile and matching recorded kernel release,
and the installed matching `kernel-devel` package. Plasma Login Manager
(`plasmalogin.service`) was active with a local seat0 KDE Wayland session.
Secure Boot and SELinux were disabled. No conflicting streaming-host RPMs were
installed. DKMS, G++, CMake and Ninja were absent. Firewalld was active;
the VibeShine firewall service was absent before package installation.

The machine exposes two AMD GPUs: Navi 21 at PCI `0000:03:00.0` and
Raphael/Radeon 610M at `0000:13:00.0`. A subsequent SSH check identified the
discrete GPU as an RX 6900 XT with Mesa 26.2.4. `vainfo` reported H.264
Constrained Baseline, Main and High encoding entrypoints. FFmpeg successfully
encoded 60 synthetic 1280x720 frames with `h264_vaapi` on that render device.
This confirms basic hardware encoding, while VibeShine capture and streaming
remain pending. To inspect the discrete GPU explicitly:

```bash
sudo dnf install libva-utils
ls -l /dev/dri/by-path/
vainfo --display drm --device /dev/dri/by-path/pci-0000:03:00.0-render
```

Retain the VAAPI profiles and entrypoints. An encoding entrypoint is a
prerequisite; qualification still requires a real H.264 streaming session.

Record the package version, running kernel, GPU/driver and results for each row.
The installation and runtime rows below remain pending until observed on the
target machine.

| Test | Evidence to retain | Status |
| --- | --- | --- |
| Exact kernel-devel and DKMS build | Installer output, DKMS status, build logs on failure | Pending |
| DRM module loads | Installed and loaded version match; no load errors | Pending |
| Service/controller session binding | System service status and relevant journal excerpt | Pending |
| HTTPS Web UI | Browser reaches `https://localhost:47990` | Pending |
| Moonlight pairing | PIN accepted; client reconnects | Pending |
| Hardware H.264 | Host log identifies hardware encoder; real video stream | Pending |
| Audio and input | Audible audio; keyboard/mouse/controller in session | Pending |
| Virtual display | Create/remove display; original topology restored | Pending |
| Service restart | Warn viewer, restart controller, reconnect, inspect recovery | Pending |
| Reboot | Driver/controller recover; paired client reconnects | Pending |
| Upgrade | Install a higher native version; configuration/pairings retained | Pending |
| Removal/reinstall | DNF removal stops services; reinstall retains state/pairings | Pending |

For logs, use `sudo journalctl -u vibeshine-session-controller.service
-u vibeshine.service -b --no-pager`. Review logs before sharing. A reachable Web
UI or active system unit alone does not prove encoder, audio, input or display
readiness. NVIDIA greeter streaming additionally needs a logout/login test;
non-NVIDIA pre-login streaming is not currently supported.

For lifecycle testing, take a root-readable backup of `/etc/vibeshine` and
`/var/lib/vibeshine` while the service is stopped, after warning the viewer.
Use a genuinely newer RPM to test upgrade; repeat the installer. Remove with
`sudo dnf remove vibeshine`, verify the controller is stopped and persistent
state remains, then reinstall a trusted matching/newer RPM and reconnect the
already paired client. Do not invoke machine-host reset or purge during this
data-retention test. Keep backups private; they contain pairing credentials.
