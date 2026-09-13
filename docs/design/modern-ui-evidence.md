# Modern UI verification evidence

This historical report predates the approved [shadcn reference](shadcn-reference.md).
Its performance observations remain baseline evidence; the appearance contract is
now recorded in [gallery evidence](shadcn-gallery-evidence.md). The native component gallery is
built as `choscordb-component-gallery` only when `BUILD_TESTING` is enabled and is not installed.
It presents normal, selected, disabled, loading, empty, success, warning, and error vocabulary.

Deterministic local reference captures can be generated without changing saved preferences:

```sh
capture_profile="$(mktemp -d)"
CFFIXED_USER_HOME="$capture_profile" QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb \
  --screenshot build/ui-evidence/workspace-light.png \
  --screenshot-theme light
CFFIXED_USER_HOME="$capture_profile" QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb \
  --screenshot build/ui-evidence/workspace-dark.png \
  --screenshot-theme dark
CFFIXED_USER_HOME="$capture_profile" QT_QPA_PLATFORM=offscreen build/dev/choscordb.app/Contents/MacOS/choscordb \
  --screenshot build/ui-evidence/workspace-narrow.png \
  --screenshot-theme light --screenshot-size 960x640
```

The capture directory is intentionally ignored: screenshots vary with platform font and native
shell rendering. Structural Qt tests and semantic contrast assertions are the cross-platform gate;
native review remains required before release on Windows, macOS, and Linux in System/Light/Dark and
available forced-contrast modes.

## Release performance comparison

On 2026-09-12 the release desktop probe completed three real Cocoa runs on Apple M4 / 24 GiB,
macOS 26.5, Qt 6.8.3, after the modern UI implementation. Every run traversed one million rows,
reread the archived first page, and submitted the query once.

| Observation | Run 1 | Run 2 | Run 3 | PRD target |
|---|---:|---:|---:|---:|
| Process to ready | 1,277.59 ms | 267.80 ms | 259.88 ms | cold ≤1,500 ms; warm ≤800 ms |
| Idle physical footprint | 42.94 MiB | 43.02 MiB | 43.41 MiB | ≤120 MiB |
| Connected SQLite footprint | 43.52 MiB | 43.59 MiB | 43.97 MiB | ≤160 MiB |
| Editor key-to-paint p95 | 0.44 ms | 0.42 ms | 0.45 ms | ≤30 ms |
| Worst interactive dispatch | 25.26 ms | 18.12 ms | 18.21 ms | ≤16 ms |
| Normal close | 57.95 ms | 59.58 ms | 59.29 ms | ≤3,000 ms |

Memory, repeated-process readiness, editor latency, bounded million-row traversal, and normal close
remain within their applicable targets and compare favorably with the latest pinned-Qt baseline in
[`../performance/README.md`](../performance/README.md). Process readiness is fresh-profile rather
than controlled cold-cache evidence. Each run recorded one over-budget layout event during the
million-row phase, so the 16 ms event-dispatch gate remains unqualified rather than being reported
as passing. Raw local reports are generated under the ignored `build/performance/` directory with
the repository's documented performance launcher.
