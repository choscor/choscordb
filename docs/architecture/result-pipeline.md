# Result pipeline integration contract

This document records the remaining integration work; it is not evidence that the global memory requirement is met.

## Ownership

A connection actor owns its database connection and active cursor. Independent connection actors may progress concurrently. SQL is executed once; query-result export and backward paging must never re-execute a statement. A disk result store retains pages as they arrive. A single application cache owns the hot pages and enforces the per-result and global limits. The UI owns only its visible transferred page and requests missing pages asynchronously.

The generational connection/query identity is independent of a page ordinal. Request IDs accompany metadata refreshes so obsolete responses cannot replace a newer subtree. Result-store offsets are internal disk addresses, never UI handles.

## Integration state and remaining verification

1. Disk-backed indexed paging is now integrated. Each actor awaits blocking-pool storage operations and persists pages before exposing them. Existing ordinals read from disk; the next unfetched ordinal advances the original cursor. Stored offsets support variable-size pages. Retained results survive new SQL/transactions until release. The shared hot LRU is now in front of this store; hits clone under an existing transfer reservation, and misses read disk.
2. Schema/page reservations are now integrated across core work, queued events, CXX DTOs, and visible Qt allocations. Capacity exhaustion pauses before fetching, and release/disconnect can interrupt the wait. Reservations survive event dequeue and are shrunk only after conversion buffers are destroyed. Hot-cache entries now hold their own reservations and can be evicted before a transfer waits. Bounded deferred-value events now carry equivalent reservations; the legacy full-value compatibility call remains outside this bounded path. Continue testing stopped event consumers across multiple connections and real allocation peaks.
3. Independent SQLite deferred readers now preserve disk backing for the complete result lifetime, including after another query starts. Bounded chunk reads run on blocking workers and retain reservations through queued events and the visible detail model. Query release/disconnect retires the reader; native query replacement closes its detail window.
4. Export now replays persisted pages from row zero, then advances the original remaining cursor through the same fetch/persist path as paging. Every row is encoded cell by cell; deferred values use bounded chunks. The destination is preserved until successful atomic publication. Cancellation/failure aborts temporary output; an incomplete archived result fails instead of silently exporting only a prefix. Export never resubmits SQL. The native dialog exposes formats, destination, progress, cancellation, and retry of the export operation.
5. Release query pages, files, cache allocations, event leases, and deferred handles on explicit result/tab release. Keep released generations stale even when a slot is reused. Shutdown cancels outstanding workers and has a measured deadline.

## Current evidence and limits

- SQLite traverses a million-row recursive source in 1,000 pages without an application-owned million-row vector.
- The standalone LRU charges its nodes and value capacities and refuses insertion when pinned pages consume the budget.
- The Qt table model has its own default 64 MiB allocation budget and rejects oversized transfers atomically.
- The core now verifies a complete million-row indexed traversal and disk rereads of pages 0 and 999, with one page held by the test at a time.
- Core/CXX/Qt schema-page reservations now share the application budget. Bounded deferred-value inspection and export now use these reservations too. Legacy full-value compatibility calls and observed allocation/RSS validation remain open before claiming full application-wide compliance.
- Metadata and DDL are verified by a real core/SQLite integration test to preserve active SQLite cursors. Transaction boundaries and explicit release may close them.
- Native SQLite may itself allocate memory for sorting, materialization, or evaluating large values. Release measurements must distinguish driver/application buffering from database engine working memory.

## Verification gates

Exercise backward scrolling after eviction, variable-size pages, two visible results, saturated event queues, zero-byte values with many pages, very wide rows, large deferred values after a new query, export after browsing, cancellation during replay and live fetching, corrupt page files, disk-full appends, and shutdown during all of these operations. Confirm both configured accounting and observed release-build memory.
