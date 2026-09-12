# History and disconnected recovery

This extends the established workspace reference before native implementation of PRD §5.7.

## History dock

View → Query history opens a bottom dock using the same spacing, palette, and table headers as Results. At the initial 1280 × 900 workspace, the dock starts around 360 px high, with readable timestamp/profile columns and the SQL excerpt taking remaining width. Users can resize the dock and columns. Its toolbar contains Record history (checked by default), Clear history…, and Refresh. A paged table shows Timestamp, Connection profile, SQL excerpt, Duration, Status, and Rows. Display unknown row counts as an em dash, not zero. Display an unsaved session as “Unsaved connection”; retain a missing saved profile's identifier without connecting to resolve its label.

Select a row to see selectable SQL in a read-only preview. Large previews show bounded segments of at most 65,536 UTF-16 code units without splitting a surrogate pair. Earlier text and Later text browse the entire saved SQL in the read-only preview; opening the entry uses the complete saved SQL. Open in new query (also table activation) creates a modified editor tab with the SQL and optional saved profile association. It never selects an active connection or executes SQL. Previous/Next browse bounded pages. Loading and errors are inline, and stale responses cannot replace a newer selection. SQL and server text always render as plain text.

Clear history requires one confirmation, then removes stored entries and refreshes. Disabling recording affects future records; it does not delete existing entries. Persist policy changes before updating the confirmed checkbox state; on failure restore the prior value and show an inline error. Defaults remain 90 days or 10,000 records. Retention settings will also be available through Preferences.

## Recovery

Persist tab order, stable document ID, SQL buffer, display title, saved profile association, file path, dirty state, cursor and selection. Cursor coordinates use UTF-8 byte offsets and must fall on character boundaries. Save snapshots asynchronously after a short edit debounce and after tab creation, closing, reordering, or cursor changes. Coalesce pending snapshots to prevent queued copies of large documents.

At startup load the saved snapshot asynchronously. Restore every tab disconnected, including file-backed documents from the saved buffer; do not read or execute their files automatically. Recovery does not open database sessions, resolve passwords, or run SQL. Avoid overwriting edits made while recovery loads: complete initial recovery before enabling editor mutation, while keeping the application responsive. An empty snapshot creates one untitled tab.

A nonmodal status message reports recovery failures and keeps the stored snapshot intact until the user deliberately retries or starts a new workspace. Snapshot failures remain visible and must never be described as successful saves. Explicit SQL file Save remains separate from recovery persistence.

On application close, flush the latest snapshot asynchronously before shutting down the engine. Keep the event loop responsive. If persistence fails, offer Retry, Close without recovery, and Cancel; only the explicit Close without recovery choice may discard that snapshot. Closing an individual dirty tab continues to require discard confirmation. Transaction rollback/cancellation confirmation remains part of shutdown, not a consequence of restored profile associations.

## Acceptance evidence

Rust tests must prove restart restoration, atomic replacement, bounds and malformed-state rejection, policy persistence, retention, and disabled/cleared history. Core tests must exercise bounded asynchronous APIs, correlated failures and shutdown. Native tests must restore Unicode text, selection, order, file identity and dirty state with zero sessions and zero executed statements; reopening history must only create an editor. Exercise delayed startup/saves, stale responses, failure retry, and close while a snapshot is pending. Screenshots document the empty/populated history dock and disconnected recovery; they do not substitute for behavior tests.

Normal close waits for connection terminal events and a metadata history flush barrier before global engine shutdown. Abrupt process termination remains outside this orderly-flush guarantee. History-write failures remain visible and never turn successful SQL into a query failure.
