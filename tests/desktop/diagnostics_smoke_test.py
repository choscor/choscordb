"""Exercise the app's public smoke launch with isolated local diagnostics."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile

if os.name == "posix":
    import fcntl


def main() -> None:
    executable = Path(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="choscordb-diagnostics-") as root:
        environment = os.environ.copy()
        environment["CHOSCORDB_TEST_DATA_DIR"] = root
        report = Path(root, "smoke-report.zip")
        environment["CHOSCORDB_TEST_EXPORT_PATH"] = str(report)
        environment["QT_QPA_PLATFORM"] = "offscreen"

        def launch_clean() -> None:
            completed = subprocess.run(
                [str(executable), "--smoke-test"],
                env=environment,
                text=True,
                capture_output=True,
                timeout=30,
                check=False,
            )
            if completed.returncode:
                raise AssertionError(
                    f"smoke launch exited {completed.returncode}: {completed.stderr}"
                )

        launch_clean()
        if not report.is_file():
            raise AssertionError("app smoke launch did not produce a diagnostic ZIP")
        with zipfile.ZipFile(report) as archive:
            if archive.testzip() is not None:
                raise AssertionError("diagnostic ZIP failed CRC validation")
            if set(archive.namelist()) != {"manifest.json", "events.jsonl"}:
                raise AssertionError("diagnostic ZIP contains unexpected members")
            manifest = json.loads(archive.read("manifest.json"))
            if manifest["schema"] != 1 or not manifest["has_history"]:
                raise AssertionError("diagnostic manifest lacks the smoke run")
            if manifest["build_version"] != "unknown" or "build_version" not in manifest[
                "unavailable_categories"
            ]:
                raise AssertionError("manifest invented a build identifier")
            expected_family = (
                "windows"
                if sys.platform == "win32"
                else "macos"
                if sys.platform == "darwin"
                else "linux"
            )
            if manifest["os_family"] != expected_family:
                raise AssertionError("manifest mislabeled the OS family")
            if "crash_signature" not in manifest["unavailable_categories"]:
                raise AssertionError("manifest claims an unavailable crash signature")
            if root.encode() in archive.read("manifest.json") + archive.read(
                "events.jsonl"
            ):
                raise AssertionError("report contains its local storage path")
        folder = Path(root, "diagnostics")

        def records() -> list[dict]:
            return [
                json.loads(line)
                for path in folder.glob("*.jsonl")
                for line in path.read_text(encoding="utf-8").splitlines()
            ]

        initial = records()
        events = [record["event"] for record in initial]
        for required in ("startup", "memory_sample", "shutdown"):
            if required not in events:
                raise AssertionError(f"missing {required} in {events}")
        memory_samples = [
            record for record in initial if record["event"] == "memory_sample"
        ]
        if not any(
            isinstance(sample.get("resident_bytes"), (int, float))
            and sample["resident_bytes"] > 0
            for sample in memory_samples
        ):
            raise AssertionError(
                "no supported resident-memory measurement was recorded"
            )
        if list(folder.glob("run-*.marker")):
            raise AssertionError("clean exit left a run marker")

        if os.name == "posix":
            lock_fd = os.open(folder / ".io.lock", os.O_RDWR | os.O_CREAT, 0o600)
            fcntl.flock(lock_fd, fcntl.LOCK_EX)
            screenshot = Path(root, "locked-startup.png")
            process = subprocess.Popen(
                [str(executable), "--screenshot", str(screenshot)],
                env=environment,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            try:
                deadline = time.monotonic() + 2.5
                while not screenshot.is_file() and time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise AssertionError("locked startup exited before Qt drew the window")
                    time.sleep(0.025)
                if not screenshot.is_file():
                    raise AssertionError("diagnostics lock blocked the Qt startup event loop")
            finally:
                fcntl.flock(lock_fd, fcntl.LOCK_UN)
                os.close(lock_fd)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
            if process.returncode:
                raise AssertionError(f"locked startup exited {process.returncode}")

        held_environment = environment.copy()
        held_environment["CHOSCORDB_TEST_SMOKE_DELAY_MS"] = "10000"
        process = subprocess.Popen(
            [str(executable), "--smoke-test"],
            env=held_environment,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            deadline = time.monotonic() + 20
            while not list(folder.glob("run-*.marker")):
                if process.poll() is not None:
                    raise AssertionError(
                        "held app exited before its run marker appeared"
                    )
                if time.monotonic() >= deadline:
                    raise AssertionError("held app did not create a run marker")
                time.sleep(0.05)
            process.kill()
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)

        launch_clean()
        events = [record["event"] for record in records()]
        if events.count("unclean_exit") != 1:
            raise AssertionError(f"expected one unclean prior run: {events}")
        with zipfile.ZipFile(report) as archive:
            manifest = json.loads(archive.read("manifest.json"))
            if manifest["category_counts"].get("unclean_exit") != 1:
                raise AssertionError("relaunch ZIP did not report the unclean exit")
        if list(folder.glob("run-*.marker")):
            raise AssertionError("relaunch left a run marker")


if __name__ == "__main__":
    main()
