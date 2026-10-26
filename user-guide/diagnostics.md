# Diagnostics

ChoscorDB records a small set of diagnostic events on your computer.
It does not send these events to a server.
You can save them in a diagnostics bundle and attach the bundle to a support report.

## What ChoscorDB records

A diagnostic record contains only fixed fields:

- The event type, for example startup, connection failure, query failure, export or UI stall.
- The driver type: SQLite, PostgreSQL or MySQL.
- The error class, for example connection, authentication, query or I/O.
- A duration and a duration range.
- The number of open tabs.
- The memory that the ChoscorDB process uses.

ChoscorDB does not record SQL text, history, result rows or error messages from the database.
It does not record database names, connection profile names, hosts, passwords or file paths.

ChoscorDB records a memory sample a maximum of one time each 60 seconds.
If the window does not respond for 2 seconds or more, ChoscorDB records a UI stall.

If ChoscorDB stopped unexpectedly, it records an `unclean_exit` event at the next start.
This event does not tell the cause.
The cause can be a crash, a forced quit, a power failure or a different abrupt stop.

## Make a diagnostics bundle

1. Choose `Help` > `Export Diagnostics…`.
2. Read the summary in the `Export Diagnostics` dialog.
3. In `ZIP destination`, type a file path, or click `Browse…`.
4. Click `Save ZIP…`.
5. Open the ZIP file and examine its contents.
6. Attach the ZIP file to your support report.

The summary shows the estimated size, the time range in UTC and the number of records in each category.
If the file exists, ChoscorDB asks before it replaces it.

The diagnostics bundle contains two files:

- `manifest.json` describes the coverage, the counts, the limits and the data that is not available.
- `events.jsonl` contains the diagnostic records.

## Limits

| Item | Limit |
| --- | --- |
| Days in the diagnostics bundle | The most recent 7 UTC days |
| Days in local storage | 14 UTC days |
| Records for one day | 256 KiB |
| Diagnostics bundle | 2 MiB |
| Records that wait to be written | 1,024 |

When a limit is full, ChoscorDB discards routine records before failure records.
The summary shows the number of discarded records.

## The diagnostics folder

ChoscorDB keeps the records in the `diagnostics` folder of its application data directory.
To open this folder, click `Show Diagnostics Folder` in the `Export Diagnostics` dialog.
For the location of the application data directory, see [Your data](install.md#your-data).

## Clear the diagnostics

1. Choose `Help` > `Export Diagnostics…`.
2. Click `Clear Local Diagnostics`.
3. Click `Yes` to confirm.

This action deletes only the diagnostic records.
Connection profiles, history, recovery data, passwords and preferences stay available.
ChoscorDB starts to record again immediately.

If ChoscorDB cannot write a diagnostic record, it continues to operate.
If the diagnostics bundle cannot be saved, the dialog shows the error.
