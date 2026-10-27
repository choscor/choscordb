# Tests and fixtures

This page describes the test layers of ChoscorDB and the disposable database fixtures.
It also describes the desktop performance probe.
Read [Build and run](build.md) to build the tests.
Read [Continuous integration and quality gates](ci.md) for the gates that run them.

## Test layers

| Layer | Command | Scope |
| --- | --- | --- |
| Rust | `cargo test --workspace --locked` | Engine, drivers, storage and policy. SQLite tests use real databases. |
| Native | `ctest --preset dev` | Qt UI and bridge behavior, with the Qt `offscreen` platform. |
| Live database | `cargo test ... -- --include-ignored` | PostgreSQL and MySQL tests that need a running server. |

Test policy in Rust.
Keep C++ tests on UI and bridge behavior.
The usual test runs skip the live PostgreSQL and MySQL tests.
These tests have the `#[ignore]` attribute and need a fixture from this page.

## Rules for fixtures

- Each fixture uses disposable credentials.
  Do not use them for a different purpose.
- Each fixture listens on the loopback interface only.
- The fixtures do not change the system trust store or `~/.ssh`.
- Run fixture-backed tests with `--test-threads=1`.

## Example databases

`examples/databases/` contains a small sample dataset for manual tests.
The dataset has five customers, six products, eight orders and an `order_summary` view.

- For SQLite, run `python3 examples/databases/setup_sqlite.py`. It needs only Python 3.
- For PostgreSQL, run `docker compose -f examples/databases/compose.yaml up -d --wait`.
- The Compose project also starts an OpenSSH server for SSH tunnel tests.

Read the [example databases README](../examples/databases/README.md) for the connection values and the SSH steps.
`scripts/ci/test_example_databases.py` tests these examples.

## PostgreSQL fixture

`scripts/integration/postgres_fixture.py` manages a local PostgreSQL cluster that only this repository owns.
It has the actions `start`, `status`, `env` and `stop`.

The fixture has these properties:

- It keeps the cluster in `build/integration/postgres`. Use `--directory` to change it.
- It uses PostgreSQL from `/opt/homebrew/opt/postgresql@17/bin`. Use `--bin-dir` for a different installation.
- It listens on a free loopback port that it selects at start.
- It requires SCRAM password authentication.
- It creates a private CA and a server certificate for `localhost`.
- It creates an encrypted PKCS#12 client identity for the `tls_client` role.

The fixture writes an ownership marker, `fixture.json`, in its directory.
It refuses a directory that is not empty and has no marker.
It does not start the Homebrew service.

### Run the PostgreSQL tests

1. Install PostgreSQL 17, for example with `brew install postgresql@17`.
2. Build the native tests with `cmake --build --preset dev`.
3. Start the fixture:

   ```sh
   python3 scripts/integration/postgres_fixture.py start
   python3 scripts/integration/postgres_fixture.py status
   ```

4. Run the tests with the fixture environment:

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

5. Stop the fixture:

   ```sh
   python3 scripts/integration/postgres_fixture.py stop
   ```

The `env` action prints the connection values as JSON.
The variables start with `CHOSCORDB_TEST_POSTGRES`.
They include the connection string, the CA path, the client identity and its password.
The marked directory stays after `stop`, so you can start the same cluster again.

The `restart` test stops and starts the fixture cluster.
It checks that the old connection fails and that a new connection works.
If an assertion fails after the stop, the test starts the cluster again.
Do not run a different fixture-backed test at the same time as `restart`.

### PostgreSQL over an SSH tunnel

The live driver suite can also connect through an SSH server that allows TCP forwarding.
The SSH server resolves the database host and port.
Before you run the tests, do these steps:

1. Set up key or agent authentication for the SSH server.
2. Add the SSH host key to your OpenSSH known-hosts file.
3. Set the SSH variables in addition to the fixture environment:

   ```sh
   export CHOSCORDB_TEST_POSTGRES_SSH_HOST=bastion.example
   export CHOSCORDB_TEST_POSTGRES_SSH_PORT=22
   export CHOSCORDB_TEST_POSTGRES_SSH_USER=operator
   export CHOSCORDB_TEST_POSTGRES_SSH_IDENTITY=/absolute/path/to/private-key
   ```

