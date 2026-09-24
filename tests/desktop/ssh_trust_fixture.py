#!/usr/bin/env python3
"""Run Qt trust-controller tests with deterministic keyscan output.

The Qt process receives its environment before startup. Real inspection, hashing,
bridge delivery and approval-file operations run; only network keyscan output is
replaced. Native sshd integration is separate transport evidence.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="choscordb-qt-trust-") as temporary:
        directory = Path(temporary)
        identity = directory / "fixture"
        subprocess.run(
            ["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", str(identity)],
            check=True,
        )
        key_type, public_key, *_ = identity.with_suffix(".pub").read_text().split()
        line = f"192.0.2.10 {key_type} {public_key}"
        scanner = directory / "ssh-keyscan"
        scanner.write_text(
            "#!/bin/sh\n"
            'if [ "$CHOSCORDB_QT_TRUST_DELAY" = 1 ]; then /bin/sleep 0.25; fi\n'
            f"printf '%s\\n' {shlex.quote(line)}\n"
        )
        scanner.chmod(0o700)
        environment = os.environ.copy()
        environment.update(
            PATH=str(directory) + os.pathsep + environment.get("PATH", ""),
            QT_QPA_PLATFORM="offscreen",
            CHOSCORDB_QT_TRUST_FIXTURE="1",
        )
        for name, delay in [
            ("sshHostKeyStaleInspectionSuccessCannotOpenApproval", "1"),
            ("sshHostKeyReviewRequiresSelectionAndShowsExactFingerprint", "0"),
        ]:
            environment["CHOSCORDB_QT_TRUST_DELAY"] = delay
            subprocess.run([str(executable), name], env=environment, check=True)


if __name__ == "__main__":
    main()
