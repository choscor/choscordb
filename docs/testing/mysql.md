# MySQL support

Choose **MySQL** when creating a connection profile. Enter the server, user, and password; the default port is **3306**.
The database is optional: leave it empty to connect without selecting a default
schema, or enter a database to select it at login. Passwords can remain scoped to
the session or be saved in the existing operating-system credential store.
TLS verifies the server certificate and hostname by default. Use the root
certificate field for a private certificate authority. Disable TLS only when
appropriate for your server, such as the disposable local fixture below.

The adapter supports queries, paged results, exact decimal and unsigned integer
values, transactions, database/table/view/column browsing, object data, and DDL.
The SQL editor recognizes MySQL backticks, backslash-escaped strings, and `#`
comments. Generated object templates use MySQL quoting and placeholders.
CSV, JSON, JSON Lines, and MySQL SQL INSERT exports use the shared export flow.
MySQL SQL exports encode text as UTF-8 hexadecimal expressions using
[CONVERT](https://dev.mysql.com/doc/refman/8.4/en/cast-functions.html), so quote
and backslash contents survive differing server SQL modes.

SSH profiles support password, public-key, and agent authentication with strict
host-key verification. An encrypted private key can use a session-only or securely
saved passphrase. SSH passwords and passphrases are supplied through the local
askpass broker and do not enter profile metadata or command arguments. The original
database hostname remains the TLS verification name when traffic passes through
the tunnel.

InnoDB table grids support reviewed, bound INSERT/UPDATE/DELETE batches. Updates
and deletes require a primary key and compare the original values; a conflict
rolls back the entire batch. Commit or roll back an existing manual transaction
before applying grid edits. Views and nontransactional tables remain read-only;
tables without a primary key allow inserts only. Simple SELECT projections can
be editable when their source columns and primary key are unambiguous.

Results spool incrementally: pages become available while later rows are still
arriving. Text and binary values larger than 16 KiB are stored separately and
read through bounded chunks. Result storage still needs temporary disk space.
Cancellation and timeouts use a separate control connection to interrupt the
query and drain its response, preserving the SQL session when recovery succeeds.
If recovery fails, reconnect before continuing. Object reads remain independent
of the SQL session.

The object navigator loads bounded metadata pages. Use **Load more…** to continue
past the first page; there is no 10,000-object total cutoff. Refresh the parent if
objects are created or dropped while paging.

Select multiple statements in the editor to run a script sequentially. Execution
stops at the first error, retaining earlier completed results. Use **Next result**
after the current result's final page to inspect later statement or stored
procedure results, including results with different columns. Statements already
committed before an error are not undone automatically; use an explicit
transaction when appropriate. Exports apply to the currently selected result.

The editor refreshes the session's `NO_BACKSLASH_ESCAPES` and `ANSI_QUOTES` modes
before selecting the statement to execute.
Script execution refreshes the mode between statements, including after a
`SET sql_mode` statement. Common MySQL metadata statements are recognized;
unknown grammar still prompts for confirmation.

Remaining bounds and constraints:

- Native network packets are capped at 64 MiB; large values are deferred after
  decoding. Server `max_allowed_packet` settings can impose a smaller bound.
- Native transport memory uses a separate fixed admission budget: at most eight
  live SQL/object transports and two active row-decoding workers. Cancellation
  has two additional control slots with 1 MiB packet limits. Transport slots
  remain held while an idle connection retains its buffers; close unused MySQL
  connections or object tabs when this budget is full. These allocations are
  separate from the configurable core page-transfer budget. The upstream client
  also caches packet buffers (by default, at most 128 buffers of 4 MiB each).
- Deferred value storage is bounded to 4 GiB per result source. Page/schema
  memory budgets and available temporary disk space still apply.
- Compound routine definitions with internal statement separators are not
  supported; create them with a database administration tool. Calling existing
  routines and reading their multiple results is supported. The `mysql`
  command-line client's `DELIMITER` directive is not SQL and is not supported.
- Grid editing rejects ambiguous query shapes and deferred original values when
  it cannot establish a safe conflict comparison.

## Disposable integration server

Docker is required for this fixture. The following credentials are exclusively
for an isolated local test database; the port is bound to localhost.

```sh
docker run --detach --rm --name choscordb-mysql-test \
  -e MYSQL_ROOT_PASSWORD=choscordb-test-password \
  -e MYSQL_DATABASE=choscordb_test \
  -p 127.0.0.1:33306:3306 mysql:8.4
```

Wait until `docker exec choscordb-mysql-test mysqladmin ping --silent` succeeds,
then run:

```sh
cargo test -p choscordb-driver-mysql --lib --test live --test editing --test server_connection --locked -- --include-ignored --test-threads=1
cargo test -p choscordb-core --test mysql --locked -- --include-ignored --test-threads=1
```

The tests use `127.0.0.1:33306`, database `choscordb_test`, and the fixture's root
credentials. Set `CHOSCORDB_MYSQL_PORT` if you map a different local port. Tests
create disposable objects and must run against a test database. The regular
workspace suite skips tests requiring a server; the MySQL CI job runs them.

Remove the fixture when finished:

```sh
docker stop choscordb-mysql-test
```

For the disposable SSH integration fixture (Docker and OpenSSH required), run:

```sh
CHOSCORDB_MYSQL_CONTAINER=choscordb-mysql-test python3 scripts/integration/mysql_ssh_fixture.py
```

This exercises unencrypted public-key forwarding, strict host-key rejection,
SSH password authentication, and an encrypted private key with a passphrase.

It creates temporary keys and host-key storage, runs a real SSH server alongside
the MySQL fixture, and cleans up its resources without changing `~/.ssh`.

Run the native MySQL workspace regressions against the same database:

```sh
CHOSCORDB_TEST_MYSQL=1 QT_QPA_PLATFORM=offscreen build/dev/choscordb-workspace-tests
```

## Native TLS policy and client identity tests

`Disable` uses plaintext TCP. `Require` requires encryption without checking the
server certificate. `VerifyCa` additionally validates its CA chain; `VerifyFull`
(the default) also validates the database hostname. MySQL's native client does
not expose a safe `Prefer` downgrade policy, so that mode is rejected explicitly.
A PKCS#12 (`.p12`/`.pfx`) client identity can supply a certificate and private key;
its archive password is a separate credential, never part of the saved profile.
Custom trust files accept PEM certificate bundles or DER certificates.

Run the isolated TLS fixture (Docker and OpenSSL required):

```sh
python3 scripts/integration/mysql_tls_fixture.py
```

It creates disposable CA/server/client certificates, starts its own loopback-only
MySQL container, runs encryption, CA/hostname validation, and client-certificate
authentication tests, then removes the container and keys. It does not modify the
system trust store or connect to an existing database.
