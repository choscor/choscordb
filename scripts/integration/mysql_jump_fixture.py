#!/usr/bin/env python3
"""Verify native SSH ProxyJump with isolated agent, host trust and Docker servers."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import uuid


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def wait_ready(command, env=None):
    deadline = time.monotonic() + 90
    while True:
        result = subprocess.run(command, env=env, capture_output=True)
        if result.returncode == 0:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError("Disposable fixture did not become ready")
        time.sleep(1)


def main():
    root = Path(__file__).resolve().parents[2]
    ssh = shutil.which("ssh")
    if not ssh or not shutil.which("ssh-agent"):
        raise SystemExit("OpenSSH client and agent are required")
    prefix = "choscordb-jump-" + uuid.uuid4().hex[:10]
    mysql, target, jump = (prefix + suffix for suffix in ["-mysql", "-target", "-jump"])
    with tempfile.TemporaryDirectory(prefix="choscordb-jump-") as temp:
        directory = Path(temp)
        agent = None
        try:
            run(
                "docker",
                "run",
                "--detach",
                "--rm",
                "--name",
                mysql,
                "-e",
                "MYSQL_ROOT_PASSWORD=choscordb-test-password",
                "-e",
                "MYSQL_DATABASE=choscordb_test",
                "mysql:8.4",
                stdout=subprocess.DEVNULL,
            )
            wait_ready(["docker", "exec", mysql, "mysqladmin", "ping", "--silent"])
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
            (directory / "authorized_keys").write_text(
                (directory / "identity.pub").read_text()
            )
            (directory / "sshd_config").write_text(
                "\n".join(
                    [
                        "Port 22",
                        "ListenAddress 0.0.0.0",
                        "HostKey /fixture/host",
                        "AuthorizedKeysFile /fixture/authorized_keys",
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
            for name, link in [
                (target, f"{mysql}:mysql-ssh-target"),
                (jump, f"{target}:target-ssh"),
            ]:
                run(
                    "docker",
                    "run",
                    "--detach",
                    "--rm",
                    "--name",
                    name,
                    "--link",
                    link,
                    "-p",
                    "127.0.0.1::22",
                    "-v",
                    f"{directory}:/fixture:ro",
                    "--entrypoint",
                    "/bin/sh",
                    "postgres:17-alpine",
                    "-c",
                    "apk add --no-cache openssh >/dev/null && echo 'root:fixture-only' | chpasswd && exec /usr/sbin/sshd -D -e -f /fixture/sshd_config",
                    stdout=subprocess.DEVNULL,
                )
            port = (
                run("docker", "port", jump, "22", capture_output=True)
                .stdout.strip()
                .rsplit(":", 1)[1]
            )
            key = (directory / "host.pub").read_text()
            (directory / "known_hosts").write_text(
                f"[127.0.0.1]:{port} {key}target-ssh {key}"
            )
            # -F is inherited by OpenSSH's native jump subprocess; no global SSH files change.
            (directory / "ssh_config").write_text(
                f'Host *\n    UserKnownHostsFile "{directory / "known_hosts"}"\n'
                "    GlobalKnownHostsFile /dev/null\n    StrictHostKeyChecking yes\n    BatchMode yes\n"
            )
            (directory / "ssh").write_text(
                '#!/bin/sh\nexec "$CHOSCORDB_REAL_SSH" -F "$CHOSCORDB_SSH_CONFIG" "$@"\n'
            )
            (directory / "ssh").chmod(0o700)
            socket = directory / "agent.sock"
            agent = subprocess.Popen(
                ["ssh-agent", "-D", "-a", str(socket)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            env = dict(
                os.environ,
                SSH_AUTH_SOCK=str(socket),
                CHOSCORDB_REAL_SSH=ssh,
                CHOSCORDB_SSH_CONFIG=str(directory / "ssh_config"),
                CHOSCORDB_SSH_KNOWN_HOSTS=str(directory / "known_hosts"),
                CHOSCORDB_SSH_JUMP_PORT=port,
            )
            env["PATH"] = str(directory) + os.pathsep + env["PATH"]
            wait_ready(["ssh-add", str(directory / "identity")], env)
            wait_ready(
                [
                    str(directory / "ssh"),
                    "-o",
                    "ConnectTimeout=2",
                    "-p",
                    port,
                    "root@127.0.0.1",
                    "true",
                ],
                env,
            )
            wait_ready(
                [
                    str(directory / "ssh"),
                    "-o",
                    "ConnectTimeout=2",
                    "-J",
                    f"root@127.0.0.1:{port}",
                    "root@target-ssh",
                    "true",
                ],
                env,
            )
            run(
                "cargo",
                "test",
                "-p",
                "choscordb-driver-mysql",
                "--test",
                "jump_hosts",
                "--locked",
                "--",
                "--ignored",
                cwd=root,
                env=env,
            )
        finally:
            if agent is not None:
                agent.terminate()
                try:
                    agent.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    agent.kill()
                    agent.wait()
            for name in [jump, target, mysql]:
                subprocess.run(
                    ["docker", "rm", "-f", name],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )


if __name__ == "__main__":
    main()
