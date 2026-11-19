"""Run the build script's real dependency/compiler/configure functions in fixtures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BUILD = (ROOT / "scripts/linux_build.sh").read_text()
FUNCTIONS = BUILD.split("# Determine the OS and call the appropriate function", 1)[0]
COMPILERS = "select_compilers() {" + BUILD.split("select_compilers() {", 1)[1].split("\nselect_compilers\n", 1)[0]


class BuildDistributionTests(unittest.TestCase):
    def test_step_argument_forms(self):
        for arguments in (("--step", "deps"), ("--step=deps",)):
            result = subprocess.run(["bash", "-c", FUNCTIONS + '\nprintf "selected=%s\\n" "$step"',
                                     "build-test", *arguments], cwd=ROOT,
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("selected=deps", result.stdout)
        for arguments in (("--step",), ("--step", "--skip-cuda")):
            result = subprocess.run(["bash", "-c", FUNCTIONS, "build-test", *arguments],
                                    cwd=ROOT, text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("--step requires a value", result.stderr)

    def shell(self, code, environment=None):
        return subprocess.run(["bash", "-c", code], cwd=ROOT,
                              env=dict(os.environ, **(environment or {})),
                              text=True, capture_output=True)

    def test_os_release_identity_and_like_tokens(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "os-release"
            for identity, like, family in [("nobara", "fedora", "fedora"),
                    ("fedora", "", "fedora"), ("custom", "rhel fedora", "fedora"),
                    ("custom", "notfedora", "custom"), ("arch", "", "arch"),
                    ("cachyos", "arch", "arch"), ("custom", "arch linux", "arch")]:
                with self.subTest(identity=identity, like=like):
                    path.write_text(f'ID="{identity}"\nID_LIKE="{like}"\nVERSION_ID="44"\n')
                    result = self.shell('source scripts/linux_distribution.sh; detect_linux_distribution "$RELEASE_FILE"; printf "%s %s %s" "$os_id" "$os_family" "$os_version"', {"RELEASE_FILE": str(path)})
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, f"{identity} {family} 44")

    def test_fedora_dependencies_and_native_compilers(self):
        result = self.shell(FUNCTIONS + COMPILERS + '''
distro=fedora; gcc_version=system; skip_cuda=1
unset CC CXX
select_compilers
[[ "$CC:$CXX" == gcc:g++ ]]
add_fedora_deps
printf '%s\\n' "${dependencies[@]}"
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        dependencies = result.stdout.splitlines()
        for dependency in ("gcc", "gcc-c++", "libstdc++-static", "libjpeg-turbo-devel", "libpng-devel", "libwebp-devel", "make", "sqlite-devel", "npm",
                           "wayland-devel", "wayland-protocols-devel", "libayatana-appindicator-gtk3-devel"):
            self.assertIn(dependency, dependencies)
        self.assertNotIn("gccsystem", dependencies)
        self.assertNotIn("libappindicator-gtk3-devel", dependencies)

    def test_arch_and_explicit_compilers_are_preserved(self):
        result = self.shell(COMPILERS + '''
distro=arch; gcc_version=14; unset CC CXX
select_compilers
[[ "$CC:$CXX" == gcc-14:g++-14 ]] || exit 1
distro=fedora; CC=/toolchain/gcc; CXX=/toolchain/g++
select_compilers
[[ "$CC:$CXX" == /toolchain/gcc:/toolchain/g++ ]]
''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cuda_disabled_configure_clears_cached_release_requirement(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = self.shell(FUNCTIONS + COMPILERS + '''
distro=fedora; gcc_version=system; skip_cuda=1
check_version() { return 0; }
cmake() { printf '%s\\n' "$@"; }
cd "$TEST_BUILD_ROOT"
run_step_cmake
''', {"TEST_BUILD_ROOT": temporary})
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("-DSUNSHINE_ENABLE_CUDA=OFF", result.stdout.splitlines())
            self.assertIn("-DSUNSHINE_REQUIRE_CUDA_PASCAL=OFF", result.stdout.splitlines())

    def test_native_install_has_regular_public_inode(self):
        # Exercise the actual packaging rule with a small compiled executable;
        # no host application dependencies or package installation are needed.
        import shutil
        if not shutil.which("cmake") or not shutil.which("cc"):
            self.skipTest("CMake and a C compiler are required")
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / "main.c").write_text("int main(void) { return 0; }\n")
            (stage / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\nproject(Fixture C)\n"
                "add_executable(sunshine main.c)\n"
                "set_target_properties(sunshine PROPERTIES OUTPUT_NAME vibeshine VERSION 2.0.0)\n"
                f'include("{ROOT / "cmake/packaging/unix.cmake"}")\n')
            commands = [["cmake", "-S", temporary, "-B", str(stage / "build"),
                         "-DCMAKE_SYSTEM_NAME=Linux", "-DCMAKE_INSTALL_PREFIX=/usr"],
                        ["cmake", "--build", str(stage / "build")],
                        ["cmake", "--install", str(stage / "build")]]
            for command in commands:
                result = subprocess.run(command, env=dict(os.environ, DESTDIR=str(stage / "install")),
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            executable = stage / "install/usr/bin/vibeshine"
            self.assertTrue(executable.is_file())
            self.assertFalse(executable.is_symlink())
            self.assertEqual(executable.stat().st_nlink, 1)


if __name__ == "__main__":
    unittest.main()
