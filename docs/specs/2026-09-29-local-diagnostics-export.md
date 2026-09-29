# Local diagnostics and export for ChoscorDB

**Status:** Ready for implementation
**Date:** 2026-09-29
**Source:** Brainstorm with the product owner; repository inspection; the existing Agents app diagnostics; official documentation linked below.

## Outcome

Let a ChoscorDB user save a privacy-safe diagnostic ZIP and attach it to a support conversation. The report should help the maintainer diagnose errors, crashes, UI hangs, performance regressions, and memory growth from real use. Diagnostics stay on the user's machine unless the user deliberately shares the ZIP. Memory trends indicate possible leaks; this release does not claim to locate individual allocations or prove a leak.

## Current state and research

ChoscorDB has Qt Widgets UI and a Rust engine, with macOS, Windows, and Linux packages. `desktop/app/main.cpp` does not initialize an application diagnostics store, and the inspected UI has no diagnostic export. `docs/prd-mvp.md` requires telemetry disabled by default, no MVP usage analytics transmission, and redaction of passwords, tokens, keys, connection strings, and row data. This feature preserves the no-transmission boundary while recording bounded local data.

The related Agents app has a useful reference implementation in `/Users/ngthluu/choscor/macos-app/agents/Sources/AgentsCore/Diagnostics/`: JSON-lines daily logs, 14-day rotation, a run marker for unclean exits, crash handling, a main-thread hang watchdog and short bounded samples, and periodic/event-driven memory and CPU readings. Its README documents the local files. Its source should inform ChoscorDB's design, but the inspected app does not provide a user-facing export flow. It is macOS-specific; ChoscorDB needs portable behavior.

Established app patterns:

