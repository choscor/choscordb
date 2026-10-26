#!/usr/bin/env python3
"""Start and stop an isolated MySQL 8.4 or MariaDB server for macOS CI."""

import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time


PASSWORD = "choscordb-test-password"
FLAVORS = {
    "mysql": {
        "name": "MySQL",
        "port": 33306,
        "owner": "choscordb-mysql-ci-fixture-v1",
        "directory": "build/integration/mysql",
        "server": "mysqld",
        "client": "mysql",
        "admin": "mysqladmin",
    },
    "mariadb": {
        "name": "MariaDB",
        "port": 33307,
        "owner": "choscordb-mariadb-ci-fixture-v1",
        "directory": "build/integration/mariadb",
        "server": "mariadbd",
        "client": "mariadb",
        "admin": "mariadb-admin",
    },
}


def initialize(flavor, bin_dir, data, log):
    """Create a data directory whose root account has no password yet."""
    if flavor == "mysql":
        command = [
            bin_dir / "mysqld",
            "--no-defaults",
            "--initialize-insecure",
            f"--datadir={data}",
            f"--log-error={log}",
        ]
    else:
        install = bin_dir / "mariadb-install-db"
        command = [
            install,
            "--no-defaults",
            f"--basedir={install.resolve().parent.parent}",
            f"--datadir={data}",
            "--auth-root-authentication-method=normal",
            "--skip-test-db",
        ]
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("start", "stop"))
    parser.add_argument("--flavor", choices=tuple(FLAVORS), default="mysql")
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--bin-dir", type=Path)
    args = parser.parse_args()
    flavor = FLAVORS[args.flavor]
    name = flavor["name"]
    port = flavor["port"]
    root = (args.directory or Path(flavor["directory"])).resolve()
    marker = root / "fixture.json"
    if args.action == "start":
        if args.bin_dir is None:
            parser.error("start requires --bin-dir")
        if root.exists() and any(root.iterdir()):
            raise SystemExit(
                f"Refusing to initialize a nonempty {name} fixture directory"
            )
        with socket.socket() as probe:
            if probe.connect_ex(("127.0.0.1", port)) == 0:
                raise SystemExit(f"{name} fixture port {port} is already in use")
        root.mkdir(parents=True, exist_ok=True)
        bin_dir = args.bin_dir.resolve()
        marker.write_text(
            json.dumps({"owner": flavor["owner"], "bin_dir": str(bin_dir)}) + "\n"
        )
        data = root / "data"
        log = root / "server.log"
        initialize(args.flavor, bin_dir, data, log)
        server = [
            bin_dir / flavor["server"],
            "--no-defaults",
            f"--datadir={data}",
            f"--port={port}",
            "--bind-address=127.0.0.1",
            f"--socket={root / 'mysql.sock'}",
            f"--pid-file={root / 'mysql.pid'}",
            f"--log-error={log}",
        ]
        if args.flavor == "mysql":
            subprocess.run([*server, "--daemonize"], check=True)
        else:
            # mariadbd cannot daemonize itself; detach it from this session.
            subprocess.Popen(
                server,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
        client = [
            bin_dir / flavor["client"],
            "--no-defaults",
            "--protocol=tcp",
            "--host=127.0.0.1",
            f"--port={port}",
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
            raise SystemExit(f"{name} fixture did not become ready; inspect server.log")
        subprocess.run(
            [
                *client,
                f"--execute=ALTER USER 'root'@'localhost' IDENTIFIED BY '{PASSWORD}'; CREATE DATABASE choscordb_test;",
            ],
            check=True,
        )
        return
    if not marker.exists():
        return
    config = json.loads(marker.read_text())
    if config.get("owner") != flavor["owner"]:
        raise SystemExit(f"Refusing an unrecognized {name} fixture directory")
    if not (root / "mysql.pid").exists():
        return
    subprocess.run(
        [
            Path(config["bin_dir"]) / flavor["admin"],
            "--no-defaults",
            "--protocol=tcp",
            "--host=127.0.0.1",
            f"--port={port}",
            "--user=root",
            "shutdown",
        ],
        check=True,
        env={**os.environ, "MYSQL_PWD": PASSWORD},
    )


if __name__ == "__main__":
    main()
