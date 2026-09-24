#!/usr/bin/env python3
"""Run native TLS policy/client-certificate tests against an isolated MySQL container."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import uuid


def run(*args, **kwargs):
    return subprocess.run([str(arg) for arg in args], check=True, text=True, **kwargs)


def main():
    openssl = shutil.which("openssl")
    if not openssl:
        raise SystemExit("OpenSSL is required to issue disposable fixture certificates")
    name = "choscordb-mysql-tls-" + uuid.uuid4().hex[:10]
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.PIPE}
    with tempfile.TemporaryDirectory(prefix="choscordb-mysql-tls-") as temp:
        directory = Path(temp)
        run(
            openssl,
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-days",
            "2",
            "-subj",
            "/CN=ChoscorDB disposable TLS CA",
            "-addext",
            "basicConstraints=critical,CA:TRUE",
            "-addext",
            "keyUsage=critical,keyCertSign,cRLSign",
            "-keyout",
            directory / "ca.key",
            "-out",
            directory / "ca.crt",
            **quiet,
        )
        for identity, common_name, usage in [
            ("server", "localhost", "serverAuth"),
            ("client", "tls_client", "clientAuth"),
        ]:
            run(
                openssl,
                "req",
                "-new",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-subj",
                f"/CN={common_name}",
                "-keyout",
                directory / f"{identity}.key",
                "-out",
                directory / f"{identity}.csr",
                **quiet,
            )
            extensions = directory / f"{identity}.cnf"
            extensions.write_text(
                "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                + f"extendedKeyUsage={usage}\n"
                + ("subjectAltName=DNS:localhost\n" if identity == "server" else "")
            )
            run(
                openssl,
                "x509",
                "-req",
                "-in",
                directory / f"{identity}.csr",
                "-CA",
                directory / "ca.crt",
                "-CAkey",
                directory / "ca.key",
                "-CAcreateserial",
                "-days",
                "2",
                "-sha256",
                "-extfile",
                extensions,
                "-out",
                directory / f"{identity}.crt",
                **quiet,
            )
        run(
            openssl,
            "pkcs12",
            "-export",
            "-in",
            directory / "client.crt",
            "-inkey",
            directory / "client.key",
            "-certfile",
            directory / "ca.crt",
            "-keypbe",
            "PBE-SHA1-3DES",
            "-certpbe",
            "PBE-SHA1-3DES",
            "-macalg",
            "sha1",
            "-passout",
            "pass:fixture-identity-password",
            "-out",
            directory / "client.p12",
            **quiet,
        )
        # Only the disposable server material is mounted, readable by the container's mysql user.
        server = directory / "server"
        server.mkdir()
        for filename in ["ca.crt", "server.crt", "server.key"]:
            shutil.copy(directory / filename, server / filename)
            (server / filename).chmod(0o644)
        try:
            run(
                "docker",
                "run",
                "--detach",
                "--rm",
                "--name",
                name,
                "-e",
                "MYSQL_ROOT_PASSWORD=choscordb-test-password",
                "-p",
                "127.0.0.1::3306",
                "-v",
                f"{server}:/fixture:ro",
                "mysql:8.4",
                "--ssl-ca=/fixture/ca.crt",
                "--ssl-cert=/fixture/server.crt",
                "--ssl-key=/fixture/server.key",
                stdout=subprocess.DEVNULL,
            )
            deadline = time.monotonic() + 90
            while subprocess.run(
                [
                    "docker",
                    "exec",
                    name,
                    "mysqladmin",
                    "--protocol=tcp",
                    "-h127.0.0.1",
                    "ping",
                    "--silent",
                ],
                **quiet,
            ).returncode:
                if time.monotonic() >= deadline:
                    run("docker", "logs", name)
                    raise SystemExit("Disposable MySQL failed to become ready")
                time.sleep(1)
            run(
                "docker",
                "exec",
                name,
                "mysql",
                "-uroot",
                "-pchoscordb-test-password",
                "-e",
                "CREATE DATABASE choscordb_test; "
                "CREATE USER 'tls_client'@'%' IDENTIFIED BY 'fixture-password' REQUIRE X509;",
                **quiet,
            )
            ports = json.loads(
                subprocess.check_output(["docker", "inspect", name], text=True)
            )[0]["NetworkSettings"]["Ports"]
            env = dict(
                os.environ,
                CHOSCORDB_MYSQL_TLS_PORT=ports["3306/tcp"][0]["HostPort"],
                CHOSCORDB_MYSQL_PORT=ports["3306/tcp"][0]["HostPort"],
                CHOSCORDB_MYSQL_TLS_ROOT_CERTIFICATE=str(directory / "ca.crt"),
                CHOSCORDB_MYSQL_TLS_CLIENT_IDENTITY=str(directory / "client.p12"),
                CHOSCORDB_MYSQL_TLS_CLIENT_PASSWORD="fixture-identity-password",
            )
            run(
                "cargo",
                "test",
                "-p",
                "choscordb-driver-mysql",
                "--test",
                "tls_modes",
                "--test",
                "native_properties",
                "--test",
                "socks",
                "--test",
                "bootstrap",
                "--locked",
                "--",
                "--include-ignored",
                env=env,
            )
            run(
                "cargo",
                "test",
                "-p",
                "choscordb-core",
                "--test",
                "mysql",
                "--locked",
                "--",
                "--include-ignored",
                env=env,
            )
            run(
                "cargo",
                "test",
                "-p",
                "choscordb-core",
                "--test",
                "lifecycle_native",
                "mysql",
                "--locked",
                "--",
                "--ignored",
                env=env,
            )
        finally:
            subprocess.run(
                ["docker", "rm", "-f", name],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )


if __name__ == "__main__":
    main()
