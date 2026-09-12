# Result copying

This reference defines the result-grid copy behavior from PRD §5.5 before implementation.

Control/Command+C copies the selected cells as tab-separated rows, preserving blank holes in a rectangular selection. A context menu on the result grid adds **Copy selected cells**, **Copy selected rows**, and **Copy current page**. Row copying includes every column for each distinct row touched by the selection; page copying includes every visible row and column in the current loaded page. The actions copy data only, without column headers or fabricated total counts.

NULL is copied as the visible `NULL` token while an empty string remains empty. Tabs, quotes and line breaks use the existing quoted-field representation. Binary values already present in the page use their complete hexadecimal representation. A deferred large value cannot be copied from the grid because the grid intentionally retains only its handle; the error directs the user to export the result for the exact complete value. Copying never fetches another page, reruns SQL, loads a deferred value, or changes the selection.

All scopes share the result model's existing byte limit and fail with a plain-text message rather than partially replacing the clipboard. Empty selections disable selected-cell/row actions; page copy is disabled for an empty model. Context-menu construction and triggering revalidate the current model and selection so a page replacement cannot copy stale indexes.

Acceptance covers sparse cells, duplicate selected indexes, multiple/discontiguous rows, whole-page order, NULL versus empty, quoted text, full binary, deferred-value rejection, size rejection, empty state, clipboard replacement only on success, and proof that copying emits no database request.
