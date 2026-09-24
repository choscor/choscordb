#!/usr/bin/env python3
"""Verify isolated per-hop credentials without writing global SSH configuration."""

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


def database_certificates(directory):
    tls = directory / "tls"
    tls.mkdir(mode=0o755)
    quiet = {"stdout": subprocess.DEVNULL, "stderr": subprocess.PIPE}
    run(
        "openssl",
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-nodes",
        "-days",
        "2",
        "-subj",
        "/CN=Owned SSH fixture CA",
        "-addext",
        "basicConstraints=critical,CA:TRUE",
        "-keyout",
        str(tls / "ca.key"),
        "-out",
        str(tls / "ca.crt"),
        **quiet,
    )
    run(
        "openssl",
        "req",
        "-new",
        "-newkey",
        "rsa:2048",
        "-nodes",
        "-subj",
        "/CN=mysql-ssh-target",
        "-keyout",
        str(tls / "server.key"),
        "-out",
        str(tls / "server.csr"),
        **quiet,
    )
    (tls / "extensions").write_text(
        "subjectAltName=DNS:mysql-ssh-target,DNS:postgres-ssh-target\nextendedKeyUsage=serverAuth\n"
    )
    run(
        "openssl",
        "x509",
        "-req",
        "-in",
        str(tls / "server.csr"),
        "-CA",
        str(tls / "ca.crt"),
        "-CAkey",
        str(tls / "ca.key"),
        "-CAcreateserial",
        "-days",
        "2",
        "-extfile",
        str(tls / "extensions"),
        "-out",
        str(tls / "server.crt"),
        **quiet,
    )
    # Only this generated fixture key is shared with the disposable database users.
    (tls / "server.key").chmod(0o644)
    return tls


