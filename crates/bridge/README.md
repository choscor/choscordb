# CXX transport ownership

Commands are nonblocking submissions. IDs include a slot and generation; zero is valid. Check `Submit.accepted`, never `id != 0`. The bridge contains type conversion and lifecycle forwarding; database policy lives in the core.

`drain_events` returns at most one event. Schema and result-page events carry `has_lease`, `lease_id`, and `reserved_bytes`. These are memory reservations, distinct from query IDs. Dropping a CXX DTO does not release its reservation, because a native model may still own the copied values.

A consumer must:

1. Keep the reservation while the DTO and any conversion temporaries exist.
2. If it retains the data, destroy the DTO before calling `shrink_page_lease` with the retained allocation size. Shrinking keeps a bookkeeping allowance and never permits growth.
3. Drop the retained allocations before calling `release_page_lease`.
4. Release ignored events after their DTOs are destroyed. Stale or duplicate releases return an error; they cannot release a reused slot.

The Qt `EngineAdapter` implements this sequence. A controller calls `retainTransfer` during the direct event callback, and later `releasePageLease` after replacing/clearing its model. The adapter shrinks only after leaving the DTO scope. It also delays a release requested from a nested Qt event loop until the outer DTO is gone. Retention is single-owner; additional views must obtain their own accounted storage rather than share a release handle.

`memory_usage` reports reservations, not process RSS. The core hot cache participates in the same broker. Deferred-value events are not integrated into this protocol yet; full result-pipeline acceptance remains open.
