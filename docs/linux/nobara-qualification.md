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

On 2026-10-12, fork revision `44723ec1` built successfully on the target
Nobara machine with CUDA disabled and GCC 16.2.1. All 145 selected
`fast|component` CTest cases passed, as did the build/distribution and both
Fedora and Arch installer regression scripts. CPack produced
`vibeshine-2.0.0~nobara.validation-1.x86_64`; the RPM inspector validated its
306 payload entries, privileged metadata, assets, services, DKMS sources and
lifecycle contracts. Package SHA-256:
`fd3ba0e46b194a098c992d631b31879c88ad523b8326809e3fe0a29a89eca12d`.
These are build/package results; installation and streaming remain separate
qualification steps.

After prerequisite installation, the updated installer preflight passed over
SSH. Both DRM and DualSense modules compiled as an unprivileged user against the target
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
The statuses below distinguish observed target-machine results from remaining
interactive tests.

| Test | Evidence to retain | Status |
| --- | --- | --- |
| Exact kernel-devel and DKMS build | Installer output, DKMS status, build logs on failure | Passed: both modules installed for target kernel |
| DRM module loads | Installed and loaded version match; no load errors | Passed: installed/loaded version 2.0.1 after reboot |
| Service/controller session binding | System service status and relevant journal excerpt | Passed: ready desktop session 2, host active/running |
| HTTPS Web UI | Browser reaches `https://localhost:47990` | HTTP 200 verified; interactive login pending |
| Moonlight pairing | PIN accepted; client reconnects | Passed: Moonlight 6.2.0 on macOS, reconnect after controller restart |
| Hardware H.264 | Host log identifies hardware encoder; real video stream | Passed: live 1280x720 H.264 using h264_vaapi |
| Audio and input | Audible audio; keyboard/mouse/controller in session | Keyboard passed; audio transport/CoreAudio initialized; audible and physical controller checks pending |
| Virtual display | Create/remove display; original topology restored | Virtual-1 captured; all eight virtual outputs disabled after quitting Desktop; physical baseline verification pending |
| Service restart | Warn viewer, restart controller, reconnect, inspect recovery | Passed: stopped test stream, restarted controller, paired client reconnected |
| Reboot | Driver/controller recover; paired client reconnects | Passed: new boot ID, both services active, paired Mac reconnects with H.264 video and KRunner keyboard input |
| Upgrade | Install a higher native version; configuration/pairings retained | Passed: 2.0.0 to 2.0.1; configuration and pairing files retained; paired client reconnects after reboot |
| Removal/reinstall | DNF removal stops services; reinstall retains state/pairings | Passed on Nobara: normal DNF removal, native installer reinstall, configuration/pairing checksums retained, paired Mac streams again |

Subsequent target-machine installation found and corrected first-install and
interrupted-install systemd hook assumptions (absent service fences, template
queries, socket units without `MainPID`). Revision `d1d37523` was rebuilt and
installed through DNF after recovery. Both DKMS modules report installed for
the exact target kernel and appear in `lsmod`. The controller and host reach
active/running, and the local HTTPS UI returns 200. The host discovers
`h264_vaapi` and `hevc_vaapi`; AV1 encoding is unavailable on this GPU.
The same 145 CTest cases passed after the KMS routing fix. Privileged ownership,
mode, link count and file capabilities were checked on the installed payload;
the active firewalld zone allows the packaged service after definition reload.
Moonlight 6.2.0 was installed on the owner's Mac, paired, and completed a live
H.264 stream from Virtual-1. Keyboard input opened Konsole and ran a test
command; the virtual keyboard also emitted matching libinput events. Client
logs show stereo CoreAudio output and received audio packets; a five-second
stereo tone was played into the host's stream sink. Audible output was not
observed. The idle-desktop stream reported approximately 19–20 FPS, with zero
network frame drops in the observed stable statistics; this does not establish
60 FPS performance under game load.

Input testing found that uinput was not loaded and a pre-existing uaccess ACL
retained `group::---` after udev assigned the dedicated group. Loading uinput
and restoring only that group's `rw` ACL allowed the host to create its virtual
keyboard and mouse. The package now loads uinput at install/boot, preserves
existing user ACLs during recovery, and the DNF installer verifies that the
service identity with its dedicated input group can actually open `/dev/uinput`.

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

Post-upgrade reboot qualification confirmed a new boot ID, both DKMS modules
installed at 2.0.1, DRM loaded at 2.0.1, HTTPS 200, and working group read/write
access on `/dev/uinput`. The previously paired Mac opened a live H.264 Desktop
stream without pairing again; Alt+F2 opened KRunner through streamed input.
Hosted Fedora RPM build and booted package lifecycle workflows also passed.
The hosted lifecycle check does not substitute for native removal/reinstall
or audible audio and physical controller testing.

Native removal/reinstall also passed on the target. DNF removed only Vibeshine;
configuration, apps, machine profile and pairing state matched saved SHA-256
checksums both after removal and after reinstall. Both DKMS modules were
installed again, the controller recovered the host, HTTPS returned 200 and
the already paired Mac opened a Desktop video stream without a new PIN.
The DKMS log warned that Nobara lacks `/usr/sbin/weak-modules`; exact running
kernel installation nevertheless succeeded. Future-kernel coverage remains
dependent on the normal DKMS kernel-update path.