4. Run the live suite:

   ```sh
   cargo test -p choscordb-driver-postgres --test live --locked -- --ignored --test-threads=1
   ```

`CHOSCORDB_TEST_POSTGRES_SSH_IDENTITY` is optional with an SSH agent or the default OpenSSH identities.
The tests never accept an unknown host key.
The `ssh` test checks that ChoscorDB does not connect directly when the SSH server is not available.

### PostgreSQL TLS modes

The `tls_modes` test uses the fixture CA and the client identity:

```sh
cargo test -p choscordb-driver-postgres --test tls_modes --locked -- --include-ignored
```

It tests these cases:

- `Require` with a server certificate that is not trusted.
- CA verification without hostname verification.
- Full hostname verification.
- A PEM trust bundle with more than one certificate.
- Client identities that are absent, incorrect or valid.
- `Prefer` falls back to plaintext when the server declines TLS, and `Require` refuses.

## MySQL fixture

`scripts/integration/mysql_ci_fixture.py` starts a local MySQL 8.4 server for the live tests.
It has the actions `start` and `stop`.

The fixture has these properties:

- It keeps the server in `build/integration/mysql`. Use `--directory` to change it.
- `start` needs `--bin-dir`, the `bin` directory of a MySQL 8.4 installation.
- It listens on `127.0.0.1:33306`.
- It creates the `choscordb_test` database.
- The `root` user has the password `choscordb-test-password`.
- It writes the server log to `server.log` in its directory.

### Run the MySQL tests

1. Install MySQL 8.4, for example with `brew install mysql@8.4`.
2. Start the fixture:

   ```sh
   python3 scripts/integration/mysql_ci_fixture.py start --bin-dir "$(brew --prefix mysql@8.4)/bin"
   ```

3. Run the driver tests:

   ```sh
   cargo test -p choscordb-driver-mysql --lib --test live --test editing --test driver_contract --locked -- --include-ignored --test-threads=1
   ```

4. Run the core tests:

   ```sh
   cargo test -p choscordb-core --test mysql --locked -- --include-ignored --test-threads=1
   ```

5. Run the native MySQL workspace tests:

   ```sh
   CHOSCORDB_TEST_MYSQL=1 QT_QPA_PLATFORM=offscreen build/dev/choscordb-workspace-tests
   ```

6. Stop the fixture:

   ```sh
   python3 scripts/integration/mysql_ci_fixture.py stop
   ```

If your server uses a different local port, set `CHOSCORDB_MYSQL_PORT`.
The tests create and drop objects, so use only a test database.

### MySQL fixtures in Docker

Some MySQL fixtures need Docker.
Each fixture starts its own containers, runs its tests and then removes the containers and keys.

| Script | What it tests | Needs |
| --- | --- | --- |
| `scripts/integration/mysql_tls_fixture.py` | TLS modes, CA and hostname verification, client certificates | Docker, OpenSSL |
| `scripts/integration/mysql_ssh_fixture.py` | SSH public-key forwarding, host-key rejection, SSH passwords, encrypted private keys | Docker, OpenSSH, a MySQL container |
| `scripts/integration/mysql_jump_fixture.py` | SSH ProxyJump with an isolated agent and host trust | Docker, OpenSSH |
| `scripts/integration/ssh_hop_credentials_fixture.py` | Separate credentials for each SSH hop | Docker, OpenSSH |

`mysql_ssh_fixture.py` uses a MySQL container that you start.
Set `CHOSCORDB_MYSQL_CONTAINER` to the container name.
The default name is `choscordb-mysql-implementation`.
To start a container, run:

```sh
docker run --detach --rm --name choscordb-mysql-implementation \
  -e MYSQL_ROOT_PASSWORD=choscordb-test-password \
  -e MYSQL_DATABASE=choscordb_test \
  -p 127.0.0.1:33306:3306 mysql:8.4
```

Wait until `docker exec choscordb-mysql-implementation mysqladmin ping --silent` succeeds.
Then run the fixture.

## Network round-trip budgets

The `round_trips` tests count the network round trips of the PostgreSQL and MySQL drivers.
`crates/driver-api/tests/support/round_trip_proxy.rs` is a TCP proxy between the driver and the fixture.
The proxy counts the accepted connections and the turnarounds.
A turnaround is a client message that gets a server reply.

