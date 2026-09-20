# Disposable PostgreSQL integration fixture

Install PostgreSQL 17 locally (on macOS: `brew install postgresql@17`). The helper uses its own cluster under `build/integration/postgres`; it does not start the Homebrew service or modify the system trust store. Supply `--bin-dir` for another PostgreSQL installation.

```sh
python3 scripts/integration/postgres_fixture.py start
python3 scripts/integration/postgres_fixture.py status
```

The fixture listens on a dynamically selected loopback port, requires SCRAM password authentication, and creates a private CA plus a server certificate valid for `localhost`. Its credentials are disposable test data. The helper rejects a nonempty directory without its ownership marker.

After building the native tests, run the adapter and desktop checks with the fixture environment:

```sh
python3 - <<'PYTEST'
import json, os, subprocess
values = json.loads(subprocess.check_output([
    'python3', 'scripts/integration/postgres_fixture.py', 'env']))
env = dict(os.environ, **values, CARGO_INCREMENTAL='0', QT_QPA_PLATFORM='offscreen')
subprocess.run(['cargo', 'test', '-p', 'choscordb-driver-postgres', '--locked',
                '--', '--include-ignored', '--test-threads=1'], env=env, check=True)
subprocess.run(['cargo', 'test', '-p', 'choscordb-core',
                '--test', 'postgres_disconnect', '--locked', '--',
                '--include-ignored', '--test-threads=1'], env=env, check=True)
subprocess.run(['ctest', '--preset', 'dev', '--output-on-failure'], env=env, check=True)
PYTEST
```

The `restart` integration executable is exclusive: it verifies the fixture ownership marker, stops only this repository's cluster, checks that the existing connection becomes terminal, restarts the cluster, and proves a fresh connection works. Its drop guard restarts the fixture if an assertion fails after shutdown. Do not run another fixture-backed test concurrently with it.

Ordinary workspace tests skip live PostgreSQL cases. This command includes metadata tests, typed values, writes before fetch, transactions, stale cursors, cancellation, timeouts, deferred values, and TLS trust/hostname/authentication failures. The native case covers profile Test/Connect with verified TLS, paging, cancellation, and query recovery. These checks do not prove all-platform behavior or the complete PRD memory/performance requirements.

Stop the isolated server after testing:

```sh
python3 scripts/integration/postgres_fixture.py stop
```

Its marked directory remains available for a subsequent start. Fixture certificates and passwords are solely for local tests.

## SSH tunnel coverage

The live driver suite can also run through an SSH server with TCP forwarding
allowed. The database host and port are resolved from that server. Configure
key/agent authentication and verify the SSH host key in your OpenSSH known-hosts
file before running; tests never accept an unknown key automatically.

Add these variables to the existing PostgreSQL fixture environment:

```sh
export CHOSCORDB_TEST_POSTGRES_SSH_HOST=bastion.example
export CHOSCORDB_TEST_POSTGRES_SSH_PORT=22
export CHOSCORDB_TEST_POSTGRES_SSH_USER=operator
export CHOSCORDB_TEST_POSTGRES_SSH_IDENTITY=/absolute/path/to/private-key
cargo test -p choscordb-driver-postgres --test live --locked -- --ignored --test-threads=1
```

The identity variable is optional when using an SSH agent or default OpenSSH
identities. The existing live cases exercise queries, transactions, cancellation,
disconnect, and PostgreSQL certificate/hostname verification over the tunnel.
The deterministic `ssh` test separately verifies that an unavailable SSH server
never falls back to a direct database connection.
