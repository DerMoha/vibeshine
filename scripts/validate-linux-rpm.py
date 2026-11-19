#!/usr/bin/env python3
"""Inspect the actual CPack RPM payload and metadata without installing it."""
import argparse
from pathlib import Path
import subprocess
import stat


def rpm(package, *arguments):
    return subprocess.run(["rpm", "-qp", *arguments, str(package)], check=True,
                          capture_output=True, text=True).stdout


def validate(package):
    identity = rpm(package, "--qf", "%{NAME} %{ARCH}").strip()
    if identity != "vibeshine x86_64":
        raise ValueError(f"Unexpected package identity: {identity}")
    records = rpm(package, "--qf",
                  "[%{FILENAMES}\\t%{FILEMODES}\\t%{FILEUSERNAME}\\t%{FILEGROUPNAME}\\t%{FILECAPS}\\n]")
    files = {}
    file_types = {}
    for line in records.splitlines():
        path, mode, user, group, capabilities = line.split("\t")
        file_types[path] = int(mode)
        files[path] = (int(mode) & 0o7777, user, group, capabilities)
    private = "/usr/libexec/vibeshine/"
    expected = {
        "/usr/bin/vibeshine": (0o755, "root", "root", ""),
        private + "vibeshine-session-exec": (0o755, "root", "root", ""),
        private + "vibeshine-session-broker": (0o700, "root", "root", "cap_kill,cap_setgid,cap_setuid=p"),
        private + "vibeshine-host": (0o750, "root", "vibeshine", "cap_sys_admin,cap_sys_nice=p"),
    }
    for path, metadata in expected.items():
        if not stat.S_ISREG(file_types.get(path, 0)):
            raise ValueError(f"Privileged executable must be a regular inode: {path}")
        if files.get(path) != metadata:
            raise ValueError(f"Incorrect privileged-file metadata for {path}: {files.get(path)}; expected {metadata}")
    for path, (_, user, _, capabilities) in files.items():
        if user != "root":
            raise ValueError(f"Program payload must be root-owned: {path}")
        if capabilities and path not in expected:
            raise ValueError(f"Unexpected file capabilities on {path}: {capabilities}")
        if path.startswith(("/var/lib/vibeshine/", "/home/", "/root/")):
            raise ValueError(f"Persistent/user data must not be in the payload: {path}")
    required = ["/usr/share/vibeshine/web/index.html", "/usr/share/vibeshine/web/v2/index.html",
                "/usr/lib/sysusers.d/vibeshine.conf", "/usr/lib/firewalld/services/vibeshine.xml",
                "/usr/lib/udev/rules.d/70-vibeshine-uinput.rules"]
    required += [private + name for name in ("vibeshine-machine-host", "vibeshine-session-controller",
                 "vibeshine-profile-import", "vibeshine-drm-install", "vibeshine-ds5-install",
                 "vibeshine-app-supervisor", "vibeshine-kwin-session-environment")]
    required += ["/usr/lib/systemd/system/" + name for name in (
        "vibeshine.service", "vibeshine-session-controller.service", "vibeshine-session-exec.socket",
        "vibeshine-session-exec@.service", "vibeshine-drm-setup.service")]
    for path in required:
        if path not in files:
            raise ValueError(f"Missing runtime payload: {path}")
    for driver in ("drm", "ds5"):
        if not any(path.startswith(f"/usr/src/vibeshine-{driver}-") and path.endswith("/dkms.conf") for path in files):
            raise ValueError(f"Missing DKMS source: {driver}")
    scripts = rpm(package, "--scripts")
    for contract in ("/usr/bin/systemd-sysusers", "vibeshine_quiesce_or_abort", "vibeshine_quiesce_for_removal"):
        if contract not in scripts:
            raise ValueError(f"Missing package lifecycle operation: {contract}")
    conflicts = rpm(package, "--conflicts").splitlines()
    for name in ("sunshine", "apollo", "vibepollo"):
        if name not in conflicts:
            raise ValueError(f"Missing conflict guard: {name}")
    requires = rpm(package, "--requires")
    if "/usr/bin/systemd-sysusers" not in requires:
        raise ValueError("Missing pre-install sysusers dependency")
    print(f"Validated {package}: {len(files)} files, privileged metadata, Web UIs, services, DKMS sources and lifecycle hooks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", type=Path, nargs="+")
    args = parser.parse_args()
    for package in args.packages:
        validate(package)


if __name__ == "__main__":
    main()
