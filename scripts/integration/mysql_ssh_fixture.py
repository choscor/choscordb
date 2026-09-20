#!/usr/bin/env python3
"""Run real SSH MySQL tests without modifying the user's SSH configuration.

Requires Docker, OpenSSH, and the disposable MySQL container documented in
mysql.md. Set CHOSCORDB_MYSQL_CONTAINER to its name (default:
choscordb-mysql-implementation). Creates and removes its own SSH container/keys.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import uuid


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def main():
    root = Path(__file__).resolve().parents[2]
    mysql = os.environ.get(
        "CHOSCORDB_MYSQL_CONTAINER", "choscordb-mysql-implementation"
    )
    name = "choscordb-mysql-ssh-" + uuid.uuid4().hex[:10]
    ssh = shutil.which("ssh")
    if not ssh:
        raise SystemExit("OpenSSH is required")
    with tempfile.TemporaryDirectory(prefix="choscordb-mysql-ssh-") as temp:
        directory = Path(temp)
        for key in ["identity", "host"]:
            run(
                "ssh-keygen",
                "-q",
                "-t",
                "ed25519",
                "-N",
                "",
                "-f",
                str(directory / key),
            )
        (directory / "sshd_config").write_text(
            "\n".join(
                [
                    "Port 22",
                    "ListenAddress 0.0.0.0",
                    "HostKey /fixture/host",
                    "AuthorizedKeysFile /fixture/identity.pub",
                    "StrictModes no",
                    "PermitRootLogin yes",
                    "PubkeyAuthentication yes",
                    "PasswordAuthentication no",
                    "KbdInteractiveAuthentication no",
                    "AllowTcpForwarding yes",
                    "UsePAM no",
                    "PidFile /tmp/fixture-sshd.pid",
                    "LogLevel ERROR",
                    "",
                ]
            )
        )
        try:
            run(
                "docker",
                "run",
                "--detach",
                "--rm",
                "--name",
                name,
                "--link",
                f"{mysql}:mysql-ssh-target",
                "-p",
                "127.0.0.1::22",
                "-v",
                f"{directory}:/fixture:ro",
                "--entrypoint",
                "/bin/sh",
                "postgres:17-alpine",
                "-c",
                "apk add --no-cache openssh >/dev/null && passwd -d root >/dev/null && exec /usr/sbin/sshd -D -e -f /fixture/sshd_config",
                stdout=subprocess.DEVNULL,
            )
            port = (
                run("docker", "port", name, "22", capture_output=True)
                .stdout.strip()
                .rsplit(":", 1)[1]
            )
            (directory / "known_hosts").write_text(
                f"[127.0.0.1]:{port} " + (directory / "host.pub").read_text()
            )
            # This wrapper delegates every byte to real OpenSSH. It isolates
            # host-key trust/config files, without bypassing verification.
            (directory / "ssh").write_text(
                '#!/bin/sh\nexec "$CHOSCORDB_REAL_SSH" -F /dev/null -o GlobalKnownHostsFile=/dev/null '
                '-o "UserKnownHostsFile=$CHOSCORDB_SSH_KNOWN_HOSTS" "$@"\n'
            )
            (directory / "ssh").chmod(0o700)
            env = dict(
                os.environ,
                CHOSCORDB_REAL_SSH=ssh,
                CHOSCORDB_SSH_PORT=port,
                CHOSCORDB_SSH_IDENTITY=str(directory / "identity"),
                CHOSCORDB_SSH_KNOWN_HOSTS=str(directory / "known_hosts"),
            )
            env["PATH"] = str(directory) + os.pathsep + env["PATH"]
            deadline = time.monotonic() + 90
            while True:
                probe = subprocess.run(
                    [
                        str(directory / "ssh"),
                        "-o",
                        "BatchMode=yes",
                        "-o",
                        "StrictHostKeyChecking=yes",
                        "-o",
                        "ConnectTimeout=2",
                        "-p",
                        port,
                        "-i",
                        str(directory / "identity"),
                        "root@127.0.0.1",
                        "true",
                    ],
                    env=env,
                    capture_output=True,
                )
                if probe.returncode == 0:
                    break
                if time.monotonic() >= deadline:
                    run("docker", "logs", name)
                    raise RuntimeError(
                        "SSH fixture did not become ready: " + probe.stderr.decode()
                    )
                time.sleep(1)
            run(
                "cargo",
                "test",
                "-p",
                "choscordb-driver-mysql",
                "--test",
                "ssh",
                "--locked",
                "--",
                "--include-ignored",
                "--test-threads=1",
                cwd=root,
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
