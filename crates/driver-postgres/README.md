# PostgreSQL adapter

`PostgresDriver` implements the shared driver contracts. A bounded command worker
owns the client. A local transaction scope owns each server portal, fetching at
most one wire row per Execute message. Decoding and temporary-file I/O run on
blocking workers. The first bounded row/command executes before execute returns. Automatic
transactions commit at EOF or cursor close; manual
transactions persist until explicit commit/rollback. Preparing a single statement
and checking owned column metadata precede Bind/Execute and database writes.
Auto-commit commands that PostgreSQL forbids inside explicit transaction blocks
use its implicit single-statement transaction instead; manual mode preserves the
server rejection and explicit transaction state.

Connections use native platform TLS verification and system roots by default.
An optional PEM root certificate augments those roots; it never disables hostname
or certificate verification. Plaintext requires explicit `TlsMode::Disable`.
Passwords do not enter generated SQL or error logs. Notices retain bounded
display messages with SQLSTATE and UTF-8-safe truncation; summary logging remains
redacted. Query errors retain their server SQLSTATE and displayable message.

Cancellation tokens target one execution generation. Native cancellation bypasses
the command queue, and disconnect interrupts a busy portal. Deferred text/bytea
values use a temporary file with authenticated position-bound handles; independent
readers survive cursor close, a subsequent query, and connection close.

Current limits (not full PRD completion):

- tokio-postgres buffers an individual wire DataRow and prepared metadata before
  this adapter can inspect them. Returned pages and deferred read chunks have
  allocation bounds, but an exceptionally wide server row is not yet covered by
  the strict end-to-end memory invariant.
- Unknown scalar OIDs fail explicitly. Precision/scale are available from
  navigator metadata and prepared result column modifiers. Result
  nullability remains unknown: protocol source origins cannot establish whether
  joins/grouping introduce NULL. Navigator metadata retains declared nullability.
- Temporary value storage has a 4 GiB per-result ceiling. It is local temporary
  storage, not recoverable persisted query output.
- Native connection setup has a 15-second TCP connect timeout. Query timeouts use
  server statement_timeout plus the execution deadline checked between row reads.

Live regression tests require a disposable PostgreSQL fixture:

```sh
CHOSCORDB_TEST_POSTGRES='host=localhost port=5432 user=fixture dbname=postgres password=fixture sslmode=disable' \
CHOSCORDB_TEST_POSTGRES_ROOT_CERTIFICATE=/path/to/test-ca.pem \
CARGO_INCREMENTAL=0 cargo test -p choscordb-driver-postgres --test live -- --ignored
```

The TLS regression expects the certificate to match `localhost`, not `127.0.0.1`,
and to be trusted only by the supplied CA. The fixture must require password
authentication. The adapter never changes the user's system trust store.

Closing during metadata or DDL aborts the PostgreSQL transport. A sticky close
signal also prevents a queued auxiliary operation from starting after close;
ordinary query cancellation remains scoped to its execution generation. Normal
closes retain the graceful rollback path. Transport-abort close acknowledges local
termination; PostgreSQL may finish rollback asynchronously as it detects EOF.

The opt-in `auxiliary_close` integration test needs exclusive fixture use: it
briefly locks `pg_catalog.pg_namespace`, with bounded acquisition and explicit
rollback before assertions. It checks metadata and DDL, manual and automatic
pending writes, backend termination, and committed state through an observer.
Run with the same fixture environment as above:

```sh
CARGO_INCREMENTAL=0 cargo test -p choscordb-driver-postgres --test auxiliary_close -- --ignored
```

## SSH connections

In a PostgreSQL connection profile, enable **Connect through SSH tunnel** and enter the SSH
host, port, and username. Optionally enter a private-key file path. Keep the
PostgreSQL host and port set to the database address reachable from the SSH
server (for example, `localhost:5432` for a database on that server).

Choose SSH agent, public key, or password authentication. Public-key profiles
accept an optional encrypted-key passphrase. Passwords and passphrases can stay
session-only or be saved in the operating-system credential store. They are
delivered to the system `ssh` executable through the application's authenticated
loopback askpass broker and never appear in command arguments or profile files.
Verify and trust the server key with OpenSSH before connecting. Unknown or changed
host keys fail closed. OpenSSH configuration can supply proxy settings.

Each database connection owns its SSH process and forwards over standard input
and output without opening a local listening port. Connection failures never
fall back to a direct connection. Query cancellation opens a separate SSH
forward to the same database; closing or dropping a connection terminates its
tunnel. SSH connection and cancellation handshakes have a 15-second deadline.
TLS settings remain independent of SSH: verification uses the configured
PostgreSQL hostname and optional root certificate. Saved profiles contain only
SSH settings, the key path, and opaque credential references, never key contents
or SSH credentials.