def main():
    root = Path(__file__).resolve().parents[2]
    ssh = shutil.which("ssh")
    if not ssh or not shutil.which("ssh-agent"):
        raise SystemExit("OpenSSH client and agent are required")
    prefix = "choscordb-hop-auth-" + uuid.uuid4().hex[:10]
    mysql, postgres, target, jump = (
        prefix + suffix for suffix in ["-mysql", "-postgres", "-target", "-jump"]
    )
    with tempfile.TemporaryDirectory(prefix="choscordb-hop-auth-") as temp:
        directory = Path(temp)
        tls = database_certificates(directory)
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
                "-v",
                f"{tls}:/tls:ro",
                "mysql:8.4",
                "--ssl-ca=/tls/ca.crt",
                "--ssl-cert=/tls/server.crt",
                "--ssl-key=/tls/server.key",
                "--require-secure-transport=ON",
                stdout=subprocess.DEVNULL,
            )
            wait_ready(["docker", "exec", mysql, "mysqladmin", "ping", "--silent"])
            run(
                "docker",
                "run",
                "--detach",
                "--rm",
                "--name",
                postgres,
                "-e",
                "POSTGRES_USER=root",
                "-e",
                "POSTGRES_PASSWORD=fixture-pg-password",
                "-v",
                f"{tls}:/tls:ro",
                "--entrypoint",
                "/bin/sh",
                "postgres:17-alpine",
                "-c",
                "cp /tls/server.key /tmp/server.key && chmod 600 /tmp/server.key && chown postgres:postgres /tmp/server.key && exec docker-entrypoint.sh postgres -c ssl=on -c ssl_ca_file=/tls/ca.crt -c ssl_cert_file=/tls/server.crt -c ssl_key_file=/tmp/server.key",
                stdout=subprocess.DEVNULL,
            )
            wait_ready(
                [
                    "docker",
                    "exec",
                    postgres,
                    "pg_isready",
                    "-U",
                    "root",
                    "-d",
                    "postgres",
                ]
            )
            for key in ["identity", "host", "host_ca", "hop_key", "target_key"]:
                run(
                    "ssh-keygen",
                    "-q",
                    "-t",
                    "ed25519",
                    "-N",
                    {
                        "hop_key": "hop-key-password",
                        "target_key": "target-key-password",
                    }.get(key, ""),
                    "-f",
                    str(directory / key),
                )
            (directory / "authorized_keys").write_text(
                "".join(
                    (directory / (key + ".pub")).read_text()
                    for key in ["identity", "hop_key", "target_key"]
                )
            )
            run(
                "ssh-keygen",
                "-q",
                "-s",
                str(directory / "host_ca"),
                "-I",
                "owned-fixture",
                "-h",
                "-n",
                "127.0.0.1,target-ssh,fixture-target",
                "-V",
                "-1m:+1h",
                str(directory / "host.pub"),
            )
            (directory / "sshd_config").write_text(
                "\n".join(
                    [
                        "Port 22",
                        "ListenAddress 0.0.0.0",
                        "HostKey /fixture/host",
                        "HostCertificate /fixture/host-cert.pub",
                        "AuthorizedKeysFile /fixture/authorized_keys",
                        "StrictModes no",
                        "PermitRootLogin yes",
                        "PubkeyAuthentication yes",
                        "PasswordAuthentication yes",
                        "KbdInteractiveAuthentication no",
                        # Negative authentication cases must not penalize later
                        # positive connections from the same Docker source.
                        "PerSourcePenalties no",
                        "AllowTcpForwarding yes",
                        "UsePAM no",
                        "PidFile /tmp/fixture-sshd.pid",
                        "LogLevel INFO",
                        "",
                    ]
                )
            )
            for name, links in [
                (
                    target,
                    [f"{mysql}:mysql-ssh-target", f"{postgres}:postgres-ssh-target"],
                ),
                (jump, [f"{target}:target-ssh"]),
            ]:
                run(
                    "docker",
                    "run",
                    "--detach",
                    "--rm",
                    "--name",
                    name,
                    *[item for link in links for item in ["--link", link]],
                    "-p",
                    "127.0.0.1::22",
                    "-v",
                    f"{directory}:/fixture:ro",
                    "--entrypoint",
                    "/bin/sh",
                    "postgres:17-alpine",
                    "-c",
                    "apk add --no-cache openssh >/dev/null && echo 'root:"
                    + ("target-password" if name == target else "jump-password")
                    + "' | chpasswd && exec /usr/sbin/sshd -D -E /tmp/fixture-auth.log -f /fixture/sshd_config",
                    stdout=subprocess.DEVNULL,
                )
            port = (
                run("docker", "port", jump, "22", capture_output=True)
                .stdout.strip()
                .rsplit(":", 1)[1]
            )
            key = (directory / "host_ca.pub").read_text()
            (directory / "known_hosts").write_text(
                f"@cert-authority [127.0.0.1]:{port} {key}@cert-authority 127.0.0.1 {key}@cert-authority target-ssh {key}@cert-authority fixture-target {key}"
            )
            (directory / "empty_hosts").write_text("")
            shutil.copyfile(directory / "identity", directory / "identity-127.0.0.1")
            (directory / "identity-127.0.0.1").chmod(0o600)
            # -F is inherited by OpenSSH's native jump subprocess; no global SSH files change.
            (directory / "ssh_config").write_text(
                f'Host jump-alias\n    HostName 127.0.0.1\n    IdentityAgent none\nMatch host 127.0.0.1\n    IdentityFile "{directory / "identity-%h"}"\n'
                "Host target-alias\n    HostName target-ssh\n    HostKeyAlias fixture-target\n"
                f'Host *\n    UserKnownHostsFile "{directory / "known_hosts"}"\n'
                "    GlobalKnownHostsFile /dev/null\n    StrictHostKeyChecking yes\n    BatchMode yes\n"
            )
            (directory / "stall.py").write_text(
                "import os,subprocess,sys\n"
                "assert not any(os.environ.get(k) for k in ['CHOSCORDB_SSH_ASKPASS_TOKEN','CHOSCORDB_SSH_PROXY_TOKEN'])\n"
                "child=\"import socket,sys; h,p=open(sys.argv[1]).read().rsplit(':',1); s=socket.create_connection((h,int(p))); s.recv(1)\"\n"
                "subprocess.run([sys.executable,'-c',child,sys.argv[1]],check=True)\n"
            )
            (directory / "audit_identity.py").write_text(
                "import os,pathlib,stat,sys\n"
                "args=sys.argv[1:]; exposed='\\n'.join(args+list(os.environ.values()))\n"
                "for name in ['CHOSCORDB_SSH_HOP_KEY','CHOSCORDB_SSH_TARGET_KEY','CHOSCORDB_SSH_AGENT_KEY']:\n"
                " key=pathlib.Path(os.environ[name]).read_text(); assert key not in exposed, 'key material escaped secret boundary'\n"
                "for i,arg in enumerate(args[:-1]):\n"
                " if arg=='-S':\n"
                "  path=pathlib.Path(args[i+1]); assert stat.S_IMODE(path.parent.stat().st_mode)==0o700\n"
                "  with open(os.environ['CHOSCORDB_SSH_CONTROL_AUDIT'],'a') as out: out.write(str(path)+'\\n')\n"
                " if arg=='-i':\n"
                "  path=pathlib.Path(args[i+1])\n"
                "  if path.parent.name.startswith('choscordb-ssh-'):\n"
                "   assert stat.S_IMODE(path.stat().st_mode)==0o600, 'identity permission failure'\n"
                "   assert stat.S_IMODE(path.parent.stat().st_mode)==0o700, 'directory permission failure'\n"
            )
            (directory / "ssh").write_text(
                '#!/bin/sh\npython3 "$CHOSCORDB_SSH_IDENTITY_AUDIT" "$@" || exit 97\nfor arg in "$@"; do\n  if [ "$arg" = "-G" ] && [ -f "$CHOSCORDB_SSH_STALL_FLAG" ]; then exec python3 "$CHOSCORDB_SSH_STALL_SCRIPT" "$CHOSCORDB_SSH_STALL_ADDRESS"; fi\ndone\nexec "$CHOSCORDB_REAL_SSH" -vvv -E "$CHOSCORDB_SSH_LOG" -F "$CHOSCORDB_SSH_CONFIG" "$@"\n'
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
                CHOSCORDB_SSH_LOG=str(directory / "ssh.log"),
                CHOSCORDB_SSH_IDENTITY_AUDIT=str(directory / "audit_identity.py"),
                CHOSCORDB_SSH_STALL_SCRIPT=str(directory / "stall.py"),
                CHOSCORDB_SSH_STALL_ADDRESS=str(directory / "stall.address"),
                CHOSCORDB_SSH_STALL_FLAG=str(directory / "stall.flag"),
                CHOSCORDB_SSH_CONFIG=str(directory / "ssh_config"),
                CHOSCORDB_SSH_KNOWN_HOSTS=str(directory / "known_hosts"),
                CHOSCORDB_SSH_JUMP_PORT=port,
                CHOSCORDB_SSH_HOP_KEY=str(directory / "hop_key"),
                CHOSCORDB_SSH_TARGET_KEY=str(directory / "target_key"),
                CHOSCORDB_SSH_TRUST_PATH=str(directory / "approved_known_hosts"),
                CHOSCORDB_SSH_TARGET_CONTAINER=target,
                CHOSCORDB_SSH_CONTROL_AUDIT=str(directory / "control_paths"),
                CHOSCORDB_SSH_AGENT_KEY=str(directory / "identity"),
                TMPDIR=str(directory / "private-identities"),
                CHOSCORDB_SSH_EMPTY_HOSTS=str(directory / "empty_hosts"),
                CHOSCORDB_SSH_DB_CA=str(tls / "ca.crt"),
            )
            Path(env["TMPDIR"]).mkdir(mode=0o700)
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
                "docker",
                "exec",
                target,
                "sh",
                "-c",
                "adduser -D final && echo 'final:final-password' | chpasswd",
            )
            run(
                "cargo",
                "build",
                "-p",
                "choscordb-driver-mysql",
                "--example",
                "ssh_hops",
                "--locked",
                cwd=root,
            )
            # The real helper executable path exercises shell quoting and SSH token escaping.
            application_directory = directory / "app's % helpers"
            application_directory.mkdir()
            application = application_directory / "ssh_hops"
            shutil.copy2(root / "target/debug/examples/ssh_hops", application)
            for scenario in os.environ.get(
                "CHOSCORDB_SSH_SCENARIOS",
                "password,alias,hop-key,target-key,agent,configured,two-hops,five-hops,bad-hop-trust,wrong-tls,aux-stall,deadline,inline-target,inline-both,inline-unencrypted,inline-agent,inline-hop-file-target,inline-wrong-passphrase,inline-wrong-key,inline-bad-trust,inline-deadline,inline-aux-stall,inline-abort,trust,sharing,two-hops-sharing,binding,two-hops-binding,certificate-context",
            ).split(","):
                print("SSH hop scenario:", scenario, flush=True)
                run(str(application), scenario, env=env, cwd=root)
                assert not list(Path(env["TMPDIR"]).glob("choscordb-ssh-*")), (
                    "private SSH identities leaked"
                )
            run(
                "cargo",
                "build",
                "-p",
                "choscordb-driver-postgres",
                "--example",
                "ssh_hops_pg",
                "--locked",
                cwd=root,
            )
            postgres_application = application_directory / "ssh_hops_pg"
            shutil.copy2(
                root / "target/debug/examples/ssh_hops_pg", postgres_application
            )
            for scenario in os.environ.get(
                "CHOSCORDB_SSH_PG_SCENARIOS",
                ",".join(
                    [
                        "password",
                        "alias",
                        "hop-key",
                        "target-key",
                        "agent",
                        "configured",
                        "two-hops",
                        "five-hops",
                        "bad-hop-trust",
                        "wrong-tls",
                        "deadline",
                        "inline-target",
                        "inline-both",
                        "inline-unencrypted",
                        "inline-agent",
                        "inline-hop-file-target",
                        "inline-wrong-passphrase",
                        "inline-wrong-key",
                        "inline-bad-trust",
                        "inline-deadline",
                        "inline-abort",
                        "trust",
                        "sharing",
                        "two-hops-sharing",
                        "binding",
                        "two-hops-binding",
                        "certificate-context",
                    ]
                ),
            ).split(","):
                print("PostgreSQL SSH hop scenario:", scenario, flush=True)
                run(str(postgres_application), scenario, env=env, cwd=root)
                assert not list(Path(env["TMPDIR"]).glob("choscordb-ssh-*")), (
                    "private SSH identities leaked"
                )
            print(
                "Adversarial second hop accepts final password but rejects its own credential",
                flush=True,
            )
            run(
                "docker",
                "exec",
                target,
                "sh",
                "-c",
                "echo 'root:final-password' | chpasswd",
            )
            run(str(application), "two-hops-wrong-second", env=env, cwd=root)
            run(str(postgres_application), "two-hops-wrong-second", env=env, cwd=root)
            run(
                "docker",
                "exec",
                target,
                "sh",
                "-c",
                "echo 'root:target-password' | chpasswd",
            )
            # A jump accepting the target password must still reject its own wrong secret.
            run(
                "docker",
                "exec",
                jump,
                "sh",
                "-c",
                "echo 'root:target-password' | chpasswd",
            )
            print(
                "Adversarial jump accepts target password but rejects its own credential",
                flush=True,
            )
            run(str(application), "wrong-hop", env=env, cwd=root)
            run(str(postgres_application), "wrong-hop", env=env, cwd=root)

        finally:
            if (
                os.environ.get("CHOSCORDB_SSH_FIXTURE_LOG")
                and (directory / "ssh.log").exists()
            ):
                shutil.copyfile(
                    directory / "ssh.log", os.environ["CHOSCORDB_SSH_FIXTURE_LOG"]
                )
            if agent is not None:
                agent.terminate()
                try:
                    agent.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    agent.kill()
                    agent.wait()
            for name in [jump, target, mysql, postgres]:
                subprocess.run(
                    ["docker", "rm", "-f", name],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )


if __name__ == "__main__":
    main()
