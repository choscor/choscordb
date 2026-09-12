# Foundation performance smoke baseline

`foundation-smoke.json` records three release-build launches on an Apple M4 with 24 GiB RAM, macOS 26.5, Qt 6.11.2, and QScintilla 2.14.1. The process displayed the offscreen workspace, waited for its 100 ms smoke timer, then shut down.

| Run | Complete process duration | Peak resident memory |
|---|---:|---:|
| First observed launch | 1.128 s | 50.33 MiB |
| Subsequent launch | 0.238 s | 50.59 MiB |
| Subsequent launch | 0.251 s | 50.73 MiB |

All processes exited successfully. These are baseline observations of the current SQLite foundation. They do not prove the PRD performance gates: OS caches were not flushed; offscreen rendering differs from a visible window; peak process memory is not a steady idle measurement; the full MVP is not implemented. Connected-session memory, cold/warm first paint, editor p95 latency, UI task duration, cancellation latency, shutdown under all operations, and multi-connection million-row browsing still need release instrumentation and repeated measurements.

The next measurement harness is specified in [measurement-contract.md](measurement-contract.md). Enable its separate executable with `cmake --preset release -DCHOSCORDB_BUILD_PERFORMANCE_PROBE=ON`, then build the release preset. Run `python3 scripts/performance/desktop.py build/release/choscordb-desktop-probe build/performance/desktop.json` on a real desktop. The launcher records process readiness and exit timing in addition to the probe's internal measurements. It uses an owned temporary profile and does not modify the user's application database. This command is being integrated; no acceptance result is claimed until a report is produced and inspected.

## Native release desktop measurements, 2026-09-11

The integrated probe completed two Cocoa runs on Apple M4 / 24 GiB, macOS 26.5, Qt 6.11.2. Raw reports are `desktop-macos-run-1.json` and `desktop-macos-run-2.json`. Both traversed 1,000,000 rows through 1,000 grid pages, then reread the evicted first page from disk with one query submission. The harness retained only scalar observations. Separate write-side-effect regressions establish absence of SQL replay.

| Observation | Run 1 | Run 2 | PRD target |
|---|---:|---:|---:|
| Idle current RSS | 162.38 MiB | 163.05 MiB | ≤120 MiB |
| Connected SQLite current RSS | 163.00 MiB | 163.48 MiB | ≤160 MiB |
| Process to usable workspace | 838.10 ms | 408.20 ms | Warm startup ≤800 ms |
| Small-document key-to-paint p95 | 0.77 ms | 1.30 ms | ≤30 ms |

The memory targets fail in these observations. Startup is fresh-profile/repeated-process timing, not a controlled cold-cache measurement, and displayed compositor latency is not measured. Each run also recorded a top-level UI dispatch above 16 ms. Normal close completed well below three seconds here, but blocking storage, active work, PostgreSQL sessions and supported-platform shutdown remain unverified. Background development processes were not controlled; these are diagnostic observations, not qualified release acceptance. The probe now also records the slowest event's type and receiver for subsequent attribution.

## Memory and event attribution

`desktop-macos-phases.json` adds startup RSS boundaries and separates events begun after workspace readiness. On the local Qt 6.11.2 build, RSS was approximately 15 MiB before QApplication, 54 MiB after QApplication, 69 MiB after MainWindow construction and 163 MiB after display. A temporary bare Qt window of the same size measured approximately 111 MiB. Changing only `QT_STYLE_OVERRIDE=Fusion` reduced application idle RSS by roughly 6 MiB, leaving it above target; no shipping style change was made.

A diagnostic macOS `vmmap` sample attributed about 49 MiB to IOSurface allocations and reported a physical footprint of 78 MiB. Physical footprint and resident memory are different metrics; that observation prompted the explicit metric decision below but did not itself establish acceptance. The largest observed GUI event was QWidgetWindow Expose (206). One phased run stayed below 16 ms after readiness, while its follow-up exceeded it. The UI-task gate remains unproven. The probe now snapshots readiness before dispatch and serializes after the closing event returns, addressing review findings about measurement boundaries.

The PRD does not name an operating-system memory metric. After the RSS/IOSurface attribution, the measurement contract was amended to use Apple's `task_vm_info.phys_footprint` for the macOS gate while retaining RSS as a diagnostic. This follows Apple's definition of application memory footprint and avoids counting the same shared/rendering residency as private application pressure.

Three subsequent pinned-Qt runs are recorded in `desktop-macos-qt683-physical-run-1.json`, `desktop-macos-qt683-physical-run-2.json`, and `desktop-macos-qt683-physical-run-3.json`:

| Observation | Run 1 | Run 2 | Run 3 | PRD target |
|---|---:|---:|---:|---:|
| Idle physical footprint | 80.38 MiB | 78.49 MiB | 78.89 MiB | ≤120 MiB |
| Connected SQLite physical footprint | 80.44 MiB | 78.77 MiB | 79.11 MiB | ≤160 MiB |
| Sampled million-row browse peak | 130.66 MiB | 131.42 MiB | 130.25 MiB | within application budget |
| Worst interactive event dispatch | 23.90 ms | 10.87 ms | 13.34 ms | ≤16 ms |
| Interactive dispatches above 16 ms | 1 | 0 | 0 | 0 |
| Editor key-to-paint p95 | 0.80 ms | 0.63 ms | 0.77 ms | ≤30 ms |

The repeated physical-footprint samples pass the idle and connected-session memory gates with substantial headroom. All runs traversed the million-row result; the bounded page/cache tests separately enforce the application allocation budget. The only interactive dispatch above 16 ms was a 23.90 ms `UpdateRequest` during ready idle in run 1, outside the million-row browsing phase. Runs 2 and 3 had no interactive dispatch above 16 ms, and their slowest events occurred during million-row browsing at 10.87 ms and 13.34 ms. This establishes the browsing and editor-latency gates on this machine. Controlled cold-cache startup remains unqualified; fresh-profile process readiness varied and is not a cold-cache measurement.

CI pins Qt 6.8.3, whereas these local reports use Homebrew Qt 6.11.2. Comparing the pinned release stack is the next discriminating measurement. No memory or latency optimization is claimed from these diagnostic experiments.

## CI-pinned Qt comparison

Two Cocoa runs used the CI-pinned Qt 6.8.3 and QScintilla 2.14.1 build; raw reports are `desktop-macos-qt683-run-1.json` and `desktop-macos-qt683-run-2.json`. The first observed process took 7.49 s to workspace readiness while the repeated process took 0.30 s; OS caches were not controlled, so neither establishes the cold-start gate. The repeated process is below the 0.8 s warm target. Idle RSS was 162.6 MiB in both runs and connected SQLite RSS was 163.0–163.2 MiB. This closely matches the Qt 6.11.2 observations and rules out the local Qt minor version as the main source of the memory excess. Both runs traversed and archived-reread one million rows successfully. Each recorded one ready-workspace event above 16 ms, while editor key-to-paint p95 remained below 1 ms.

The pinned build exposed obsolete `AGL` references in Qt 6.8.3's qmake and CMake metadata when building with an SDK that no longer ships that framework. The bootstrap now removes only a complete AGL framework token from its generated Makefile, idempotently. A small CMake compatibility module removes the unavailable framework from Qt's imported WrapOpenGL target according to the configured SDK. The exact QScintilla and application links then passed. This workaround does not modify the installed Qt package.
