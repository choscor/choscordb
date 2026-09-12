# Generate SQL from the navigator

This reference precedes implementation of PRD §5.2's SELECT, INSERT, UPDATE and DELETE generation actions.

A table or view context menu contains Generate SQL → SELECT, INSERT, UPDATE and DELETE. SELECT and DELETE use the selected object's quoted qualified name. INSERT and UPDATE require loaded columns; before expansion, those actions are disabled and the submenu explains “Expand this object to load columns.” Generation does not fetch children. Refresh invalidates the old column list immediately, following the navigator's existing request-generation rules.

Choosing an action opens a new, modified query tab, selects that object's existing connection in the query toolbar, and associates its saved profile when available. It does not execute SQL or open a new connection. The editor receives quoted identifiers from a shared Rust template service. Names containing dots, quotes or Unicode are preserved as identifier components; qualified names are never split naively on dots.

SELECT uses `SELECT * FROM ...;`. INSERT lists loaded columns with positional placeholders. UPDATE lists loaded columns with placeholders and an unfinished predicate. DELETE also contains an unfinished predicate. A leading editor comment for INSERT/UPDATE explains that placeholders must be replaced with values before running; predicates remain visibly incomplete. These are editable starting points, not bound-parameter execution. Empty known column sets allow INSERT DEFAULT VALUES; UPDATE remains disabled without columns.

The shared service rejects malformed qualified names and bounds input/output to 1 MiB and 4,096 columns. Failure shows a plain-text error and leaves existing tabs untouched. Context-menu selections retain a persistent model index; refreshed or disconnected objects are revalidated before generation. Existing Copy qualified name and Show DDL actions remain alongside generation.

Acceptance covers all four templates, exact quoting for dotted/escaped/Unicode names, loaded-column gating, refresh/disconnect invalidation, resource failures, a real SQLite navigator-to-editor flow, profile/connection association, and proof that generation does not execute a statement.

Native reference: [generated SQL draft](native-navigator-sql.png). Connection switching follows the query toolbar's existing policy: if another connection cannot be selected while work is in flight, generation reports that condition before opening a tab. SELECT/DELETE remain independent of loaded columns; INSERT with a known empty column set uses DEFAULT VALUES and does not add a placeholder comment.
