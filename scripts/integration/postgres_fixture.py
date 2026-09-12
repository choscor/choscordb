#!/usr/bin/env python3
"""Manage only this repository's marked, local PostgreSQL test cluster."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess


def run(args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["start", "stop", "status", "env"])
    parser.add_argument("--directory", type=Path, default=Path("build/integration/postgres"))
    parser.add_argument("--bin-dir", type=Path,
                        default=Path("/opt/homebrew/opt/postgresql@17/bin"))
    args = parser.parse_args()
    root = args.directory.resolve()
    marker = root / "fixture.json"
    if marker.exists():
        config = json.loads(marker.read_text())
        if config.get("owner") != "choscordb-postgres-test-fixture-v1":
            raise SystemExit("Refusing an unrecognized fixture directory")
    elif args.action != "start":
        raise SystemExit("Fixture has not been initialized")
    else:
        if root.exists() and any(root.iterdir()):
            raise SystemExit("Refusing to initialize a nonempty unmarked directory")
        root.mkdir(parents=True, exist_ok=True)
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        config = {"owner": "choscordb-postgres-test-fixture-v1", "port": port,
                  "user": "choscordb", "password": "choscordb-local-fixture-password",
                  "database": "postgres", "bin_dir": str(args.bin_dir.resolve())}
        marker.write_text(json.dumps(config, indent=2) + "\n")
    binaries = Path(config["bin_dir"])
    data = root / "data"
    if args.action == "env":
        print(json.dumps({"CHOSCORDB_TEST_POSTGRES":
              f"host=localhost port={config['port']} user={config['user']} "
              f"dbname={config['database']} password={config['password']} sslmode=disable",
              "CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE": str(root / "ca.crt"),
              "CHOSCORDB_TEST_POSTGRES_HOST": "localhost",
              "CHOSCORDB_TEST_POSTGRES_PORT": str(config["port"]),
              "CHOSCORDB_TEST_POSTGRES_USER": config["user"],
              "CHOSCORDB_TEST_POSTGRES_PASSWORD": config["password"],
              "CHOSCORDB_TEST_POSTGRES_DATABASE": config["database"],
              "CHOSCORDB_TEST_POSTGRES_CTL": str(binaries / "pg_ctl"),
              "CHOSCORDB_TEST_POSTGRES_DATA": str(data),
              "CHOSCORDB_TEST_POSTGRES_LOG": str(root / "server.log")}, indent=2))
        return
    if args.action == "status":
        raise SystemExit(subprocess.run([str(binaries / "pg_ctl"), "-D", str(data), "status"]).returncode)
    if args.action == "stop":
        run([binaries / "pg_ctl", "-D", data, "-m", "fast", "-w", "stop"])
        return
    if not (data / "PG_VERSION").exists():
        password_file = root / "init-password"
        password_file.write_text(config["password"])
        password_file.chmod(0o600)
        try:
            run([binaries / "initdb", "-D", data, "-U", config["user"],
                 "--auth-local=trust", "--auth-host=scram-sha-256", "--encoding=UTF8",
                 "--no-locale", f"--pwfile={password_file}"])
        finally:
            password_file.unlink(missing_ok=True)
    if config.get("tls_version") != 2:
        openssl = shutil.which("openssl")
        if not openssl:
            raise SystemExit("OpenSSL is required for the isolated TLS fixture")
        quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.PIPE}
        run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "30",
             "-subj", "/CN=ChoscorDB fixture CA", "-addext", "basicConstraints=critical,CA:TRUE",
             "-addext", "keyUsage=critical,keyCertSign,cRLSign",
             "-keyout", root / "ca.key", "-out", root / "ca.crt"], **quiet)
        run([openssl, "req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost",
             "-keyout", root / "server.key", "-out", root / "server.csr"], **quiet)
        extensions = root / "server-extensions.cnf"
        extensions.write_text("subjectAltName=DNS:localhost\nbasicConstraints=critical,CA:FALSE\n"
                              "keyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n")
        run([openssl, "x509", "-req", "-in", root / "server.csr", "-CA", root / "ca.crt",
             "-CAkey", root / "ca.key", "-CAcreateserial", "-days", "30", "-sha256",
             "-extfile", extensions, "-out", root / "server.crt"], **quiet)
        (root / "server.key").chmod(0o600)
        (root / "ca.key").chmod(0o600)
        def literal(path):
            return str(path).replace("'", "''")
        with (data / "postgresql.conf").open("a") as stream:
            stream.write(f"\nlisten_addresses = '127.0.0.1'\nport = {config['port']}\n")
            stream.write("shared_buffers = '16MB'\nmax_connections = 20\nssl = on\n")
            stream.write(f"ssl_cert_file = '{literal(root / 'server.crt')}'\n")
            stream.write(f"ssl_key_file = '{literal(root / 'server.key')}'\n")
        config["tls_version"] = 2
        marker.write_text(json.dumps(config, indent=2) + "\n")
    if subprocess.run([str(binaries / "pg_ctl"), "-D", str(data), "status"],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        run([binaries / "pg_ctl", "-D", data, "reload"])
        print("Fixture is already running")
    else:
        run([binaries / "pg_ctl", "-D", data, "-l", root / "server.log", "-w", "start"])
    print(f"Fixture ready on localhost:{config['port']}")


if __name__ == "__main__":
    main()
