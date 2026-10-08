# New connection modal simplification

Status: Ready for implementation
Date: 2026-09-28
Source: User request with a screenshot of the current New connection modal and follow-up decisions in the brainstorm session.

## Outcome

Make the New connection modal easier to scan and use for PostgreSQL, MySQL, and SQLite. Server fields should fit into two clear rows; TLS should behave like the existing SSH switch; and one choice should govern whether all connection secrets are saved. Database authentication is limited to a manually entered password or passwordless connection throughout the UI and backend.

## Current state

- `desktop/widgets/profile_dialog/profile_dialog.cpp` builds a 560 × 440 logical-pixel dialog using `DialogShell` and `DialogSections`. Server fields currently occupy Host/Port and Database/Username rows, with Password below. The body scrolls.
- TLS is a five-mode combo box with root certificate and client identity controls always shown. SSH already uses a switch that reveals its fields.
- `profile_dialog_authentication.cpp` adds a Database authentication selector and password-file/command controls. `crates/driver-api/src/database_auth.rs`, `crates/storage/src/lib.rs`, `crates/core/src/profiles.rs`, and `crates/bridge/src/lib.rs` support these extra methods.
- Separate remember checkboxes currently govern database, SSH, TLS identity, and inline SSH-key secrets. Other per-secret controls exist in hidden proxy and SSH-hop editors. Profile metadata lives in app storage; only selected secrets are written to the OS credential store. The credential store contract and cleanup lifecycle are documented in `crates/credentials/README.md`.
- `tests/desktop/connection_dialog_test.cpp` and Rust database-authentication tests cover the current controls and methods; these tests need replacement or revision.

## Requirements and decisions

1. Increase the modal's initial size enough to show the requested horizontal groups comfortably on a normal desktop display. Target roughly 860 × 600 logical pixels as an implementation starting point, using the shared dialog-size metrics and available-screen bounds. Keep the body scrollable and the header/footer accessible when the window is smaller. Avoid horizontal clipping, including in Light and Dark themes.
2. For PostgreSQL/MySQL, place labeled **Host**, a visibly narrower **Port**, and **Database** on one row. Put labeled **Username** and masked **Password** on the next row. Preserve field object names, label buddies/accessibility, focus order, validation, and existing database-specific optional-field behavior. SQLite keeps its file and read-only controls.
3. Remove the visible Database authentication selector and its password-file, hostname-override, password-command, working-directory, and timeout controls. A blank database password means passwordless authentication. Remove password-file and command handling from the backend profile/DTO/validation/runtime paths, rather than merely hiding the options. Saved profiles should contain no database-authentication method/settings field; the optional password or credential reference is the entire database-authentication contract. SSH authentication modes are separate and remain supported.
4. Add a **Use TLS** switch styled through the shared switch role, like **Connect through SSH tunnel**. Off maps to `disable` and hides TLS mode, root certificate, client identity, and identity-password controls. On reveals those controls and retains the existing supported modes (`verify_full`, `verify_ca`, `require`, and PostgreSQL-only `prefer`). Use `verify_full` when turning TLS on for a new draft; retain a selected mode when toggling off and back on within the same draft. Existing TLS compatibility checks, including Unix-socket and MySQL restrictions, still apply.
5. Show exactly one **Save credentials in OS credential store** checkbox for the connection profile, near the secret fields. It controls every applicable secret together: database password, SSH password/passphrase, inline SSH private key, TLS client-identity password, and any secret-bearing connection options that remain supported. Remove the individual save/remember checkboxes from the UI and their independent persistence choices. New profiles start unchecked; an existing profile with saved secret references opens checked. Nonsecret connection settings continue to be saved in app storage.
6. On **Save profile** or **Save & connect**, a checked box stores every supplied applicable secret in the OS credential store using existing opaque references. A blank optional password does not create a credential. An unchecked box saves only nonsecret settings and clears **all** previously saved secret references for that profile, scheduling/removing their OS entries through the existing cleanup lifecycle. A secret entered with the box unchecked remains available for the current dialog's Test/Connect flow, including Save & connect, but is not persisted. Test connection itself does not change saved credentials. Never put secrets in profile metadata, diagnostics, or status text.
7. Preserve the existing explicit Keep/Replace/Clear behavior under the single checkbox: when checked, an unchanged saved secret stays saved; replacing or explicitly clearing one secret affects that secret; turning the checkbox off clears them all. Credential-store failure must not silently produce a partly saved profile. Keep the editable draft and show a redacted error. Existing async progress and action behavior remain intact.

