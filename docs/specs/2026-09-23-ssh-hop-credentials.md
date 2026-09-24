# Independent SSH hop credentials

This is the next implementation slice of native connection parity, not a
replacement for the remaining inventory in the parent parity specification.
DBeaver source at `b4ad6c12111b7091d13cb79cad63808b7fdd8c08` assigns authentication
per destination in `SSHUtils.java` and connects jump sessions through forwarding
in `AbstractSessionController.java`. ChoscorDB currently supports only jump
endpoints using OpenSSH configuration and rejects target secrets with jumps.

## Observable requirements

1. Each of up to five ordered jump hosts can use configured OpenSSH credentials,
   an agent, a password, or a private-key file with an optional passphrase.
   The final SSH destination keeps its independent authentication settings.
2. A secret is available only to the process authenticating its own destination.
   A jump server accepting the target password must not gain access when its own
   password is wrong. Never fall back to direct database access.
3. Preserve existing profiles and existing configured jump-host behavior. Give
   credential-bearing hops stable identifiers so reordering cannot swap secrets.
   Reject duplicate identifiers and references to nonexistent credential hops.
4. Save/Test/Connect use the same validation. Store only opaque credential
   references in metadata; keep passwords and passphrases transient or in the OS
   credential store. Preserve Keep/Replace/Clear, explicit empty overrides,
   copy-on-write failure cleanup, deletion cleanup and duplicate isolation.
5. Verify each original SSH hostname and port even when a later process connects
   through a loopback forwarder. Support per-hop known-hosts and agent paths.
   Preserve the original database TLS identity and remote forwarding overrides.
6. The whole connection attempt has one deadline. Cancellation and failure close
   every owned process, broker, listener and stream. The topology must work for
   database cancellation and independent object connections as well as primary SQL.
7. Qt provides ordered editable hop rows and separate masked credential fields.
   Passwords never enter settings JSON, ordinary diagnostics or URL fields.
   Failed validation preserves the user's draft.

## Implementation boundaries

Use independently owned OpenSSH processes with app-owned forwarding between
them. Do not use nested shell ProxyCommand strings or pass one broker environment
to the whole chain. Clear inherited askpass variables before installing the
destination's broker. Disable implicit proxy chaining for explicitly routed
processes. Preserve destination configuration matching and strict host-key
identity when overriding the transport address.

Proposed metadata adds optional stable hop IDs, authentication, identity-file,
agent and known-hosts fields to jump entries, with a profile map of hop IDs to
credential references. Runtime secrets and credential updates use separate maps.
Legacy entries without these fields retain configured authentication.

Inline private keys, explicit host-trust approval, shared tunnels and configurable
local binding remain separate required slices in the parent inventory; completing
this slice does not establish complete SSH parity.

## Acceptance evidence

- Public validation/storage/engine tests cover defaults, stable reordering,
  duplicate IDs, unused credential bypass, cleanup, duplication and atomic failures.
- Real SSH fixtures use distinct jump and target passwords; positive connection
  succeeds, wrong hop password fails even if the jump accepts the target password.
- Real fixtures cover key passphrases, agent/configured compatibility, per-hop
  strict trust on nondefault ports, deadlines and cleanup.
- PostgreSQL/MySQL execute SQL through the chain and preserve TLS hostname checks;
  cancellation and MySQL independent object reads use the same isolation rules.
- Widget tests exercise save/test/connect drafts, ordered row editing and masked
  secrets. Independent requirements/code reviews and the full quality gate pass.
