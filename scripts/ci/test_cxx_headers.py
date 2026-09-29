"""Exercise incremental CXX header publication with a real Ninja consumer."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(
    shutil.which("cmake")
    and shutil.which("ninja")
    and any(shutil.which(tool) for tool in ("c++", "clang++", "g++", "cl")),
    "requires CMake, Ninja, and a C++ compiler",
)
class CxxHeadersTest(unittest.TestCase):
    def command(self, *arguments):
        result = subprocess.run(
            arguments, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        return result.stdout

    def test_one_incremental_build_updates_consumers_and_restores_deleted_headers(self):
        with tempfile.TemporaryDirectory(prefix="choscordb-cxx-headers-") as temporary:
            root = Path(temporary)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            header = source / "bridge.h"
            header.write_text("struct BridgeEvent { long long payload[1]; };\n")
            (source / "cxx.h").write_text("// synthetic CXX support header\n")
            (source / "consumer.cpp").write_text(
                '#include "choscordb-bridge/src/lib.rs.h"\n'
                "#include <iostream>\n"
                "int main() { std::cout << sizeof(BridgeEvent); }\n"
            )
            (source / "CMakeLists.txt").write_text(
                f"""cmake_minimum_required(VERSION 3.24)
project(HeaderPublication LANGUAGES CXX)
include("{(ROOT / "cmake/CxxHeaders.cmake").as_posix()}")
add_custom_target(fake-cargo
    COMMAND ${{CMAKE_COMMAND}} -E make_directory "${{PROJECT_BINARY_DIR}}/staged/choscordb-bridge/src" "${{PROJECT_BINARY_DIR}}/staged/rust"
    COMMAND ${{CMAKE_COMMAND}} -E copy_if_different "${{PROJECT_SOURCE_DIR}}/bridge.h" "${{PROJECT_BINARY_DIR}}/staged/choscordb-bridge/src/lib.rs.h"
    COMMAND ${{CMAKE_COMMAND}} -E copy_if_different "${{PROJECT_SOURCE_DIR}}/cxx.h" "${{PROJECT_BINARY_DIR}}/staged/rust/cxx.h"
)
add_library(bridge INTERFACE)
choscordb_export_cxx_headers(bridge fake-cargo "${{PROJECT_BINARY_DIR}}/staged" "${{PROJECT_BINARY_DIR}}/generated")
target_include_directories(bridge INTERFACE "${{PROJECT_BINARY_DIR}}/generated")
add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE bridge)
"""
            )
            self.command("cmake", "-S", str(source), "-B", str(build), "-G", "Ninja")
            self.command("cmake", "--build", str(build), "--parallel", "2")
            executable = build / (
                "consumer.exe" if (build / "consumer.exe").exists() else "consumer"
            )
            initial_size = int(self.command(str(executable)))

            # Cargo changes a header only after Ninja has computed its dirty graph.
            # The very same invocation must recompile the consumer, not a second build.
            header.write_text("struct BridgeEvent { long long payload[3]; };\n")
            self.command("cmake", "--build", str(build), "--parallel", "2")
            self.assertEqual(int(self.command(str(executable))), initial_size * 3)

            objects = list((build / "CMakeFiles/consumer.dir").glob("*.o")) + list(
                (build / "CMakeFiles/consumer.dir").glob("*.obj")
            )
            self.assertEqual(len(objects), 1)
            modified = objects[0].stat().st_mtime_ns
            self.command("cmake", "--build", str(build), "--parallel", "2")
            self.assertEqual(objects[0].stat().st_mtime_ns, modified)

            published = build / "generated/choscordb-bridge/src/lib.rs.h"
            published.unlink()
            self.command("cmake", "--build", str(build), "--parallel", "2")
            self.assertEqual(published.read_text(), header.read_text())
            self.assertEqual(int(self.command(str(executable))), initial_size * 3)


if __name__ == "__main__":
    unittest.main()
