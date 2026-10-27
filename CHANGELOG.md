# Changelog

## Unreleased

- Added MariaDB 10.11+ connections with their own profile choice and icon, including sequences, system-versioned tables with Current, As of and All history views, JSON values, and ed25519 accounts. A MySQL connection to a MariaDB server (or the reverse) still connects and shows a notice suggesting the matching connection type.
- Scripts in MariaDB connections now ask for confirmation before running `/*M! ... */` executable comments.
- Saving the first MariaDB connection upgrades the profile store, after which earlier ChoscorDB versions cannot open it.
- Made remote PostgreSQL and MySQL connections faster with fewer network round trips for result pages, object data and metadata.
- Large result pages now prepare off the UI thread, so the window stays responsive while they load.
- The navigator now loads objects on a separate database session, so expanding a node does not wait for a running query.
- The navigator now shows the saved objects of a connection profile at once, and then replaces them with current objects.
- SSH tunnels of one connection now share one SSH login on macOS and Linux, and send a keepalive every 30 seconds.

## [0.1.10]

- Refreshed the interface with an orange brand palette, compact 4px sizing, flat sidebars, segmented pane tabs, and consistent hover and selection highlights.
- Set all interface text in the bundled Geist and Geist Mono fonts, so every platform renders the same typeface at a larger, more readable scale.
- Improved responsiveness with large navigator trees, pins, quick search, and result pages with large selections.
- Show long text cells as a single-line preview in result grids while keeping the full value for editing and copying.
- Aligned dialogs, menus, and status messages on one consistent design.
- Fixed a failed sidebar or quick-search history request being mistaken for a failed workspace save or shutdown flush.
- Fixed document tab titles eliding and confirmation dialogs scrolling when their text fits.
- Fixed a separator line appearing under the macOS title bar.
- Fixed SSH host key approval on Windows rejecting every known-hosts file path.

## [0.1.9]

- Added a side sheet for editing result cells.
- Added quick filters for Object Data cells.
- Unified workspace status lines with paging, execution information, and full status details.
- Made notification feedback easier to read and keep available.
- Improved sidebar scrolling and section spacing.
- Corrected PostgreSQL shutdown errors being reported as query errors instead of disconnected connections.
- Fixed repeated cancellation of PostgreSQL object reads interrupting an existing transaction or SQL cursor.
- Fixed Preferences failing to load during startup or staying unable to save after appearance settings load.

## [0.1.8]

- Added signed update checks and installation for Windows and Linux, with a per-user Windows installer.
- Added syntax-colored JSON views for cells, rows, and result pages.
- Improved viewing, copying, and exporting binary and unfamiliar database values, including PostgreSQL text fallbacks.
- Added expandable sidebar pins and more compact schema object columns.
- Improved relationship diagram navigation and layout for dense schemas.
- Show workspace recovery failures in window notifications.
- Fixed recent-object navigation when quick search reopens immediately after closing tabs.
- Fixed navigation becoming available too early or staying blocked when object-data tabs have overlapping activity.
- Fixed Windows installer staging-directory replacement and failure dialogs during silent installation and update rollback.

- Added local diagnostics with a Help action to review and export a privacy-safe ZIP manually.
  Reports include bounded typed events, UI stalls, memory trends, and unclean-exit evidence.
  They do not include SQL, result data, raw crash reports, or stack traces. A stale run
  marker indicates an unclean exit; it cannot distinguish a crash from a force quit or
  power loss. Native crash signatures are unavailable in this release.

## [0.1.7]

- Added quick search for connections, database objects, and open workspaces with Cmd/Ctrl+P.
- Added sidebar pins for database and schema objects, and support for showing multiple saved connections.
- Added table and view rename and drop actions from the navigator, and hide PostgreSQL system schemas by default.
- Simplified new connection setup and TLS options, with clearer connection timeout handling.
- Added SQL target status in the result footer and refined navigation and workspace controls.
- Prepared native Windows and Linux release packages alongside the macOS release workflow.

## [0.1.6]

- Added a JSON row inspector for query results and one-hop relationship diagrams
  for database objects.
- Added typed result cells and foreign-key navigation for supported databases.
- Refined desktop components, object tab titles, and macOS title bar spacing.
- Improved the update action when an update is ready to install.

## [0.1.5]

- Simplified result filtering to a single SQL condition input.
- Improved recent query history with clearer SQL previews, connection details,
  status, and timestamps.
- Refined connection dialogs, workspace navigation, and feedback across Light
  and Dark themes.
- Improved the object DDL viewer with line numbers and updated desktop styling.

## [0.1.4]

- Expanded connection profiles with TLS settings, SOCKS proxies, SSH jump hosts,
  inline private keys, and host-key trust prompts.
- Improved database authentication and connection timeout handling for MySQL and
  PostgreSQL, including SSH credential storage options.
- Added transaction controls and idle transaction handling across database
  workspaces.
- Added typed SQL result filters and refined result-table editing, headers, and
  workspace navigation.

## [0.1.3]

- Refined dialogs, menus, selects, and tooltips so they stay contained within
  their workspace, with stronger nested-modal focus and keyboard handling.
- Improved result-table presentation and editor completion behavior across the
  desktop interface.
- Updated the application mark to a white paw print on the ChoscorDB orange
  gradient.

## [0.1.2]

- Added full-result filtering and type-aware sorting across SQL Results and
  Object Data, integrated with paging, copying, and export.
- Added staged row duplication for editable results, preserving the existing
  review-before-apply workflow.
- Added explicit SSH agent, public-key, and password authentication, including
  optional secure storage for SSH passwords and passphrases.
- Added passwordless database authentication support and strengthened
  cross-platform test reliability.

## [0.1.1]

- Added MySQL connections with TLS verification and optional SSH tunneling.
- Added MySQL browsing, query execution, transactions, editable results, multiple
  result sets, stored procedures, and bounded disk-backed result paging.
- Refreshed the desktop workspace, navigation, previews, icons, and native macOS
  title bar behavior.
- Updated build and quality tooling, plus GitHub release-draft handling.

## [0.1.0]

- SQLite and PostgreSQL workspaces with saved connection profiles, query history,
  editor preferences, and recovery of unsaved SQL buffers.
- Apple Silicon macOS packaging and authenticated, user-approved updates.
- Downloads and the Sparkle update feed hosted on GitHub Releases.
- A white Lucide database-zap app icon on a green background.
- New application identity `com.choscor.ChoscorDB`. Existing development data and
  credentials are left untouched; this release starts with a fresh profile.
