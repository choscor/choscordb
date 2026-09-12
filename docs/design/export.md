# Result export dialog

This design extends the workspace reference before implementation. Open one nonmodal export dialog per workspace, using platform fonts, system colors, native focus indicators, and eight-pixel spacing.

The form contains a format selector (CSV, JSON, JSON Lines, SQL INSERT), a destination field with Browse, and SQL-only fields for dialect, optional schema, and table. Schema and table are separate literal identifier components: a dot in either field is part of that identifier. Browse uses the native save picker. Existing destinations require one confirmation when Export is pressed, after an asynchronous existence check.

Below the form, show selectable plain-text status with processed rows and bytes. Do not invent a total or percentage. Export starts asynchronously; keep the form disabled while running and offer Cancel. Cancellation changes the status to “Cancelling…” until the worker reports a terminal outcome. Close or Escape during export requests cancellation and closes only after terminal cleanup. Query replacement or disconnect cancels and hides the dialog immediately; stale events cannot update a later query's dialog.

Success appears inline with final row and byte counts. Failure appears inline with its diagnostic and a Retry button using the current settings. The existing destination remains unchanged until the worker atomically publishes a completed export. No success message box is shown.

Labels provide keyboard mnemonics and field buddies. SQL controls are hidden for other formats. Export is available only with a result and destination; SQL additionally requires a table. The workspace owns the result lifetime and disables conflicting query actions while this dialog has an active export.

`native-export.png` records the native dialog after exporting all 1,001 original SQLite rows following result browsing. Native regressions separately verify cancellation preserves an existing destination and destination failure leaves the grid usable.
