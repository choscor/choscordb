# Preferences

This reference precedes implementation of configurable editor fonts and shortcuts in PRD §5.3 and settings persistence in §12.

Edit → Preferences opens a nonmodal dialog. The Editor page offers a font family selector, size (8–48 pt), and a read-only SQL sample. System monospace is the default; an empty stored family means the platform default. Applying updates existing and newly opened editors, their lexer styles and line-number font, without changing SQL or undo history.

The Keyboard page lists application commands with their current bindings. Use a key-sequence editor to change a binding; an empty override disables that command's shortcut. Reset to defaults removes overrides and uses platform standard keys. Store sequences in portable Qt notation, independent of translated command labels. Detect duplicate or prefix-conflicting effective bindings before saving, including defaults; show the affected commands inline. A command remains available from its menu when its shortcut is disabled.

Apply validates and saves asynchronously before changing live settings. Cancel closes without applying the draft. Successful Apply keeps the dialog open. Failed loads/saves show plain-text errors and preserve the draft; startup settings failures keep usable defaults and allow retry. Controls cannot submit overlapping saves. Closing the app includes previously accepted settings operations in its metadata flush ordering.

The initial command catalog covers New query, Open SQL file, Save SQL file, Undo, Redo, Cut, Copy, Paste, Find, Replace, Find next/previous, Run statement, Cancel query, Commit, and Rollback. Stable command IDs are shared with persistence. Font family is bounded to 256 UTF-8 bytes; portable sequences to 128 bytes each; at most one override per known command is accepted. Application settings are versioned and contain no credentials or connection strings.

Acceptance: defaults, validation, atomic save, restart persistence, failure draft retention, font application to old/new editors without text mutation, custom/disabled/reset shortcuts, duplicate/prefix conflict handling, and delayed/stale settings responses. Additional result/cache/timeout and history-retention controls remain separate preference integrations against their existing core contracts.
