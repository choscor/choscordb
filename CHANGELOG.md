# Changelog

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
