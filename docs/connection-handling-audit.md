# Connection handling comparison and hardening

> Historical design record. On 2026-09-24, the connection URL importer,
> initialization, commands, variables, lifecycle settings, and native driver
> properties were removed from the application and Rust profile paths.
> Sections below describe the earlier implementation and are not current usage guidance.


Audit date: 2026-09-22. ChoscorDB starting commit:
`7696ded414d8c4a794b8d51a6f7059ee051f12a4` (working tree already had unrelated
result-view/UI work). DBeaver Community source was downloaded with a shallow
Git clone to `/tmp/choscordb-connection-audit/dbeaver`, pinned at
[`b4ad6c12111b7091d13cb79cad63808b7fdd8c08`](https://github.com/dbeaver/dbeaver/tree/b4ad6c12111b7091d13cb79cad63808b7fdd8c08).
This compares SQLite, PostgreSQL, MySQL, and their connection transports; it does
not claim parity with every DBeaver driver or certification for production.

## Native scope selected for the continuation

The user selected **native Rust drivers matching their supported connection
behaviors**, without a JVM/JDBC compatibility layer. Acceptance criteria are in
`docs/specs/2026-09-22-native-connection-parity.md`. Arbitrary JDBC plugins and
properties are not interpreted as native options.

## Behavior at the audit date

| Variant | Native ChoscorDB behavior |
| --- | --- |
| SQLite file, memory, read-only and URI | Supported, including strict native URI parameters; literal filenames remain literal. Obsolete network credential references do not block testing. |
| PostgreSQL omitted database | Uses the explicit database username, matching the effective upstream default. |
| MySQL anonymous login / no database | Empty username and database accepted; server authentication decides access. |
| TCP hostnames, localhost, IPv4, IPv6 | Supported, including bracketed IPv6 normalization; localhost remains TCP. |
| Unix sockets | Explicit absolute endpoint: PostgreSQL socket directory, MySQL socket file. TLS/SSH combinations rejected; unavailable platforms return a clear error. |
| TLS policy | Verify-full remains default. Require encrypts without certificate validation; verify-CA checks trust without hostname validation. PostgreSQL prefer follows native fallback semantics. Unsupported MySQL prefer is rejected. |
| TLS trust and client identity | CA files/bundles and native PKCS#12 client identities; original database TLS hostname retained through SSH. |
| TLS identity password | Separate transient value or OS credential reference, with keep/replace/clear, atomic cleanup, duplication isolation, and no plaintext metadata. |
| SOCKS proxy | SOCKS5 anonymous or username/password authentication, SOCKS4/4a user ID, proxy-side DNS, separate OS credential reference, and bounded setup/cancellation. Original database TLS identity is retained. Unsupported SSH/Unix/failover combinations fail validation. |
| SSH authentication | Agent, key file, inline key, encrypted key and password for the final destination and each identified jump endpoint; separate database and destination-scoped SSH credentials. |
| SSH advanced options | Bounded connection timeout, keepalive interval/count, custom per-hop agent socket/known-hosts path, up to five ordered jump endpoints, independent remote forwarding host/port overrides, optional local binding and tunnel sharing. |
| SSH jump security | Independently owned OpenSSH processes isolate each destination's credential broker. Stable hop IDs retain secret ownership on reorder; an invalid hop cannot use the final destination's credential. |
| SSH trust | Strict verification uses each original hostname and port, including forwarded hops. Selected first-use approval records a host key; unknown or changed keys fail until explicitly approved. |
| Deadlines and cleanup | Bounded connection setup and credential broker work; selected connection deadline also governs cancellation, including direct PostgreSQL TLS negotiation. Broker/task/socket cleanup tested. |
| Database authentication | Manual or stored passwords, PostgreSQL passfiles with SSH/explicit-host lookup and database-host fallback, and bounded command-generated passwords. Provider secrets are resolved for each connection and excluded from profile metadata. |
| Qt connection form | Socket/default guidance, TLS identity/password, SSH controls, proxy settings and authentication providers. Save/Test/Connect share configuration and preserve failed drafts. |

## Upstream source evidence

The following paths are relative to the pinned DBeaver checkout:

- `plugins/org.jkiss.dbeaver.ext.mysql/src/org/jkiss/dbeaver/ext/mysql/MySQLDataSourceProvider.java`:
  `getConnectionURL` appends the database only when nonempty (lines 140–150).
- `plugins/org.jkiss.dbeaver.ext.mysql.ui/src/org/jkiss/dbeaver/ext/mysql/ui/views/MySQLConnectionPage.java`:
  connection completeness checks host and port, rather than requiring a database.
- `plugins/org.jkiss.dbeaver.ext.postgresql.ui/src/org/jkiss/dbeaver/ext/postgresql/ui/PostgreConnectionPage.java`:
  initializes new profiles with `postgres`; supports its own default-database behavior.
- `plugins/org.jkiss.dbeaver.net.ssh/src/org/jkiss/dbeaver/model/net/ssh/config/SSHAuthConfiguration.java`:
  distinguishes agent, password, and key authentication records.
- `plugins/org.jkiss.dbeaver.net.ssh/src/org/jkiss/dbeaver/model/net/ssh/SSHConstants.java`
  and `SSHTunnelImpl.java`: configurable timeouts, alive interval, forwarding,
  and jump-server configuration.
- `plugins/org.jkiss.dbeaver.net.ssh.sshj/src/org/jkiss/dbeaver/model/net/ssh/SSHJSessionController.java`:
  authentication-specific handling, timeout/keepalive setup, and host verification.
  Permissive host-verification branches are not copied.
- `plugins/org.jkiss.dbeaver.ext.postgresql.ui/src/org/jkiss/dbeaver/ext/postgresql/ui/PostgreSSLConfigurator.java`:
  additional TLS modes and client certificate/key configuration.

These are behavioral references. The Rust implementation retains its native
client libraries and OpenSSH transport rather than copying JDBC or Java code.

## Explicit boundaries

This is not full compatibility with every DBeaver plugin or configuration. The
native implementation rejects settings it cannot honor. In particular:

- No JVM, replacement JDBC JARs, Java socket factories, or arbitrary JDBC
  properties. URL import uses a tested allowlist; unsupported URL options are
  rejected rather than silently discarded.
- PostgreSQL `allow` and MySQL `preferred` TLS fallback are not provided by the
  selected native clients. Use an exposed policy with its stated verification.
- Native MySQL identity loading uses PKCS#12. PEM private-key, Java keystore,
  inline-key/certificate editors and client-certificate generation are not
  exposed. PKCS#12 conversion can be done externally.
- SSH per-hop credentials, inline private keys, explicit local bind endpoints,
  opt-in tunnel sharing and selected first-use host approval are implemented on
  the supported Unix transport. Inline private keys and tunnel sharing fail
  closed on Windows pending platform-specific ownership verification.
- Kerberos/cloud IAM/interactive MFA and additional database engines remain
  outside the currently exposed native connection configuration.
- SOCKS cannot currently be combined with SSH, Unix sockets or native multi-host
  selection/direct socket properties. It does not bypass certificate verification.
- Passfile authentication with PostgreSQL failover hosts is rejected because the
  native connection configuration accepts only one password. MySQL pool-only
  settings are rejected because the application uses direct connections.
- Command authentication terminates Unix process groups on cancellation/timeout;
  Windows descendant-process cleanup is not verified.
- SSH stderr is still discarded; some host-trust/authentication/forwarding errors
  remain generic.

These boundaries are not counted as implemented parity or verified production
behavior. Supported native configurations have positive and contrasting tests;
all-platform, keychain and packaged-application checks require separate evidence.

Credentials are not rewritten when an endpoint is edited. Saved credential
references continue to belong to the profile. Both Test and Connect resolve only
credentials used by the selected driver/authentication method.

## Verification

Behavior tests use public driver, profile storage, engine, and Qt dialog seams.
Regression failures were observed before fixes for stalled PostgreSQL startup/TLS,
stalled askpass clients, stale unused credential references, optional MySQL
database, invalid endpoint syntax, and bracketed IPv6 TCP connections.

Integration uses disposable local MySQL and PostgreSQL fixtures, plus the existing
isolated OpenSSH fixture. Tests must never point to a production database.
`docs/testing/mysql.md` and `docs/testing/postgres.md` describe setup. Offscreen Qt
checks cover widgets and submission behavior; they do not establish window-manager,
keychain, packaged-app, or other-operating-system behavior.

## Implementation and review evidence

The initial hardening pass passed the full quality gate (355 Rust tests and all
39 CTest suites), plus local PostgreSQL/MySQL/SSH fixtures. Its baseline and logs
remain in `/tmp/choscordb-connection-audit/`.

The continuation baseline, source snapshots, and red/green logs are retained in
`/tmp/choscordb-connection-parity/`. Changes cover driver API endpoint/URL/TLS/SSH
contracts, both network drivers, profile storage and credentials, CXX/Qt adapters,
the split connection dialog, authentication providers, native properties, SQLite
URI import, regression tests and disposable TLS/SSH fixtures.

The 2026-09-23 intermediate integrated run passed 432 Rust tests and 37 of 39
CTest suites. The workspace suite caught two newly added regressions (escaped
property limits and the passfile hostname UI); the source-policy suite caught
four presentation metrics. These failures are being fixed, so this is not a final
green quality result. PostgreSQL's 23 existing live cases also passed after the
direct cancellation deadline fix.

Independent requirements and code reviews approved the completed backend behavior
with the named fixes, but identified SOCKS as a remaining native transport gap.
SOCKS protocol, driver, credential and UI tests now pass, including live PostgreSQL
and MySQL TLS, hostname rejection and cancellation through the proxy. Final
integration checks remain pending. Unrelated result-view changes in the shared working tree
are excluded from the connection implementation and reviews.

The SOCKS and SSH forwarding override slices have independent requirements and
code approvals with no open material finding. Their whole-project checkpoint is
running in `/tmp/choscordb-connection-parity/quality-socks-forwarding.log`. The SSH
override has a real MySQL jump-host success/failure regression; PostgreSQL uses
the same validated forwarding helper for connection and cancellation, but a new
override-specific PostgreSQL TLS fixture has not been run.

The frozen SOCKS/forwarding checkpoint passed the canonical full quality command
on 2026-09-23: **460 Rust tests passed**, 81 fixture-dependent tests skipped, and
**all 39 CTest suites passed**, alongside formatting, lint, type checks, Clippy,
dependency policy and the release native build. Live fixtures above supply the
separate network evidence. This green checkpoint precedes the remaining startup
initialization and lifecycle work; it is not a claim of complete DBeaver parity.

The startup and cleanup checkpoint also passed the canonical full quality gate:
**485 Rust tests passed**, 88 fixture-dependent cases skipped, and **all 39 CTest
suites passed** (`quality-bootstrap-final.log`). Both independent review axes
approved the final socket cleanup repairs. A separate MySQL fixture passed 20
live tests, including four core-engine tests. PostgreSQL's final live bootstrap
and driver suites passed as recorded in `bootstrap-cleanup-live-final.log`.

The subsequent manual-startup implementation applies the profile's transaction
mode to initialization SQL and publishes actual pending state before UI readiness.
Real SQLite, PostgreSQL and MySQL fixtures cover visibility and explicit
Commit/Rollback; Test Connection rolls back pending initialization. Independent
object sessions preserve their own pending initialization until close. MySQL now
refreshes transaction state after recoverable server errors before reusing the
connection, preventing the next manual query from implicitly committing existing
work. Live tests distinguish an active transaction after an ordinary SQL error
from an inactive transaction after deadlock rollback. The final MySQL fixture
passed 23 cases; independent review reran all 12 cleanup/recovery cases, including
stalled recovery, query deadlines and owner cancellation. Code review approved
the ordinary manual-control slice. Richer PostgreSQL controls and per-hop SSH
credentials are subsequent work; the next full gate is still outstanding.

Per-hop SSH credentials now have stable hop identifiers and distinct OS credential
references across metadata, core, bridge and the structured Qt editor. Removing a
hop cleans its credential; reordering retains ownership by ID, and Test/Connect
use a transient override only for its matching hop. Public storage/core/bridge
regressions and 32 focused Qt tests pass. The final isolated SSH fixture exited
successfully with PostgreSQL and MySQL through one, two and five hops; it covers
independent passwords, encrypted keys, agent and configured authentication,
host certificates and alias resolution, VerifyFull database TLS and hostname
failure, query cancellation with session reuse, and MySQL object reads. Its two
adversarial cases showed that an intermediate server accepting the next hop's
password still rejects a wrong password for its own hop. A stalled OpenSSH
preflight obeys the selected deadline and is killed with descendants; aborting
an auxiliary attempt closes its relay promptly. Credential-map and editor reviews
approved their scopes. Independent native transport review also approved the
final helper and relay lifecycle.

The subsequent inline-private-key slice stores target and per-hop key material
only in the credential store, with separate opaque references from passphrases.
The Rust SSH transport materializes owner-only temporary identity files and
removes them after success, failure, timeout or cancellation. Windows inline
key use fails closed until an owner-only ACL implementation is verified. Real
PostgreSQL and MySQL SSH fixtures passed encrypted/plain target and hop keys,
mixed authentication, TLS identity, cancellation and cleanup; public core tests
cover saved refs, stable-ID reorder, removal, deletion, transient Test/Connect
overrides, rollback after a late vault failure, and metadata secrecy. The Qt
editor exposes File/Inline with masked key entry and separate key/passphrase
actions; its latest whole-workspace run passed 72 tests with five fixture skips.
Independent UI and Rust code reviews found no open material issue. The canonical
full quality gate after this integration is still running.

At that checkpoint, remaining native parity work included connection commands and variable
expansion, app-wide idle activity and ping recovery policy, interactive SSH host
trust, and tunnel sharing/local binding where compatible with native drivers.
Those are not complete, and no percentage is treated as a release claim.

The next bounded slices have moved beyond that checkpoint. Four connection
command phases now run with bounded child cleanup and literal argument vectors;
public review regressions cover exited-parent descendants, full event queues,
Test cleanup, and shell-builtin avoidance. Global Qt input now resets idle
closure across sessions, including the final input within the old throttle
window. Explicit SSH host-key inspection and selected fingerprint approval
passed live PostgreSQL/MySQL fixtures; unknown and changed keys still fail
strict verification. The Qt editor requires explicit selection/path approval
and never reconnects automatically. Focused code and requirements reviews
approved these bounded slices; the canonical full quality gate after their
integration remains outstanding.

Connection templates now cover SQLite path and PostgreSQL/MySQL host, port,
database, user and native properties. The saved profile retains raw expressions;
Test and Connect resolve one attempt, revalidate it, and keep startup SQL/hook
expansion opt-in so existing dollar syntax remains literal. Supported variables
are the nonsecret host, port, database, user, datasource and local compact
date/time values. Focused
storage/core/bridge and Qt tests pass. Other DBeaver
variables and handler fields remain outside this bounded implementation.
Optional local SSH binding and authenticated tunnel sharing are implemented in
native transport and passed disposable driver fixtures. The final independent
requirements and code reviews approved process-group cleanup, stable cache
identity including implicit certificate sidecars, and the Qt binding/sharing
controls. The Qt workspace suite passed 80 tests with seven fixture skips.
Opt-in idle write-transaction rollback uses conservative native transaction
snapshots. The core actor's normal, delayed-input, active cursor and
stalled-rollback tests, storage round trip, real SQLite Qt workflow, SQLite
native cases, nine live MySQL cases and PostgreSQL live transaction controls
pass. Independent code and requirements reviews approved the bounded policy.
The canonical full gate passed on 2026-09-23 with **602 Rust tests passed**,
102 fixture-dependent skips, and **all 41 CTest suites passed**, including the
release-mode native build (`quality-idle-final.log`). Subsequent connection
recovery work now probes idle sessions and replaces failed connections under one
deadline; public tests cover credential refresh, failed replacement and no SQL
replay. The broader DBeaver variable inventory remains a configuration gap.

The final parity and independent-hop integration ran the canonical full quality
gate on 2026-09-23: **621 Rust tests passed**, 104 fixture-dependent cases were
ignored, and **all 41 CTest suites passed**, including the release-mode native
build. The complete disposable SSH fixture passed for both PostgreSQL and MySQL,
including five hops, separate credentials, strict trust, TLS hostname checks,
sharing, local binding, cancellation, and adversarial wrong-hop passwords. Its
server disables OpenSSH source penalties so intentional authentication failures
cannot lock out later positive fixture cases.
