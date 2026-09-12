#!/usr/bin/env python3
"""Run the release desktop probe and record externally observed process timings."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time


def run(binary, destination, timeout):
    with tempfile.TemporaryDirectory(prefix="choscordb-performance-") as directory:
        report = Path(directory) / "probe.json"
        started = time.perf_counter_ns()
        process = subprocess.Popen(
            [str(binary), str(report)], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True,
        )
        markers = {}
        diagnostics = []

        def read_output():
            for line in process.stdout:
                observed = time.perf_counter_ns()
                try:
                    event = json.loads(line).get("event")
                except (ValueError, AttributeError):
                    continue
                if event in ("ready", "shutdown_started"):
                    markers.setdefault(event, observed)

        def read_errors():
            for line in process.stderr:
                # Keep diagnostics bounded; never include them in a published report.
                diagnostics.append(line[:1024])
                if len(diagnostics) > 32:
                    del diagnostics[0]

        readers = [threading.Thread(target=read_output), threading.Thread(target=read_errors)]
        for reader in readers:
            reader.start()
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise RuntimeError("Desktop probe exceeded its execution deadline") from None
        finally:
            exited = time.perf_counter_ns()
            for reader in readers:
                reader.join()
        if report.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(report.read_bytes())
        if code:
            raise RuntimeError(f"Desktop probe failed ({code}): {''.join(diagnostics)}")
        data = json.loads(report.read_text())
        if not all(marker in markers for marker in ("ready", "shutdown_started")):
            raise RuntimeError("Desktop probe omitted required timing markers")
        data["external_observation"] = {
            "process_to_ready_ms": (markers["ready"] - started) / 1e6,
            "shutdown_marker_to_exit_ms": (exited - markers["shutdown_started"]) / 1e6,
            "total_process_ms": (exited - started) / 1e6,
            "limitations": "Pipe receipt can shorten observed shutdown time; wait observation can lengthen it. Fresh profile is not cold OS cache."
        }
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(data, indent=2) + "\n")
        return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--timeout", type=float, default=600)
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    data = run(args.binary.resolve(strict=True), args.report, args.timeout)
    print(json.dumps(data["external_observation"], indent=2))


if __name__ == "__main__":
    main()
