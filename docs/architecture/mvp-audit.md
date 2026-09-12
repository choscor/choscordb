# MVP acceptance audit

This file tracks the evidence required to claim the active functional MVP scope complete. On 2026-09-12 the user explicitly deferred all distribution work, including platform packages, signing/notarization, notices/SBOM and release artifacts; the original `docs/prd-mvp.md` remains unchanged as the product document. “Implemented” means current source and focused tests cover the behavior. “Open” means evidence is missing, contradicted, or narrower than the active functional requirement.

| PRD §19 acceptance criterion | Status | Current evidence or remaining proof |
|---|---|---|
| Installs and launches on Windows, macOS and Linux | Deferred by scope override | Distribution and installation evidence will be handled in a later iteration. |
| PostgreSQL and SQLite connections work | Implemented | Driver contracts, native SQLite flows and owned live PostgreSQL TLS/value/cancellation/transaction tests pass, including network loss and fixture restart. |
| Passwords never appear in local database or logs | Implemented | Versioned credential references, the macOS credential adapter, redaction and database/log scanning tests pass. Other-platform credential execution is outside the current scope. |
| Navigator loads objects lazily | Implemented | Native model/controller tests cover expansion-only requests, subtree refresh, stale generations and connection isolation. |
| Editor executes selection or current statement | Implemented | Native editor/workspace tests cover lexical statement selection and explicit selection. |
| Active queries can be cancelled | Implemented | Core and native SQLite cancellation, live PostgreSQL native cancellation and race tests pass. |
| Result grid respects configured memory limits | Implemented and measured | Shared reservation, transfer, per-result/global cache and eviction tests pass. Three macOS Release runs report 78.49–80.38 MiB idle physical footprint and 78.77–80.44 MiB connected, below the §15 targets. |
| One-million-row result can be browsed progressively | Implemented on native SQLite | Three Release runs traverse 1,000 pages, verify contiguous values, sample a 130.25–131.42 MiB browse peak and reread evicted page zero without replaying SQL. |
| CSV and JSON exports stream and cancel | Implemented | CSV, JSON, JSON Lines and SQL INSERT paths use cursor-to-temporary-file streaming, atomic rename, cancellation and cleanup tests. |
| Explicit commit and rollback | Implemented | Core/driver/native transaction tests include manual state, confirmation and rollback on disconnect. |
| Workspace restores without reconnecting or executing SQL | Implemented | Recovery tests cover inert tabs, corrupt state fallback, request counters and close persistence. |
| Critical automated tests pass | Implemented locally | The complete Rust workspace, 22 native suites, 19 live PostgreSQL cases, two disconnect cases and an exclusive server-restart case pass. Published CI/distribution evidence is deferred. |
| Release contains licenses, notices, checksums and build instructions | Deferred by scope override | Existing release foundations are preserved, but distribution completion is outside the current functional iteration. |

## Performance and reliability gates

Native Release measurements and limitations are recorded under `docs/performance`. Three pinned-Qt runs pass the macOS physical-footprint gates; all three million-row browsing phases stay below 16 ms, and editor key-to-paint p95 is 0.63–0.80 ms. Normal shutdown is below three seconds. A fresh-profile process reached readiness in 1.425 seconds in the latest run, within the cold target, but this is not a controlled cold OS-cache measurement.

Reliability tests cover query timeout/recovery, cancellation races, corrupt local state/cache, disk-full-style export failures, invalid Unicode, large binary/text values and active-query shutdown. Live PostgreSQL evidence now covers server-side connection termination, sticky terminal behavior, a fresh session after loss, blocked implicit-command shutdown, and an actual owned-fixture stop/restart cycle.

## Deferred distribution gates

The deterministic source-candidate and unsigned macOS staging foundations are preserved for later work. Packages, notices/SBOM, checksums, signing and installation smoke tests are all deferred by the 2026-09-12 scope revision and are not part of current functional completion.
