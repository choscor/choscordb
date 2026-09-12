# Disconnect a navigator session

This reference precedes the navigator disconnect interaction. A connection node offers Disconnect. The action targets that node's existing session, regardless of which session is selected in the query toolbar.

A plain-text confirmation identifies the session and says that disconnect rolls back any uncommitted transaction. If the workspace knows that this session has uncommitted work, the primary action is “Roll back and disconnect.” If it has active query, fetch or export work, the message also says that work will be cancelled. Cancel is the default. The rollback notice is retained for idle sessions because some adapters cannot report transaction state authoritatively.

Cancel makes no connection, query, transaction or editor change. Acceptance revalidates the session after the dialog's event loop, then requests asynchronous disconnect. Duplicate requests for a disconnecting session are ignored. Query controls cannot submit new work to that session while disconnect is pending. Other sessions and all editor buffers remain available. The navigator removes the node only when the engine confirms disconnection; that event also invalidates its completion catalog.

Acceptance covers cancelling and accepting a transaction disconnect, rollback proven by reopening SQLite storage, an unselected session target, active-work cancellation, stale removed nodes, duplicate requests and retained editor buffers. This action does not delete a saved connection profile.

Native reference: [disconnect confirmation](native-disconnect.png). The default button is Cancel. The shared driver contract now requires terminal connection close to abort unfinished cursors and roll back uncommitted work before cursor finalization can commit it. Core and adapter regressions cover suspended writes, in addition to the native confirmation flow.
