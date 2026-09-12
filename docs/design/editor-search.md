# Editor search and replace

This interaction extends the workspace reference before implementation of PRD §5.3.

Edit → Find reveals a compact search panel between the query toolbar and editor tabs, focused on Find. Replace reveals its replacement row. Use platform standard Find, Replace, Find Next and Find Previous shortcuts. The panel operates only on the current editor; switching tabs invalidates a previous match. Escape within the panel closes it and returns focus to the editor.

The first row contains Find text, Previous, Next, Match case, Whole word, and Close. The second contains Replace with, Replace, and Replace all. A plain-text status reports no match, wrapped search, replacement count, and errors. Search treats entered text literally. Default search ignores case, matches substrings, and wraps at either end. Whole-word matching uses Unicode word boundaries. A nonempty selected string may seed Find when it fits the supported pattern size.

Find searches an immutable snapshot on a worker and selects a complete UTF-8 match only if the active editor, revision and starting selection are unchanged. It does not change the buffer. Replace changes only the last found selection when its editor, revision, range and options still match; otherwise it finds the next occurrence first. Each replacement is undoable. Replace all searches an immutable snapshot on a worker, computes a bounded replacement result, and applies it as one undo operation only when the originating editor and revision are still current. Edits, tab changes, closing the panel, or destroying the target prevent stale application. Only one search or replacement worker may be pending, and the event loop remains responsive.

The shared text service limits documents and replacement output to 16 MiB and search patterns to 16 KiB. Over-limit operations fail visibly before changing the document. Replacement text is literal, and replacement output is not searched again during Replace all. Unicode byte boundaries, empty search, no match, case/word options, growing output limits, stale worker responses, and undo are behavioral acceptance cases.

Recovery and application close disable search mutations with the editor. No search action connects to a database, executes SQL, writes a SQL file, or changes the selected connection.
