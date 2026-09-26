#!/usr/bin/env python3
"""Start and stop an isolated MySQL 8.4 server for macOS CI."""

import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time


PORT = 33306
PASSWORD = "choscordb-test-password"
OWNER = "choscordb-mysql-ci-fixture-v1"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("start", "stop"))
    parser.add_argument(
        "--directory", type=Path, default=Path("build/integration/mysql")
    )
    parser.add_argument("--bin-dir", type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    marker = root / "fixture.json"
    if args.action == "start":
        if args.bin_dir is None:
            parser.error("start requires --bin-dir")
        if root.exists() and any(root.iterdir()):
            raise SystemExit(
                "Refusing to initialize a nonempty MySQL fixture directory"
            )
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", PORT)) == 0:
                raise SystemExit(f"MySQL fixture port {PORT} is already in use")
        root.mkdir(parents=True, exist_ok=True)
        bin_dir = args.bin_dir.resolve()
        marker.write_text(json.dumps({"owner": OWNER, "bin_dir": str(bin_dir)}) + "\n")
        data = root / "data"
        server = bin_dir / "mysqld"
        log = root / "server.log"
        subprocess.run(
            [
                server,
                "--no-defaults",
                "--initialize-insecure",
                f"--datadir={data}",
                f"--log-error={log}",
            ],
            check=True,
        )
        subprocess.run(
            [
                server,
                "--no-defaults",
                f"--datadir={data}",
                f"--port={PORT}",
                "--bind-address=127.0.0.1",
                f"--socket={root / 'mysql.sock'}",
                f"--pid-file={root / 'mysql.pid'}",
                f"--log-error={log}",
                "--daemonize",
            ],
            check=True,
        )
        client = [
            bin_dir / "mysql",
            "--no-defaults",
            "--protocol=tcp",
            "--host=127.0.0.1",
            f"--port={PORT}",
            "--user=root",
        ]
        for _ in range(60):
            ready = subprocess.run(
                [*client, "--execute=SELECT 1"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if ready.returncode == 0:
                break
            time.sleep(1)
        else:
            raise SystemExit("MySQL fixture did not become ready; inspect server.log")
        subprocess.run(
            [
                *client,
                "--execute=ALTER USER 'root'@'localhost' IDENTIFIED BY 'choscordb-test-password'; CREATE DATABASE choscordb_test;",
            ],
            check=True,
        )
        return
    if not marker.exists():
        return
    config = json.loads(marker.read_text())
    if config.get("owner") != OWNER:
        raise SystemExit("Refusing an unrecognized MySQL fixture directory")
    if not (root / "mysql.pid").exists():
        return
    client = Path(config["bin_dir"]) / "mysqladmin"
    subprocess.run(
        [
            client,
            "--no-defaults",
            "--protocol=tcp",
            "--host=127.0.0.1",
            f"--port={PORT}",
            "--user=root",
            "shutdown",
        ],
        check=True,
        env={**os.environ, "MYSQL_PWD": PASSWORD},
    )


if __name__ == "__main__":
    main()
