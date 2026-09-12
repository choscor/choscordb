# Operating-system credentials

`CredentialStore` is an injectable synchronous contract. The production adapter explicitly selects Keychain on macOS, Credential Manager on Windows, and Secret Service on Linux through [keyring 3.6.3](https://docs.rs/crate/keyring/3.6.3/source/README.md). Unsupported or unavailable backends return fixed errors; no memory/file fallback is selected. Backend calls are serialized and must run outside the UI thread.

Secrets use the shared non-serializable, redacted `Secret` wrapper. Its owned Rust buffer is zeroized on drop. Inputs are limited to 16 KiB; the native backend can impose a smaller limit. Profiles and CXX profile DTOs hold only opaque credential references. A password crosses CXX solely as an explicit operation argument, and the Qt adapter clears its temporary UTF-8 copy after dispatch.

The core metadata worker owns profile and credential operations. Replacements record a reference-only cleanup intent before publishing a fresh OS entry, then commit the new profile reference. Old references are queued transactionally for cleanup. Failed cleanup remains retryable after restart. A canonical metadata sidecar lock serializes this lifecycle across app instances sharing that database. Shared references are not deleted while another profile still uses them.

Normal tests inject stores or use `UnavailableStore`. The native smoke test writes only a fresh random reference in an isolated test namespace, and removes it on completion:

```sh
CARGO_INCREMENTAL=0 cargo test -p choscordb-credentials --test native -- --ignored
```

It requires an accessible OS store and is deliberately explicit. The macOS run passed locally; Windows and Linux native execution remain unverified. Linux requires a running, unlocked Secret Service and D-Bus. Automated mocks and successful compilation do not prove native backend access.
