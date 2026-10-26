# Windows and Linux release

This page tells you how to build, check and publish the Windows and Linux packages of ChoscorDB.
It also tells you how to deploy the signed update feeds on GitHub Pages.
For the macOS package, the tag and the GitHub Release, read [macOS release](release-macos.md).
For the rules that come from earlier release failures, read [Release rules and lessons](release-lessons.md).

## Packages

The [cross-platform workflow](../.github/workflows/cross-platform-release.yml) has the name `Windows and Linux release assets`.
It builds and tests native Windows x64 and Linux x86_64 binaries.
It runs on pull requests that change build, source or release files.
It also runs when you start it manually with a stable `vX.Y.Z` tag.

A tagged run makes two candidate packages:

- `ChoscorDB-X.Y.Z-windows-x64-setup.exe` is an unsigned per-user installer with the Qt and QScintilla DLLs and licenses.
- `ChoscorDB-X.Y.Z-linux-x86_64.AppImage` is an unsigned AppImage with the Qt and QScintilla runtime files and licenses.

The workflow never creates or moves a tag.
It does not change release notes or replace a Release asset.
GitHub Actions artifacts are temporary candidates and expire after 14 days.
They are not public downloads.
The AppImage builds on Ubuntu 24.04.
ChoscorDB does not claim support for older Linux distributions.

## Native gates

The workflow runs these jobs for each platform:

1. Asserts that the checkout is the peeled commit of the selected tag.
2. Runs `update_feeds.py verify-site` for a tagged run.
3. On Windows, runs the NSIS installer rehearsal in `scripts/release/test_windows_installer_integration.py`.
4. Installs Qt 6.8.3 and the SHA-256-pinned QScintilla 2.14.1.
5. Builds and runs the native tests with `scripts/ci/desktop.py`.
6. Builds the Release configuration without developer tools.
7. Packages the candidate and runs a package smoke test.

Windows builds with MSVC and `/W4 /WX`.
Linux builds with GCC and `-Werror`.
These two jobs are the GCC and MSVC native gates.
Both jobs must pass for the final release commit.
A passing macOS test suite does not show that Windows or Linux is ready.
A passing diagnostic branch does not replace the native gates of the tagged commit.