| App | Documented pattern | Lesson for ChoscorDB |
| --- | --- | --- |
| [DBeaver](https://dbeaver.com/docs/dbeaver/Log-files/) | Error Log view, capped debug files, Help → Collect Diagnostic info, and a warning that diagnostics may contain sensitive information. | Make collection and file discovery easy; use a stronger data allowlist because ChoscorDB handles SQL and rows. |
| [DataGrip](https://www.jetbrains.com/help/datagrip/troubleshooting-materials.html) | Show logs and enable category-specific debug logging. Its SQL log contains queries; [CPU snapshots](https://www.jetbrains.com/help/datagrip/performance-issues-high-cpu-usage.html), [memory snapshots](https://www.jetbrains.com/help/datagrip/performance-issues-high-memory-consumption.html), and [automatic freeze dumps](https://www.jetbrains.com/help/datagrip/performance-issues-ide-does-not-respond.html) are deeper, separate tools. | Keep routine diagnostics lightweight. Do not copy SQL logging or heap dumps into the default export. |
| [VS Code](https://code.visualstudio.com/docs/configure/telemetry) | Separates crash, error, and usage telemetry; exposes controls and local telemetry inspection. Its [performance issue guide](https://github.com/microsoft/vscode/wiki/performance-issues) asks users to record a targeted CPU profile. | Treat routine metrics and targeted profiling as separate product decisions. |
| [Apple](https://developer.apple.com/documentation/Xcode/acquiring-crash-reports-and-diagnostic-logs) | macOS can provide full system crash reports to users for sharing. | Keep a path for later support-led investigation, but do not automatically add raw OS reports to the strict ZIP. |

## Decisions and scope

1. Record lightweight diagnostics locally by default. There is no automatic upload, telemetry endpoint, account, or background network request. Export is initiated by the user.
2. Support the packaged macOS, Windows, and Linux apps in the first release. Shared behavior and archive format must be portable; platform crash details may vary and must be labeled accurately.
3. The standard report includes structured errors and lifecycle events, crash or unclean-exit evidence, UI hang evidence, sampled memory trends, coarse feature counts, and performance timing buckets. Include app/build version, OS family/version, architecture, and diagnostic schema version. Do not store or export a persistent device identifier.
4. Only explicitly allowlisted typed fields may enter local diagnostics or the ZIP. Never write SQL text, query history, result rows, database names, profile names, hostnames, credentials, connection strings, local paths, environment variables, arbitrary exception text, or raw stack dumps. Use stable error codes or safe error classes where available. A crash signature may contain only reviewed module identifiers and relative offsets or another equally constrained representation. Do not turn an unclean exit into a claimed crash or invented stack trace.
5. Keep 14 days of local history. A standard export covers the last 7 days. Bound total storage, individual artifact size, sampling frequency, and export size; prune oldest data and preserve high-value failure evidence when a cap is reached. Exact byte and sampling limits are implementation choices to document and test.
6. Put an **Export Diagnostics…** action in Help. Explain contents and date range, show the estimated bundle size and safe category summary, then let the user choose a ZIP destination. On success, show its location and tell the user to attach it manually to their chosen support conversation. Provide **Show Diagnostics Folder** and **Clear Local Diagnostics** access in the same flow or a nearby Help action. The user can inspect the generated files before sharing.
7. Do not add user-triggered CPU profiling, allocation or heap snapshots, SQL/JDBC-style traces, automatic uploads, dashboards, support integration, or native raw crash reports to this release. A future opt-in deep-diagnostics mode may address those separately.

## Observable behavior and failures

- Logging and sampling must not block the UI or database work. A diagnostics write failure, full disk, or unsupported platform API must not prevent ChoscorDB from launching or running queries. Record an in-memory or UI-visible warning where feasible, and make export failure explicit.
- Use bounded structured events at meaningful boundaries such as startup/shutdown, connection attempt outcome by driver type, query execution outcome and duration bucket, result paging, export, cancellation, UI hang, and memory sample. Counts and timing buckets are aggregates; no per-query identifier or text is needed. Keep event taxonomy small and versioned.
- Detect unexpected prior termination via a run marker. Capture safe crash details when the platform permits; otherwise report `unclean_exit` with no guessed cause. A deliberate force quit, power loss, and a crash may be indistinguishable in the fallback path. Do not intercept or suppress normal OS crash reporting.
- Detect meaningful UI stalls and record their start/end and duration. If a safe, bounded thread sample is available, it must pass the same field allowlist; otherwise omit it and retain the hang event. Do not include raw process samples in the standard ZIP.
- Memory samples include process footprint or resident memory where supported, peak if supported, and coarse context such as open tab count. Label unavailable fields. Sample at startup, periodic intervals, and useful lifecycle boundaries without continuously profiling allocations.
- ZIP creation must use a consistent snapshot, flush pending records first, avoid symlinks and unrelated files, write to a temporary destination, and replace the chosen destination only after success. Cancellation or failure must not leave a partial ZIP at the requested path. If there is no history, export a manifest that says so.
- The manifest lists schema version, app/build and platform details, UTC coverage, included categories and counts, omitted/unavailable categories, and truncation or dropped-record counts. It must not contain sensitive paths. The UI must not imply a crash stack or leak diagnosis when only a marker or memory trend exists.
- Clear Local Diagnostics deletes only diagnostic data, not profiles, SQL history, recovery, credentials, or preferences. Capture resumes immediately under the default local policy. Existing installs need no database migration.

## Acceptance criteria and public test seams

| Observable acceptance criterion | Preferred test seam |
| --- | --- |
| Launching and using the app produces bounded local typed records, with no diagnostic network traffic. | Launch the packaged or test executable with an isolated application-data directory; exercise public UI/adapter actions and inspect files and network adapter/test proxy. |
| A report contains the agreed event classes, aggregate counts/timing buckets, memory trends, version/platform metadata, and a truthful availability manifest. | Exercise app workflows through `MainWindow`/`EngineAdapter`, export through the Help action, and inspect the ZIP schema and manifest. |
| Sensitive SQL, values, profile and host names, secrets, paths, and arbitrary driver errors never appear in files or the ZIP. | Use distinctive sentinel inputs through real profile, query, result, and error flows; inspect every local diagnostic file and ZIP member. Also test the typed event API rejects freeform or unapproved fields. |
| A clean exit is distinguished from an unclean prior run; a safe crash signature is included only when actually captured. | Spawn an isolated app subprocess for clean exit, forced termination, and a deliberate crash fixture on each supported platform; relaunch and inspect the exported manifest/events. |
| A UI stall and memory growth are visible without noticeable interference with ordinary UI work. | Trigger a controlled main-thread stall and bounded allocation workload via a test harness at the app boundary; inspect hang timing and memory samples, and verify responsive normal actions remain within existing performance gates. |
| Retention is 14 days, standard export is 7 days, and storage/ZIP caps are enforced with explicit truncation metadata. | Use an isolated data directory and injectable time or fixture files; export through the public diagnostic service and inspect dates, counts, limits, and manifest. |
| Save, cancel, disk-full/write failure, no-history, Show Folder, and Clear actions have clear results and do not damage app data. | Drive the Help UI with temporary directories and controlled file failures; inspect saved ZIP, absence of partial output, focus/accessibility names, and existing profile/history stores. |
| The same report contract works on release-like macOS, Windows, and Linux builds. | Platform CI/build smoke plus a focused export/parse test on each OS; assert unavailable native fields are labeled rather than fabricated. |

## Technical constraints and likely affected areas

- Keep the diagnostic event contract independent of Qt and Rust error message strings. Likely touch `desktop/app/main.cpp`, `desktop/app/main_window_ui.cpp`, lifecycle/workspace controllers, and selected Rust bridge/engine operation boundaries. Choose a small cross-language boundary instead of duplicating policy in Qt and Rust.
- Respect `AGENTS.md`: app state and workflow in `desktop/app/` or `desktop/widgets/`; reuse design-system dialog/menu components and semantic styling. Inspect `desktop/design_system/README.md` before adding a component. If design-system code changes, follow its own `AGENTS.md`.
- Run `python3 scripts/ci/ui_consistency.py` (inspect `--json`), `ui_policy.py`, `qss_policy.py`, and relevant native CTest targets. Run the full native suite when dependencies are present. Cover any new component construction site in the consistency census.
- Keep private local files inside the existing application-data area with restrictive permissions where supported. Do not append diagnostics to `choscordb.sqlite` merely to reuse storage; keep retention and deletion independent from user data.
- Validate release artifacts still retain symbol information needed privately by maintainers to interpret safe crash signatures, where platform packaging supports this; do not put private symbols or raw reports in the ZIP.
- For macOS, official crash logs can remain a separate support-led procedure. Windows Error Reporting and Linux core-dump facilities differ by configuration; portable app evidence must not depend on those OS facilities being enabled.

## Risks and deferred implementation choices

- A strict allowlist may reduce detail for rare failures. If a case needs SQL or a heap/profile capture, design a separate explicit, time-limited diagnostic mode later.
- Memory trends are evidence of growth, not proof of a leak. The report must say this clearly.
- Choose documented finite limits for total bytes, per-file bytes, sample cadence, duration buckets, and event taxonomy during implementation. Test cap and truncation behavior. These values must not weaken the 14-day retention, 7-day export, or strict content boundary.
- Determine the safest platform-specific crash capture mechanism during implementation. If a safe stack signature cannot be produced on a platform, ship an honest unclean-exit marker and state that limitation in the manifest and release notes.

## Fresh-session handoff

Read this whole spec, inspect the current workspace and repository instructions, then invoke `$implement` with this spec path. Use the acceptance seams above for TDD and verify the final ZIP and UI on all supported platforms.
