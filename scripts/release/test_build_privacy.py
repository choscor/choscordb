"""Observe compiler-produced file paths, including paths containing spaces."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    os.name != "nt"
    and shutil.which("cmake")
    and shutil.which("c++")
    and shutil.which("cargo")
    and shutil.which("ninja"),
    "Requires Unix C++, CMake and Rust compiler tools",
)
class BuildPrivacyTest(unittest.TestCase):
    def command(self, arguments, **kwargs):
        result = subprocess.run(
            arguments, capture_output=True, text=True, timeout=180, **kwargs
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    @unittest.skipUnless(sys.platform == "darwin", "Apple linker debug-map behavior")
    def test_cpp_linked_rust_static_library_does_not_leak_debug_map_paths(self):
        with tempfile.TemporaryDirectory(
            prefix="choscordb static privacy "
        ) as temporary:
            root = Path(temporary).resolve()
            source, build = root / "source project", root / "output tree"
            (source / "src").mkdir(parents=True)
            (source / "Cargo.toml").write_text(
                '[package]\nname="privacy-static"\nversion="0.1.0"\nedition="2024"\n'
                '[lib]\ncrate-type=["staticlib"]\n'
            )
            (source / "src/lib.rs").write_text(
                'include!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/diagnostic.rs"));\n'
            )
            (source / "src/diagnostic.rs").write_text(
                '#[unsafe(no_mangle)] pub extern "C" fn rust_diagnostic() { '
                'println!("Rust diagnostic: {}", file!()); }\n'
            )
            (source / "main.cpp").write_text(
                '#include <cstdio>\nextern "C" void rust_diagnostic();\n'
                'int main() { std::puts("C++ diagnostic retained"); std::fflush(stdout); rust_diagnostic(); }\n'
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(Privacy LANGUAGES CXX)\n"
                "set(CHOSCORDB_PRODUCTION_RELEASE ON)\n"
                f'include("{ROOT}/cmake/BuildPrivacy.cmake")\n'
                "find_program(PRIVACY_RUSTC rustc REQUIRED)\n"
                "find_program(PRIVACY_CARGO cargo REQUIRED)\n"
                'choscordb_rust_privacy_environment(privacy_env "${PRIVACY_RUSTC}")\n'
                'add_custom_target(rust_library COMMAND "${CMAKE_COMMAND}" -E env ${privacy_env} '
                '"${PRIVACY_CARGO}" build --release --offline --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml" '
                '--target-dir "${CMAKE_BINARY_DIR}/cargo" '
                'BYPRODUCTS "${CMAKE_BINARY_DIR}/cargo/release/libprivacy_static.a" VERBATIM)\n'
                "add_executable(probe main.cpp)\nadd_dependencies(probe rust_library)\n"
                'target_link_libraries(probe PRIVATE "${CMAKE_BINARY_DIR}/cargo/release/libprivacy_static.a" iconv "-framework CoreFoundation")\n'
            )
            self.command(["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build)])
            self.command(["cmake", "--build", str(build)])
            executable = build / "probe"
            self.assertEqual(
                self.command([str(executable)]).splitlines(),
                [
                    "C++ diagnostic retained",
                    "Rust diagnostic: /choscordb/src/diagnostic.rs",
                ],
            )
            symbols = self.command(["nm", "-pa", str(executable)])
            self.assertIn("_rust_diagnostic", symbols)
            self.assertFalse(
                str(root).encode() in executable.read_bytes(),
                "C++ linked Rust std debug map leaked the fixture build root",
            )

    def test_cargo_remaps_dependency_and_generated_rust_paths_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="choscordb rust privacy ") as temporary:
            root = Path(temporary).resolve()
            source, build = root / "source project", root / "output tree"
            (source / "src").mkdir(parents=True)
            dependency = source / "dependency with spaces"
            dependency.mkdir()
            (source / "Cargo.toml").write_text(
                '[package]\nname="privacy-probe"\nversion="0.1.0"\nedition="2024"\n'
                '[dependencies]\nprivacy-dependency={path="dependency with spaces"}\n'
            )
            (dependency / "Cargo.toml").write_text(
                '[package]\nname="privacy-dependency"\nversion="0.1.0"\nedition="2024"\n'
                '[lib]\npath="lib.rs"\n'
            )
            (dependency / "lib.rs").write_text(
                'include!(concat!(env!("CARGO_MANIFEST_DIR"), "/payload.rs"));\n'
            )
            (dependency / "payload.rs").write_text(
                "pub fn path() -> &'static str { file!() }\n"
            )
            (source / "src/main.rs").write_text(
                'include!(concat!(env!("CARGO_MANIFEST_DIR"), "/src/payload.rs"));\n'
                'include!(concat!(env!("OUT_DIR"), "/generated.rs"));\n'
                'fn main() { println!("{}\\n{}\\n{}", path(), privacy_dependency::path(), generated()); }\n'
            )
            (source / "src/payload.rs").write_text(
                '#[cfg(not(inherited_privacy_cfg))] compile_error!("Inherited Rust flag was lost");\n'
                "fn path() -> &'static str { file!() }\n"
            )
            (source / "build.rs").write_text(
                'fn main() { let out = std::path::PathBuf::from(std::env::var("OUT_DIR").unwrap()); '
                'std::fs::write(out.join("generated.rs"), "fn generated() -> &\'static str { file!() }").unwrap(); }\n'
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(Privacy LANGUAGES CXX)\n"
                "set(CHOSCORDB_PRODUCTION_RELEASE ON)\n"
                f'include("{ROOT}/cmake/BuildPrivacy.cmake")\n'
                "find_program(PRIVACY_RUSTC rustc REQUIRED)\n"
                "find_program(PRIVACY_CARGO cargo REQUIRED)\n"
                'choscordb_rust_privacy_environment(privacy_env "${PRIVACY_RUSTC}")\n'
                'add_custom_target(probe ALL COMMAND "${CMAKE_COMMAND}" -E env ${privacy_env} '
                '"${PRIVACY_CARGO}" build --release --offline --manifest-path "${CMAKE_SOURCE_DIR}/Cargo.toml" '
                '--target-dir "${CMAKE_BINARY_DIR}/cargo" VERBATIM)\n'
            )
            self.command(
                ["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build)],
                env={
                    **os.environ,
                    "CARGO_ENCODED_RUSTFLAGS": "--cfg\x1finherited_privacy_cfg",
                },
            )
            self.command(["cmake", "--build", str(build)])
            executable = build / "cargo/release/privacy-probe"
            observed = self.command([str(executable)]).splitlines()
            self.assertEqual(
                observed[:2],
                [
                    "/choscordb/src/payload.rs",
                    "/choscordb/dependency with spaces/payload.rs",
                ],
            )
            self.assertTrue(observed[2].startswith("/build/cargo/"), observed)
            self.assertTrue(observed[2].endswith("/out/generated.rs"), observed)
            self.assertFalse(
                str(root).encode() in executable.read_bytes(),
                "Release executable leaked fixture build root",
            )

    def test_cargo_native_environment_preserves_space_containing_compiler_flags(self):
        with tempfile.TemporaryDirectory(
            prefix="choscordb native privacy "
        ) as temporary:
            root = Path(temporary).resolve()
            source, build, cargo = (
                root / "source project",
                root / "output tree",
                root / "cargo cache",
            )
            source.mkdir()
            cargo.mkdir()
            (cargo / "location.h").write_text(
                "static const char* dependency_path() { return __FILE__; }\n"
            )
            (source / "native.c").write_text(
                "#if INHERITED_C_FLAG != 17\n#error Inherited C flag was lost\n#endif\n"
                "const char* c_path() { return __FILE__; }\n"
            )
            (source / "main.cpp").write_text(
                "#if INHERITED_CXX_FLAG != 23\n#error Inherited C++ flag was lost\n#endif\n"
                "#include <cstdio>\n"
                + f'#include "{cargo}/location.h"\n'
                + 'extern "C" const char* c_path();\n'
                + "int main() { std::puts(__FILE__); std::puts(c_path()); std::puts(dependency_path()); }\n"
            )
            (source / "compile.py").write_text(
                "import os, shlex, subprocess, sys\n"
                'assert os.environ["CC_SHELL_ESCAPED_FLAGS"] == "1"\n'
                "source, build, cxx, sysroot = sys.argv[1:]\n"
                'sdk = ["-isysroot", sysroot] if sysroot else []\n'
                'subprocess.run(["cc", *sdk, *shlex.split(os.environ["CFLAGS"]), "-c", source+"/native.c", "-o", build+"/native.o"], check=True)\n'
                'subprocess.run([cxx, *sdk, *shlex.split(os.environ["CXXFLAGS"]), source+"/main.cpp", build+"/native.o", "-o", build+"/probe"], check=True)\n'
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(Privacy LANGUAGES CXX)\n"
                "set(CHOSCORDB_PRODUCTION_RELEASE ON)\n"
                f'include("{ROOT}/cmake/BuildPrivacy.cmake")\n'
                "find_program(PRIVACY_RUSTC rustc REQUIRED)\n"
                'choscordb_rust_privacy_environment(privacy_env "${PRIVACY_RUSTC}")\n'
                'add_custom_target(probe ALL COMMAND "${CMAKE_COMMAND}" -E env ${privacy_env} '
                f'"{sys.executable}" "${{CMAKE_SOURCE_DIR}}/compile.py" '
                '"${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}" "${CMAKE_OSX_SYSROOT}" VERBATIM)\n'
            )
            self.command(
                ["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build)],
                env={
                    **os.environ,
                    "CARGO_HOME": str(cargo),
                    "CFLAGS": "-DINHERITED_C_FLAG=17",
                    "CXXFLAGS": "-DINHERITED_CXX_FLAG=23",
                },
            )
            self.command(["cmake", "--build", str(build)])
            self.assertEqual(
                self.command([str(build / "probe")]).splitlines(),
                ["/choscordb/main.cpp", "/choscordb/native.c", "/cargo/location.h"],
            )
            self.assertFalse(
                str(root).encode() in (build / "probe").read_bytes(),
                "Native executable leaked fixture build root",
            )

    def test_native_release_paths_are_virtual_and_development_is_unchanged(self):
        with tempfile.TemporaryDirectory(prefix="choscordb privacy ") as temporary:
            root = Path(temporary).resolve()
            source = root / "source project"
            source.mkdir()
            (source / "main.cpp").write_text(
                "#include <cstdio>\nextern const char* generated();\n"
                "int main() { std::puts(__FILE__); std::puts(generated()); }\n"
            )
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.28)\nproject(Privacy LANGUAGES CXX)\n"
                f'include("{ROOT}/cmake/BuildPrivacy.cmake")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/generated.cpp" "const char* generated() { return __FILE__; }\\n")\n'
                'add_executable(probe main.cpp "${CMAKE_BINARY_DIR}/generated.cpp")\n'
            )
            for mode in ("development", "production", "rehearsal"):
                with self.subTest(mode=mode):
                    build = root / (mode + " build")
                    self.command(
                        [
                            "cmake",
                            "-G",
                            "Ninja",
                            "-S",
                            str(source),
                            "-B",
                            str(build),
                            "-DCHOSCORDB_PRODUCTION_RELEASE="
                            + ("ON" if mode == "production" else "OFF"),
                            "-DCHOSCORDB_UPDATE_REHEARSAL="
                            + ("ON" if mode == "rehearsal" else "OFF"),
                        ]
                    )
                    self.command(["cmake", "--build", str(build)])
                    observed = self.command([str(build / "probe")]).splitlines()
                    if mode == "development":
                        self.assertEqual(
                            observed,
                            [str(source / "main.cpp"), str(build / "generated.cpp")],
                        )
                    else:
                        self.assertEqual(
                            observed, ["/choscordb/main.cpp", "/build/generated.cpp"]
                        )
                        self.assertFalse(
                            str(root).encode() in (build / "probe").read_bytes(),
                            "Native executable leaked fixture build root",
                        )


if __name__ == "__main__":
    unittest.main()
