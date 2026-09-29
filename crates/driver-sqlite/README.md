# SQLite adapter

This crate implements the shared contracts in `choscordb-driver-api`. It owns
SQLite session behavior and conversion from `rusqlite` types to application
types. Bounded execution and page fetching are required implementations; shared
convenience methods use `DEFAULT_RESULT_MEMORY_BUDGET`. `rusqlite` remains a
registry dependency with bundled SQLite. Application storage and result processing
may use SQLite independently of this user-database adapter.

A dedicated thread owns each SQLite connection, prepared statement, live row cursor,
and temporary large-value file. Async requests use a bounded 32-command queue;
queue saturation returns `ResourceLimit`. No request executes SQLite or file I/O
on its caller's executor thread.

A connection has one live result cursor. A subsequent connection operation closes
that cursor. Application services must preserve fetched pages separately if users
need to revisit them. SQL is prepared once and stepped progressively; the adapter
never adds LIMIT/OFFSET, counts rows, or replays a write. SQLite's parser rejects
additional statements before executing the first; the adapter reports
`Unsupported`. Trailing comments and quoted semicolons are accepted.

Acquire a fresh cancellation handle immediately before each execute. Handles are
scoped to that execution generation; stale tokens cannot interrupt a newer query.
The application actor must discard cancelled queued commands before dispatch and
await cancellation settlement before advancing its command queue.

Owned pages and the retained lookahead row must fit the requested memory budget;
the shared convenience methods use 4 MiB. Text/blob payloads are deferred when
they exceed their share of the row budget, with at most 16 KiB inline per value.
A row whose structural storage cannot fit returns `ResourceLimit`. These
allocations are separate from the application's result cache budget; SQLite
itself can allocate additional execution memory (for example sorting or
constructing a large value).
The million-row test verifies progressive adapter pages, not arbitrary SQLite
query-plan memory limits or process RSS.

Larger values are written directly from SQLite's borrowed value into an anonymous
temporary file. Handles encode file offsets and cursor generations, so there is
no growing in-memory offset index. The file is capped at 4 GiB per cursor.
Independent `DeferredReader` handles keep the file available after cursor or
connection close and expose bounded chunks. Whole-value detail loading caps a
single allocation at 64 MiB; larger requests return `ResourceLimit` and can use
the chunked reader instead.

SQLite dynamic values retain their native storage class. A NUMERIC-affinity value
already converted to SQLite REAL cannot recover the original decimal digits.
Column declarations are preserved; computed-column type/nullability and declared
precision/scale are not currently inferred.
