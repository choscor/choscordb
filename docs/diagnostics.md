# Local diagnostics

ChoscorDB records small, structured diagnostics on the user's computer by default.
It does not upload them. Choose **Help → Export Diagnostics…** to review a safe
category summary and estimated size, select a ZIP destination, and save the
standard report. Inspect the ZIP before attaching it manually to a support
conversation. The same dialog can show the diagnostics folder and clear local
diagnostics without clearing profiles, SQL history, recovery data, credentials,
or preferences. Recording resumes after a clear.

The folder is `diagnostics` inside ChoscorDB's application-data directory. It
contains date-named JSON Lines files, a small drop-count file, and run markers.
The ZIP contains `manifest.json` and `events.jsonl`. The manifest describes
coverage, counts, limits, omitted categories, unavailable measurements, and
truncation. The standard ZIP covers the most recent seven UTC days; local
records are kept for fourteen UTC days.

| Limit | Value |
| --- | --- |
| Daily JSON Lines file | 256 KiB |
| Fourteen-day record store | At most 3.5 MiB, plus small marker and count files |
| Standard ZIP | 2 MiB |
| Pending record queue | 1,024 records |
| Routine memory samples | At most once per 60 seconds |
| Event-driven memory samples | At most once per 5 seconds |
| UI stall threshold | 2 seconds, checked every 250 ms |

The event contract accepts only typed event names, driver types, error classes,
timing buckets, tab counts, and supported process memory measurements. It never
records SQL text, query history, result rows, database or profile names, hosts,
credentials, connection strings, local paths, environment variables, arbitrary
driver errors, or raw stack dumps. A full queue or file drops routine records
first to preserve failure evidence. Diagnostic I/O failure does not prevent
the app from opening or running queries; the export dialog reports a failed save.

The app records a run marker while it is open. A marker from a terminated process
becomes an `unclean_exit` event on the next launch. It may represent a crash,
force quit, power loss, or another abrupt termination. This release cannot
capture a portable safe crash signature and does not include native crash
reports or thread samples. Memory trends can show growth but cannot prove a leak
or identify an allocation. Hang-end events include bounded durations and timing
buckets. The manifest labels unavailable evidence accordingly.
