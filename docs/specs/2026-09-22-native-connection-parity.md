# Native connection behavior parity

> Historical design record. On 2026-09-24, the connection URL importer,
> initialization, commands, variables, lifecycle settings, and native driver
> properties were removed from the application and Rust profile paths.
> Sections below describe the earlier implementation and are not current usage guidance.


Continuation of `docs/connection-handling-audit.md`, using the same pinned DBeaver
Community source. The user selected: **Keep native Rust drivers and match their
supported connection behaviors**. No JVM, JDBC extensions, or additional database
engines are introduced. Existing unrelated work and profile files must survive.

Acceptance checklist for this implementation:

1. Explicit database endpoint fields preserve literal credentials and names. An
   omitted PostgreSQL database resolves to the explicit username. MySQL allows
   anonymous username and no default database. IPv4, IPv6, DNS, and localhost stay
   explicit TCP endpoints.
2. Absolute Unix endpoints use native socket transports (PostgreSQL directory,
   MySQL socket file). Unsupported SSH/TLS combinations fail before connecting;
   there is no silent fallback to TCP or unverified transport.
3. TLS preserves verify-full as the default. Require guarantees encryption;
   verify-CA validates trust without hostname verification; verify-full validates
   both. PostgreSQL prefer uses native TLS preference/fallback. Unsupported
   native-client modes are rejected rather than ignored.
4. Client PKCS#12 identities and their passwords work through both native drivers.
   Passwords are transient or have their own OS credential reference, are never
   serialized into profile metadata, and obey replace/keep/clear/cleanup rules.
   Save, Test and Connect agree; transient empty passwords override stored values.
5. SSH exposes native timeout/keepalive settings, custom agent and known-hosts
   paths, and at most five ordered jump endpoints. Existing profiles receive safe
   defaults. The original database TLS identity survives forwarding. Secrets must
   never be offered to an unintended jump host; unsupported credential/topology
   combinations fail clearly. Whole-operation and broker deadlines follow settings.
6. Native PostgreSQL/MySQL/SQLite URLs can populate an editable form, including encoded
   credentials, IPv6 and supported TLS properties. Unknown/duplicate/ambiguous
   properties fail safely. Raw URLs/passwords never enter saved profile metadata.
   Explicit empty and absent passwords remain distinct.
7. Qt exposes the supported settings with useful labels, validates incompatible
   combinations before submission, preserves drafts on failure, and can round-trip
   all nonsecret settings. Advanced sections are split into maintainable files.
8. Native property maps round-trip through storage, bridge and Qt, reject unknown
   or incompatible settings, and apply supported native PostgreSQL failover,
   routing, session selection, startup, channel-binding, socket and TLS controls,
   plus MySQL initialization, compression, routing and authentication controls.
   Units and transport limitations were documented in the retired native-properties guide.
9. PostgreSQL passfiles support bounded secure files, escaped fields, wildcard and
   first-match semantics. Password commands run with a selected working directory,
   bounded output and deadline. Test/Connect resolve provider credentials afresh;
   transient overrides, including empty passwords, take precedence. Provider
   secrets are not saved in profile metadata or exposed in diagnostics.
10. SQLite URI imports preserve encoded paths, native read-only and memory modes;
    ordinary filenames stay literal. Unknown/duplicate URI settings are rejected
    consistently at Save and connection time.
11. Public driver/storage/engine/bridge/widget regression tests demonstrate the
   behavior; local TLS/socket/SSH fixtures exercise actual native transports.
   Independent reviews and the full repository quality gate pass after integration.

Native-library limitations must be listed by name in the audit. A supported
configuration must never be silently accepted while its settings are discarded.
A percentage is a progress estimate, not proof of universal compatibility.

## Remaining upstream inventory, 2026-09-23

Independent comparison of the pinned shared connection configuration, bootstrap,
network handlers and PostgreSQL/MySQL pages identified these native application
features. They are not counted as implemented merely because the JDBC backend is
excluded:

- SOCKS4/5 proxy transport, credentials and connection UI (implemented and
  verified at the recorded SOCKS checkpoint).
- SSH per-hop credentials; independent remote forwarding host/port; inline keys;
  explicit host trust approval; shared tunnels and local binding configuration.
- Connection bootstrap SQL for PostgreSQL/SQLite, default catalog/schema,
  connection autocommit/isolation and initialization-error policy. MySQL init/setup
  and PostgreSQL startup GUC options already cover a subset. Shared bootstrap and
  manual-mode state publication are now implemented, with the latter still under
  final review. PostgreSQL mode-bearing BEGIN/START TRANSACTION and AND CHAIN
  remain native-supported gaps; their current explicit rejection is not parity.
- SQL-level keepalive and idle connection closure, beyond TCP/SSH keepalive.
- Pre/post connection/disconnection commands and dynamic field variables.

References in the downloaded checkout: `DBPConnectionBootstrap.java`,
`DBPConnectionConfiguration.java`, `DataSourceMonitorJob.java`, `SSHConstants.java`,
`SSHTunnelImpl.java`, `SocksConstants.java`, and `SocksProxyConfiguratorUI.java`.
Each supported behavior requires public-path regression evidence and integration
review before this checklist can be closed. Native-client/platform restrictions
remain separately documented in the audit.

The independent per-hop credential slice is specified in
`2026-09-23-ssh-hop-credentials.md`; it does not replace this broader inventory.
