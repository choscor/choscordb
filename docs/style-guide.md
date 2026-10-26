# Documentation style guide

ChoscorDB documentation uses ASD-STE100 Simplified Technical English (STE).
STE makes text short, clear and easy to translate.
This page gives the STE rules that the project applies and the project term list.

The full STE dictionary is licensed by ASD.
No tool in this repository checks words against that dictionary.
`scripts/ci/docs_policy.py` checks sentence length and links only.

## Rules

### Sentences

- Write procedural sentences (steps) with 20 words or fewer.
- Write descriptive sentences with 25 words or fewer.
- Use the active voice.
- Use the imperative mood for instructions.
- Write one instruction in each sentence.
- Put a condition before its instruction, for example "If the test fails, read the log."

### Words

- Use one word for one meaning.
- Use the terms in the term list.
  Do not use a synonym for a listed term.
- Do not use words that end in "-ing" as nouns.
  Write "to filter the list", not "filtering of the list".
- Use simple verb tenses: present, past and future.
- Keep articles ("a", "an", "the") where English needs them.

### Paragraphs and lists

- Give each paragraph one topic.
- Write six sentences or fewer in a paragraph.
- Use a numbered list for a procedure.
  Put one step in each list item.
- Use a bulleted list for items that have no order.
- Put commands, file names, code and UI values in inline code or in a code block.

### Exceptions

A sentence can be longer when it quotes a legal text, a standard name or a UI text.
Put `<!-- docs-ok: <reason> -->` on that line or the line above.
The reason is necessary.

## Term list

| Term | Meaning | Do not use |
| --- | --- | --- |
| ChoscorDB | The application. | the app, the tool |
| connection profile | The saved settings that open a database connection. | connection, profile, data source |
| connection | An open link to one database server or file. The UI label `New SQLite session…` is the only use of "session". | link |
| driver | The engine adapter for one database type (SQLite, PostgreSQL, MySQL). | plugin, backend |
| navigator | The tree of connection profiles and database objects at the side of the window. | explorer, sidebar |
| database object | A schema, table, view, column, index or other object in the navigator. | entity, item |
| workspace | The main window area that shows SQL editors and object pages. | canvas |
| SQL editor | The text editor where you write and run SQL. | query window |
| query | One SQL statement or a batch of statements that you run. | command, script |
| result grid | The table that shows the rows of a query or of a table. | data grid, results table |
| result page | One block of rows in the result grid. | chunk |
| history | The saved list of the queries that you ran. | log |
| recovery | The restore of unsaved SQL editor text after ChoscorDB stops unexpectedly. | autosave |
| preferences | The settings dialog of ChoscorDB. | settings, options |
| quick search | The dialog that finds database objects by name. | finder |
| diagnostics bundle | The local file that collects logs and system data for a support report. | crash report |
| export | Write result rows to a file. | download, dump |
| Rust engine | The Rust code that owns behavior, drivers and stored data. | backend |
| desktop frontend | The Qt Widgets code that shows the UI. | client, GUI layer |
| quality gate | One check that `scripts/ci/quality.py` runs. | lint step |
