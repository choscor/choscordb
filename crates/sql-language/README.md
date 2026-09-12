# SQL language foundation

Pure Rust editor services, independent of Qt, storage, and database drivers.

- `parse` returns a Tree-sitter syntax tree using the bundled SQL grammar.
- `statement_ranges` and `execution_range` use UTF-8 byte positions, matching Scintilla. A selected range is returned verbatim. Invalid UTF-8 boundaries are rejected. Cursor whitespace selects the following statement, or the preceding statement at end of document.
- `classify` requires confirmation for DROP/TRUNCATE, UPDATE/DELETE without a WHERE at the write's own nesting level, and syntax the grammar cannot verify. Classification is a confirmation gate, not an authorization system. It cannot prove that a WHERE predicate is selective or inspect SQL executed inside server routines. Dialect syntax unsupported by this grammar may cause extra confirmations.
- `quote_identifier`, `qualified_name`, and `template` escape identifiers; templates return text only. INSERT/UPDATE values use numbered parameters compatible with PostgreSQL and SQLite. UPDATE/DELETE predicates deliberately remain incomplete for user review.
- `completions` merges supplied cached schema/table/column entries with keywords and applies a prefix and limit. Metadata retrieval and context-sensitive alias resolution belong to callers/future editor work.

The lexical boundary pass recognizes doubled string/identifier delimiters, PostgreSQL E strings and tagged dollar strings, SQLite backtick/bracket identifiers, line comments, and nested block comments. It does not split inside parentheses. Unsupported or malformed syntax is left for Tree-sitter's conservative confirmation gate.

Validation: `cargo test -p choscordb-sql-language`; `cargo clippy -p choscordb-sql-language --all-targets -- -D warnings`.
