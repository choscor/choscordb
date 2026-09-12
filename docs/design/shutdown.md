# Shutdown responsiveness

This extends the recovery and disconnect references for PRD §§9, 15 and 16 before implementation of the remaining shutdown work.

Closing a window first resolves existing unfinished-query and transaction confirmation, then saves editor recovery, disconnects sessions, and flushes history asynchronously. Normal shutdown should complete within three seconds, measured after the user accepts any confirmation. Confirmation time is excluded. The event loop must remain responsive throughout database and disk cleanup.

A disconnect request must also interrupt a session busy loading navigator metadata or DDL. These operations must not hold the window open indefinitely. A dropped operation must not be retried automatically. Connection cleanup must retain rollback semantics, including suspended RETURNING writes; an interrupted metadata request cannot be treated as a successful empty response.

Performance validation must use release binaries and report platform, hardware, rendering backend, sample count and measurement boundaries. Passing a synthetic cancellation test proves the interruption contract, not the complete application shutdown target. Disk stalls, history/recovery persistence, active exports and native-driver cleanup require separate evidence. No forced process exit or silent loss of saved work is introduced to meet a timing target.

## Current audit boundaries

The core metadata/DDL interruption seam is covered by controlled pending-driver tests for both per-session disconnect and global shutdown. Native adapter work may continue after dropping a request future, so each driver must additionally terminate its underlying operation.

Other unresolved paths include transaction completion, legacy value loading, result-store disk operations, connection/cursor finalization, backpressured event delivery and metadata-worker flush. SQLite storage has a five-second busy timeout, already longer than the normal three-second shutdown target. A credential-store call or recovery/file operation may also delay flush. These require dedicated measurement and fixes; metadata interruption alone does not establish bounded application shutdown.