## Scope and non-goals

This includes the Qt modal, profile bridge, Rust profile/storage/core/driver contracts, credential actions, and affected tests. It does not add new database or SSH authentication methods, change the OS credential provider, or redesign the saved-connections sidebar. There are no users yet, so migration or fallback for profiles using database password-file/command authentication is unnecessary; remove those paths and their tests. Do not remove SSH password/key/agent authentication.

## User-visible flows and failures

- Selecting PostgreSQL or MySQL shows the two grouped server rows. SQLite continues to show its database-file form.
- TLS off submits `disable` and hides TLS details. TLS on reveals settings and submits the chosen supported mode. Invalid combinations produce field-level errors before submission.
- Saving with the credential box checked stores every supplied secret and leaves each applicable saved reference available for later connection. Saving unchecked clears prior references; a password typed in the open dialog can still be used for Test or immediate Connect.
- If the OS store is unavailable while a checked save needs a secret operation, saving fails visibly and leaves the draft intact. If deletion cleanup fails after metadata is committed, retain the existing retryable cleanup/warning behavior.
- Passwordless database connections work with an empty password, without an authentication selector or provider settings.

## Acceptance criteria and public test seams

| Observable outcome | Test seam |
| --- | --- |
| The modal opens larger, server fields occupy the requested rows, Port is narrower, and scrolling preserves access to all controls at a smaller viewport. | Qt widget test through `ProfileDialog` geometry, labels, focus, and layout; visual inspection in Light and Dark. |
| SQLite, PostgreSQL, and MySQL expose only their relevant fields; no Database authentication or per-secret save controls remain. | Qt widget test using the dialog's public construction and child control census. |
| TLS off submits `disable`; on reveals the existing supported modes/settings; invalid mode/socket combinations still fail. | Qt dialog Test/Save path plus bridge/profile validation tests. |
| A blank password connects passwordlessly, while a supplied password reaches Test/Connect without entering profile metadata. Password-file and command methods are rejected/absent throughout the public backend contract. | Bridge/profile API tests and storage serialization tests; driver connection tests with injected credentials. |
| With the one box checked, all supplied applicable secrets are saved as opaque references; unchanged ones are kept, edited ones replaced, and explicitly cleared ones removed. | Core profile save/list/load tests with an injected `CredentialStore`, plus Qt save/test flow tests. |
| With the box unchecked, saving clears all old references and a current draft can still Test or Save & connect using transient secrets. | Core save/cleanup tests and Qt dialog-to-`EngineAdapter` tests. |
| Credential-store failure leaves no partially updated profile and keeps the user's draft; cleanup failure follows the existing retry path without leaking secret text. | Core failure-injection tests and Qt error/refresh behavior tests. |

## Implementation constraints

Follow the repository `CLAUDE.md` and `desktop/design_system/CLAUDE.md` when touching UI or shared metrics. Use design-system presentation, semantic roles, spacing, and switch styling; avoid screen-owned QSS and literal visual values. Check the ownership map in `desktop/design_system/README.md` before extracting any component. If shared design-system metrics or components change, update the Light/Dark preview specimen and matching preview test. Keep object names and accessibility behavior for retained fields. Update the UI consistency census if a new construction pattern needs it.

Run `python3 scripts/ci/ui_consistency.py` (and `--json` for the source census), `python3 scripts/ci/ui_policy.py`, and `python3 scripts/ci/qss_policy.py`. Build and run relevant native CTest targets and the full native suite when dependencies are available. Replace obsolete database-authentication tests with tests for the remaining public contract.

## Risks and assumptions

- The starting size is a target, not a fixed minimum; implementation should fit the field groups and available screen without clipping.
- Hidden proxy and SSH-hop controls should not retain independent save choices or create a second visible credential option. Apply the single persistence policy to any secret path that remains reachable, while avoiding unrelated reintroduction of hidden features.
- No migration is required for unreleased password-file/command profiles, per the user's statement that there are no users yet.

## Fresh-session handoff

Read this whole spec and inspect the current workspace before editing. Then invoke `$implement` with `docs/specs/2026-09-28-new-connection-modal-simplification.md` and implement and verify the complete change.
