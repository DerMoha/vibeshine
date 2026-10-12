# Fedora and Nobara RPM builds

Fedora and Nobara (`ID=nobara` or an `ID_LIKE` token of `fedora`) use native
DNF dependencies and the system `gcc`/`g++`. Start with the CUDA-free build:

```bash
git submodule update --init --recursive
bash scripts/linux_build.sh --skip-cuda
python3 scripts/validate-linux-rpm.py build/cpack_artifacts/*.rpm
```

The build script installs dependencies through sudo. `--sudo-off` is for
isolated root build containers. It does not upgrade the Fedora distribution.
The optional COPR spec also accepts `rpmbuild --without cuda`; its default
CUDA release build remains unchanged.

RPMs are produced by the existing CPack architecture in `build/cpack_artifacts`;
they include both Web UIs, machine services, session helpers and DKMS sources.
The package pre-install hook creates the dedicated service account before RPM
assigns private-file ownership. Public executables have no file capabilities;
only the private host and session broker receive their exact permitted sets.

CUDA is not required for the base Linux build. CUDA builds require an explicit
compatible `CC` and `CXX`; they must also satisfy the existing pinned CUDA 12.9
release requirements. Distro-default compiler compatibility with CUDA is not
assumed. Arch/CachyOS retain their versioned compiler selection.

The Fedora 44 workflow builds and inspects the RPM on hosted Linux. This is
package validation, not evidence that Nobara hardware, DKMS, KDE Wayland,
Moonlight pairing or streaming work. Those require actual machine qualification.