Each test compares the count with a budget constant in the test file.
For example, the PostgreSQL tests have `PAGE_BUDGET` and `SELECT_BUDGET`.
The MySQL tests have `BASELINE_CONNECT` and `BASELINE_SECOND_OPEN`.
If a change adds round trips, the test fails.
Do not increase a budget to make a test pass. Find the cause.

Run the tests with the applicable fixture:

```sh
cargo test -p choscordb-driver-postgres --test round_trips --locked -- --include-ignored --test-threads=1
cargo test -p choscordb-driver-mysql --test round_trips --locked -- --include-ignored --test-threads=1
```

The proxy itself has unit tests in `crates/driver-api/tests/round_trip_proxy.rs`.

`scripts/integration/mysql_ssh_fixture.py` records each OpenSSH call in the `ssh-calls` file of the fixture.
The `one_connect_uses_one_ssh_login` test counts the master calls (`-M`) and the calls without a control socket (`-S`).
It does not count the `-G` calls, because they only print the configuration.

## Query settings fixture

`scripts/integration/query_settings_fixture.py` tests the recovery from corrupt query settings.
It creates SQLite metadata databases with bad query preferences.
Then it runs the native `query_settings_corruption_test` binary on each database.
CTest runs this fixture as the `query-settings-corruption` test.

## Desktop performance probe

`tests/performance/desktop_probe.cpp` measures the desktop frontend in a Release build.
It links the production desktop library and uses a temporary metadata database.
It adds no controls to the UI.

### Build and run the probe

1. Configure a Release build with the probe:

   ```sh
   cmake --preset release -DCMAKE_PREFIX_PATH=/opt/homebrew -DCHOSCORDB_BUILD_PERFORMANCE_PROBE=ON
   ```

2. Build the probe:

   ```sh
   cmake --build --preset release --target choscordb-desktop-probe
   ```

3. Run the probe through the wrapper script:

   ```sh
   python3 scripts/performance/desktop.py build/release/choscordb-desktop-probe build/performance/report.json
   ```

`CHOSCORDB_BUILD_PERFORMANCE_PROBE` is off by default.
The probe is a plain executable, not an app bundle, on all platforms.
The probe takes one argument, the path of the JSON report.
A Debug build writes a report with the status `unverified` and stops.
The script stops the probe after 600 seconds by default.
Use `--timeout` to change this limit.

### What the probe measures

The probe does these steps in sequence:

1. Records the time to the first window paint and to a ready workspace.
2. Samples memory after one second of idle time.
3. Opens an in-memory SQLite connection and samples memory again.
4. Types 200 keys in the SQL editor and records the key-to-paint p95 latency.
5. Runs a recursive query of one million rows and moves through 1,000 pages of 1,000 rows.
6. Checks the first and last values of each page and the number of query submissions.
7. Goes back to an archived page to check navigation after eviction.
8. Runs a query of 10,000 rows and 30 columns with mixed types, with a page size of 10,000.
9. Sorts that result by the first column and then filters it.
10. Closes the window and records the shutdown time.

The probe records peak RSS, the macOS physical footprint, core reservations and hot-cache peaks.
It also records the longest event dispatch in each phase.
For the wide result, the report has `wide_page_ready_ms`, `wide_sort_ms` and `wide_filter_ms`.
Each of these has a `_notify_max_ms` key with the longest dispatch of the step.
`wide_page_rows` gives the rows on the first page.
The engine page byte budget can stop a wide page before 10,000 rows.
The report includes the platform, the CPU, the Qt version, the build mode and the rendering backend.

`scripts/performance/desktop.py` adds times that it observes from outside the process.
These are the time to ready, the time from the shutdown marker to exit and the total time.

### Limits of the probe

- A fresh profile is not a cold-cache start.
- The first-paint time does not include process creation and loader time.
- The `offscreen` platform cannot measure displayed input latency.
- Memory samples can miss short peaks.
- The probe measures only a SQLite connection.
- The sort and the filter of the wide result use the engine result view directly.

The report lists its limitations.
A metric that is not available is unverified.
Do not report it as zero or as a pass.
