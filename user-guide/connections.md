# Connections

A connection profile keeps the settings that open a connection to one database.
ChoscorDB has a driver for SQLite, PostgreSQL and MySQL.
This page tells you how to make, use and change connection profiles.

## Make a connection profile

1. In the `Connections` panel, click `+` (`New connection`).
   You can also choose `File` > `New SQLite session…`.
2. In the `New connection` dialog, click the database type: `PostgreSQL`, `MySQL` or `SQLite`.
3. In `Connection name`, type a name for the connection profile.
4. Fill in the fields for the driver.
   The sections below tell you about each driver.
5. To make sure that the settings are correct, click `Test connection`.
6. Click `Save profile` to save the connection profile.
   Or click `Save & connect` to save it and open a connection.

The status line of the dialog shows work in progress and errors.
A notification in the corner of the window shows a successful test or save.
If a connection fails, the dialog keeps your settings so that you can correct them.
If you change to another connection profile with unsaved changes, ChoscorDB asks before it discards them.

## Use a saved connection profile

The `Connections` panel shows your saved connection profiles.
To open a connection, click the connection profile in the list.
The navigator then shows its database objects in `Schema & objects`.
Click the connection profile again to hide its objects.
The connection stays open.
Right-click a connection profile to see more actions:

- `Connect` opens a connection.
- `Disconnect` closes the open connection.
  For more information, see [Disconnect](navigator.md#disconnect).
- `Edit…` opens the connection profile in the dialog.
- `Test connection` tests the saved settings.
- `Duplicate` makes a copy of the connection profile.
- `Delete…` removes the connection profile after you confirm.

A delete removes the saved settings and the saved password of the connection profile.
It does not change the database.

## Passwords

The `Password` field is masked.
ChoscorDB does not show a saved password in the form.

1. Type the password in `Password`.
2. To keep the password, select `Save credentials in OS credential store`.
3. Click `Save profile`.

If you do not select the check box, ChoscorDB uses the password only for this connection.
A saved password shows the text `Saved password — leave unchanged to keep`.
To keep the saved password, do not change the field.
To remove a saved password, clear the check box and click `Save profile`.

ChoscorDB keeps saved passwords only in the credential store of the operating system.
For the credential store of each platform, see [Saved passwords](install.md#saved-passwords).

## SQLite

A SQLite connection profile opens one database file.

1. In `Database file`, type the path of the file, or click `Browse…`.
2. To read the file without changes, select `Open read-only`.

`Database file` also accepts a `file:` URI or `:memory:` for a temporary database in memory.

## PostgreSQL

A PostgreSQL connection profile uses these fields:

| Field | Value |
| --- | --- |
| `Host` | A hostname, an IP address, or the absolute path of a Unix socket directory. |
| `Port` | The server port. The default is `5432`. |
| `Database` | Optional. If you leave it empty, PostgreSQL uses the username. |
| `Username` | Necessary. The database user. |
| `Password` | Optional. Leave it empty for authentication without a password. |

A new PostgreSQL connection profile tries TLS first.
If the server does not offer TLS, ChoscorDB uses an unencrypted connection.
A connection through a Unix socket does not use TLS or SSH.

ChoscorDB does not show PostgreSQL system schemas by default.
To show the catalog and temporary schemas, use the `Show system schemas` preference.
For more information, see [Preferences](sql-editor.md#preferences).

Some PostgreSQL commands cannot run inside a transaction block.
ChoscorDB runs these commands outside a transaction block:

- `VACUUM`, `CHECKPOINT`, `REINDEX` and `CLUSTER`
- `CREATE DATABASE`, `DROP DATABASE`, `CREATE TABLESPACE` and `DROP TABLESPACE`
- `CREATE INDEX` and `DROP INDEX`, also with `CONCURRENTLY`
- `ALTER SYSTEM`, `DISCARD ALL` and `REFRESH MATERIALIZED VIEW`

If you open a transaction with `BEGIN`, PostgreSQL rejects these commands and shows its error.

## MySQL

A MySQL connection profile uses these fields:

| Field | Value |
| --- | --- |
| `Host` | A hostname, an IP address, or the absolute path of a Unix socket file. |
| `Port` | The server port. The default is `3306`. |
| `Database` | Optional. If you leave it empty, ChoscorDB connects to the server and selects no database. |
| `Username` | Optional. If you leave it empty, ChoscorDB uses anonymous authentication. |
| `Password` | Optional. Leave it empty for authentication without a password. |

A new MySQL connection profile tries TLS first.
If the server does not offer TLS, ChoscorDB uses an unencrypted connection.
If the TLS handshake fails, ChoscorDB does not change to an unencrypted connection.
A connection profile that has TLS verification settings from an earlier version keeps those settings.

The SQL editor knows MySQL backtick quotes, backslash escapes in strings and `#` comments.
ChoscorDB uses the `NO_BACKSLASH_ESCAPES` and `ANSI_QUOTES` modes of the connection to find the statements.
ChoscorDB reads these modes when it connects.
After that, ChoscorDB reads them again only when a statement makes them unknown, for example a failed statement.

To run a script, choose `Run all`, or select more than one statement and run the selection.
ChoscorDB runs the statements one after the other.
Execution stops at the first error.
The results of the statements before the error stay available.
To see the result of the next statement, go to the last page of the current result and click `Next result`.
Statements that completed before an error stay committed, unless you run them after `BEGIN`.

## SSH tunnel

PostgreSQL and MySQL connection profiles can connect through an SSH tunnel.

1. Select `Connect through SSH tunnel`.
2. In `SSH host`, type the hostname or IP address of the SSH server.
3. In `SSH port`, type the port. The default is `22`.
4. In `SSH username`, type the SSH user.
5. In `Authentication`, select `SSH agent`, `Public key` or `Password`.
6. For `Public key`, select a `Private key source`: `Key file` or `Paste private key`.
7. If the key has a passphrase, type it in `SSH passphrase`.
8. For `Password`, type the SSH password in `SSH password`.

With an SSH tunnel, `Host` is the database address as the SSH server sees it.
`localhost` then refers to the SSH server.

ChoscorDB verifies the SSH host key strictly.
If ChoscorDB does not know the host key, the connection fails.
To approve a host key:

1. Click `Inspect host keys…`.
2. In the `Verify SSH host key` dialog, compare the `SHA256 fingerprint` with a trusted source.
3. In `Known-hosts file`, choose the file to update.
4. Click `Approve selected key`.
5. Click `Retry connection`.

On macOS and Linux, the SSH tunnels of one connection share one SSH login.
A navigator session or a second query then does not log in to the SSH server again.
On Windows, each SSH tunnel logs in to the SSH server.

ChoscorDB sends an SSH keepalive message every 30 seconds.
If the SSH server does not reply to 3 messages, ChoscorDB closes the tunnel and shows the error.

The `Connection timeout (seconds)` preference sets the time limit for new SSH tunnels.
For more information, see [Preferences](sql-editor.md#preferences).

## Sample databases

To try ChoscorDB with sample data, use the [example databases](../examples/databases/README.md).
