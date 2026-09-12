# Query settings

This reference precedes the native page-size and timeout controls required by PRD §§5.4–5.5.

Query → Query settings opens a nonmodal dialog with Rows per page (100–10,000, default 1,000) and Statement timeout in seconds (0 means no timeout; maximum 86,400). Apply validates and persists asynchronously before changing the defaults; Cancel closes without applying the draft. Failed saves preserve the draft, and corrupt stored settings keep usable defaults with an explicit retry/reset path. Only one save may be pending.

An explanatory line states: “Applies to new queries. Existing results keep their page size and timeout.” Each accepted execution captures its settings. Previous/Next pages and archived rereads use that result's captured size, even after defaults change. A page may contain fewer rows when it hits the existing byte budget or end of result. Changing defaults never reruns SQL, repaginates archived results, reconnects, or changes an in-flight deadline.

Settings use a separate versioned shared QueryPreferences contract containing page size and timeout seconds. Page-size limits/default come from the driver API's validated PageSize type. Native execution, fetch, storage and bridge layers must not carry independent hard-coded defaults. Timeout zero maps to the existing no-timeout contract; other values become a per-query deadline.

Acceptance covers default/missing settings, validation and corrupt data, restart persistence, failed-save draft retention, all page sizes from 100 through 10,000, captured size on forward/backward paging after a settings change, future-query adoption, timeout cancellation and recovery, and proof that applying settings does not execute SQL.
