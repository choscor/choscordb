# Streaming export service

`export` consumes an `ExportSource` positioned at the start of an existing result.
It never connects, executes SQL, reconnects, or repeats writes. Implementing the
source requires an original cursor and/or retained result spool. Core, CXX, and
Qt integration are not implemented in this crate.

Create a `FileSink` for the destination, then run export on the application worker
runtime. The sink's dedicated thread owns all temporary-file creation, writes,
sync, rename and cleanup. It accepts at most two 64 KiB chunks; normal export
waits for each write acknowledgement. The temporary file is in the destination
directory. Successful finish syncs the file and atomically replaces the destination
using `tempfile::persist`; failure or cancellation removes the temporary file.
No partial output replaces an existing destination. Rename is the commit boundary:
a cancellation arriving after successful streaming has entered finish may observe
a completed export. Cancellation before that boundary aborts and preserves the
old destination. This provides atomic replacement, not power-loss durability of
the containing directory.

Use the independent `Cancellation` token to cancel, including while waiting on a
source read or write. The source's async reads must be cancellation-safe. Drop the
sink if the caller abandons the export future itself. Optional progress uses
`try_send` on a caller-provided bounded channel; slow consumers drop intermediate
updates instead of blocking export. Byte progress is attempted after each additional
64 KiB of encoded cell/chunk output, plus page and final updates. Row counts only
advance after a complete row; a large deferred value therefore reports bytes
before its row completes. The returned `Progress` is authoritative.

Formats:

- CSV always quotes non-NULL fields and doubles quotes. NULL is an unquoted empty
  field; empty text is `""`. Binary uses a `\x` hexadecimal representation.
- JSON uses `{"columns":[column metadata],"rows":[[values],...]}`. Columns are
  positional, so duplicate names never overwrite values. Exact decimals use
  `{"decimal":"digits"}`, binary uses `{"binary_hex":"hex"}`, and native JSON
  uses `{"json":"original JSON text"}`. Nonfinite floats use a `float` string tag.
- JSON Lines begins with a columns metadata record, followed by one
  `{"row":[values]}` record per line using the same value encoding.
- SQL INSERT quotes each table-name component and column independently. Select
  SQLite or PostgreSQL literal rules; PostgreSQL strings use explicit E literals.
  Decimals must satisfy strict JSON numeric grammar before becoming raw numeric
  literals. NUL text and nonfinite floats are explicitly rejected instead of
  emitting invalid or unsafe SQL.

Memory does not grow with the number of rows or deferred value length. The service
holds one borrowed source page, a schema copy, and bounded encoding work. All rows
stream cell by cell; no full encoded row is assembled. `page_bytes` caps the source
page; `value_bytes` caps inline values and raw deferred chunks. The historical
`encoded_row_bytes` name now denotes the conservative per-cell/schema encoding
allocation allowance, not a maximum output row length. Schema and inline cell
preflight run before copying or escaping and use checked conservative bounds.

Deferred values require `ExportSource::read_value_chunk`. Requests are at most
64 KiB and also fit the configured raw and encoding allowances. Text validation
carries at most three incomplete UTF-8 bytes across chunks; invalid UTF-8 aborts
without replacement characters. Binary bytes encode incrementally as hex.
Offset, total length, kind consistency, request size, and forward progress are
validated. The legacy `resolve` hook remains for source compatibility but export
does not call it. Sources must enforce allocation limits before returning pages
and chunks and retain their backing storage throughout the export.

These conservative allowances are not a process RSS guarantee. The caller must
account for source pages, schema, encoding buffers, and sink transfer copies in
its shared memory policy. Partial output remains temporary on every error.

`ExportSink` is the public failure-injection seam. Tests cover disk-full write
failure without commit, real destination preservation and temporary cleanup,
exact decimals/duplicate columns, cancellation, JSONL records, SQL injection
attempts, deferred-row bounds, and property-based Unicode CSV round trips.