The native tests use the Qt `offscreen` platform.
The package smoke test only starts and stops ChoscorDB with isolated application data.
These checks do not replace the installed-package checks in [Clean-machine checks](#clean-machine-checks).

### Focused checks before packaging

Run these checks before the expensive macOS package and before you create the tag:

1. Open a pull request from a preparation branch to run the workflow.
2. Make sure that the Windows installer rehearsal passes.
3. Run `python -m unittest scripts/ci/test_windows_linux_updater_config.py` on a machine with CMake.
4. Fix all failures, then select the clean release commit.

The updater configuration test runs the real CMake validation with the pinned public key and the canonical feed URL.
A pull request run keeps the updater disabled.
Thus only the test above checks the production updater configuration before a tag exists.

## Pages site

GitHub Pages serves the update feeds at `https://OWNER.github.io/REPO/updates/`.
The feed directory contains these files:

- `choscordb-appcast.xml` for macOS.
- `windows-x64.json` for Windows.
- `linux-x86_64.json` for Linux.

A tagged workflow run runs `python scripts/release/update_feeds.py verify-site --repo OWNER/REPO`.
If the Pages URL is not available, the run stops before it builds.
Run the same command before each macOS package.
A local file or the Pages configuration screen does not show that clients can get the feed.

### Bootstrap Pages for a fork

The feed URLs and package URLs must use the same `OWNER/REPO`.
A fork configures its own repository and its own update key.
The pinned public key in `scripts/release/update_feeds.py` and in the workflow belong to the production repository.
A fork must replace both with its own public key before it builds.

1. Enable Pages for the repository.
2. Download the signed `v0.1.7` appcast of the repository.
3. Make sure that its enclosure uses the immutable versioned DMG URL.
4. Run the bootstrap dry run, then the bootstrap.
5. Fetch the public feed URL and do a cache refresh check.
6. If the Pages URL does not serve the expected bytes, stop before packaging.

```sh
mkdir -p build/bootstrap
curl --disable --fail --location --proto '=https' --proto-redir '=https' \
  --output build/bootstrap/choscordb-appcast.xml \
  https://github.com/OWNER/REPO/releases/download/v0.1.7/choscordb-appcast.xml
python scripts/release/update_feeds.py bootstrap --repo OWNER/REPO \
  --appcast build/bootstrap/choscordb-appcast.xml --dry-run
python scripts/release/update_feeds.py bootstrap --repo OWNER/REPO \
  --appcast build/bootstrap/choscordb-appcast.xml
```

`bootstrap` writes only the appcast to the `gh-pages` branch.
It configures Pages if necessary.
It verifies the length and the Sparkle signature of the old public DMG first.
The default expected version is `0.1.7`.
For a different existing stable version, set `--expected-version X.Y.Z`.
`bootstrap` does not change older Releases.

## Update signature key

Windows and Linux updates use a dedicated Ed25519 key with the key ID `windows-linux-v1`.
This key is separate from the Sparkle key.
The production key already exists outside the repository.
Do not generate it again and do not overwrite the active private key file.

Set `CHOSCORDB_UPDATE_KEY` to the path of the private PEM file.
Before each release, verify the key against the pinned public key:

```sh
python scripts/release/update_feeds.py inspect-key --private-key "$CHOSCORDB_UPDATE_KEY"
```

The command must show `windows-linux-v1` and the public key in `update_feeds.py` and the workflow.
If it fails, stop.
Do not generate a replacement key at the same path.
Compare the public key to the key in both packages before deployment.

- Keep the private PEM in restricted maintainer storage.
- Keep an encrypted offline backup.
- Never put the private key in Git, GitHub Actions secrets or artifacts, packages, Pages or release notes.
- Only the raw public key goes into the packages.

If the key is lost or compromised, installed clients must not trust a new key silently.
Users must then reinstall ChoscorDB manually.
No authenticated key rotation is available.

### Create a key for a fork

Generate a key only when the target path does not exist:

```sh
python scripts/release/update_feeds.py keygen --private-key "$CHOSCORDB_UPDATE_KEY"
python scripts/release/update_feeds.py inspect-key --allow-unpinned \
  --private-key "$CHOSCORDB_UPDATE_KEY"
```

1. Review the public base64 key.
2. Pin it in `scripts/release/update_feeds.py`.
3. Pin it in the CMake options of the tagged workflow.
4. Update the test fixtures that contain the key.
5. Run `inspect-key` without `--allow-unpinned` and make sure that it passes.
6. Build the first updater-enabled package only after these steps.

### Back up and restore the key

`openssl pkey` needs OpenSSL 3 on `PATH`.
On macOS, install `openssl@3` and add its `bin` directory to `PATH`.
The feed signature tests also need it.
`/secure/offline` is a placeholder for a location outside the repository.

Make the encrypted backup once:

```sh
if test -e /secure/offline/update-signing-backup.pem; then
  printf 'Backup already exists; do not overwrite it.\n' >&2
else
  openssl pkey -in "$CHOSCORDB_UPDATE_KEY" -aes-256-cbc \
    -out /secure/offline/update-signing-backup.pem
fi
```

Keep the backup and its passphrase in different places.
Restore the backup only if the active key is lost:

```sh
if test -e "$CHOSCORDB_UPDATE_KEY"; then
  printf 'Active key exists; do not overwrite it.\n' >&2
else
  openssl pkey -in /secure/offline/update-signing-backup.pem \
    -out "$CHOSCORDB_UPDATE_KEY"
fi
chmod 600 "$CHOSCORDB_UPDATE_KEY"
python scripts/release/update_feeds.py inspect-key --private-key "$CHOSCORDB_UPDATE_KEY"
```

After a restore, verify the key against a feed that you signed before.

### Feed format

The JSON envelope signs the exact payload bytes.
The payload names the stable version, platform, architecture, versioned asset URL, size, SHA-256 and release notes.
Clients reject a bad signature, an unknown key ID or a malformed or old record.
Clients also reject an unexpected URL and changed package bytes before an installer starts.
If Pages is not available, ChoscorDB continues to operate.
A manual update check shows an error, and a background check stays quiet.

## Build the candidates

1. Verify the macOS manifest.
2. Push the exact-commit tag as described in [macOS release](release-macos.md#tag-the-source-commit).
3. Start the `Windows and Linux release assets` workflow on the default branch with `tag=vX.Y.Z`.
4. Make sure that both the Windows job and the Linux job pass.
5. Download the two candidate artifacts to an ignored local directory.

```sh
gh run download RUN_ID --repo OWNER/REPO \
  --name windows-unsigned-vX.Y.Z --dir build/candidates
gh run download RUN_ID --repo OWNER/REPO \
  --name linux-vX.Y.Z --dir build/candidates
```

Each artifact contains the package and these files:

- `PACKAGE.sha256`, the checksum.
- `PACKAGE.candidate.json`, the candidate receipt.
- An inventory file that ends in `-inventory.json`.

Keep each package next to its `.sha256` file and its `.candidate.json` receipt.
`publish.py` and `update_feeds.py deploy` need them.
The receipt records the source commit, version, name, size and SHA-256 of the package.
The source commit in both receipts must be the same as the macOS manifest `source_commit` and the remote tag.

The Linux packager verifies pinned SHA-256 hashes of `linuxdeploy` and its Qt add-on before it runs them.

## Review the candidates

1. Read the native test logs of both jobs.
2. Examine the installer, the AppImage, the inventories and the checksums.
3. Compare the bundled Qt, QScintilla, Cargo, icon and font notices with the shipped files.
4. Do the clean-machine checks below.

The packager refuses missing pinned Qt or QScintilla notices.
A Cargo package without license files has an `spdx-only` record with its license and repository URL.
The inventory supports a separate review of dependency redistribution terms.

The Windows installer is unsigned.
Windows SmartScreen can show a warning.
Smart App Control or an organization policy can block the installer.
The update signature does not give SmartScreen reputation.
State this in the release notes, and do not claim that the installer works on all protected devices.
For more information, read the [Microsoft SmartScreen guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

### Clean-machine checks

Record each result as a manual attestation, separate from automated evidence.
If you cannot do a check, record it as pending.

On a clean standard Windows account:

1. Install version N without elevation.
2. Make sure that ChoscorDB installs in the per-user path.
3. Start ChoscorDB and test SQLite, PostgreSQL and the native credential store.
4. Make sure that connection profiles, preferences and credentials persist.
5. Update to N+1 through the UI.
6. Make sure that the installer waits for the old process, upgrades in place and starts the new version.
7. Test a canceled download, unsaved SQL, pending edits and active database work.
8. Test a canceled close, then a retry.

On Linux:

1. Start the AppImage on a clean system and test SQLite, PostgreSQL and the credential store.
2. Update an AppImage that is in a writable directory.
3. Make sure that the file at the original path has the new bytes.
4. Make sure that the application data is still there.
5. Start an AppImage from a read-only directory and from a symlink.
6. Make sure that ChoscorDB offers a manual update and does not damage the original file.
7. Test a failed download, a hash mismatch, a failed replacement and a failed restart.

## Publish and deploy the feeds

1. Make sure that the remote tag still points to the manifest `source_commit`.
2. Run `publish.py` as described in [macOS release](release-macos.md#run-the-publisher).
3. Fetch the three public versioned package URLs.
4. Run the `deploy` dry run, then `deploy`.
5. Fetch the three live feeds and make sure that each one shows version `X.Y.Z`.

The private key never goes into GitHub Actions.
Run `deploy` on the maintainer machine:

```sh
python scripts/release/update_feeds.py deploy --manifest "$CHOSCORDB_RELEASE_MANIFEST" \
  --windows "$CHOSCORDB_WINDOWS_CANDIDATE" --linux "$CHOSCORDB_LINUX_CANDIDATE" \
  --notes-file "$CHOSCORDB_RELEASE_NOTES" --private-key "$CHOSCORDB_UPDATE_KEY" --dry-run
python scripts/release/update_feeds.py deploy --manifest "$CHOSCORDB_RELEASE_MANIFEST" \
  --windows "$CHOSCORDB_WINDOWS_CANDIDATE" --linux "$CHOSCORDB_LINUX_CANDIDATE" \
  --notes-file "$CHOSCORDB_RELEASE_NOTES" --private-key "$CHOSCORDB_UPDATE_KEY"
```

`deploy` does these steps:

1. Verifies that the remote tag points to the manifest `source_commit`.
2. Verifies that the stable Release has exactly the three package assets.
3. Verifies the public package bytes, hashes and signatures.
4. Signs the Windows and Linux metadata with the pinned key.
5. Advances all three feeds in one `gh-pages` commit.
6. Fetches the feeds and checks the bytes and the cache behavior.

A feed must never offer a missing package.
If `deploy` fails, the previous feeds stay in place.
Find the cause of the failure, then retry with the same verified inputs.
Do not delete a public Release, move its tag or roll back a feed silently.
Keep the earlier packages and local manifests for recovery.
To fix a faulty public release, publish a higher version.

## Installations without a feed

Earlier Windows ZIP builds and Linux AppImage builds have no updater.

- Windows ZIP users install the per-user package once.
  The portable folder stays until the user removes it.
- Linux users download and start the new AppImage once at the path that they choose.
- These installations keep the normal application data and credentials.
- Do not migrate or delete old packages silently.
