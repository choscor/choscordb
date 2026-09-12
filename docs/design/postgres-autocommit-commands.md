# PostgreSQL auto-commit commands

This reference defines PostgreSQL command execution for PRD §5.4 before implementation.

An auto-commit result query uses the existing transaction-scoped portal so results remain paged and bounded. PostgreSQL commands known to require or support execution outside an explicit block use its implicit single-statement transaction. This includes database/tablespace administration, `VACUUM`, `CHECKPOINT`, `ALTER SYSTEM`, `DISCARD ALL`, `CLUSTER`, materialized-view refresh, and ordinary or concurrent index creation, removal, and reindexing.

The adapter lexically selects this bounded command set, then prepares exactly one statement before execution. Preparation and schema accounting still precede execution, and statements with parameters remain rejected. Manual mode always keeps the explicit transaction path; a command PostgreSQL prohibits there returns the server error and leaves transaction control explicit. The adapter never retries a statement after a server error.

The direct path preserves cancellation, timeout, affected-row counts, bounded notices, SQLSTATE errors, and the normal empty result cursor contract. It restores session timeout configuration after completion or failure. Disconnect makes the connection terminal even if a direct command or its cleanup is blocked.

Acceptance covers an ordinary zero-column write, `VACUUM`, a disposable `CREATE DATABASE`/`DROP DATABASE` pair where fixture permissions allow it, manual-mode rejection, single-statement enforcement, cancellation, timeout cleanup, and a following query that proves the connection remains usable.
