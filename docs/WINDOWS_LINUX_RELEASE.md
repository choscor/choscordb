# Windows and Linux releases and updates

The [cross-platform workflow](../.github/workflows/cross-platform-release.yml)
builds and tests native Windows x64 and Linux x86_64 binaries on pull requests
or from a manually selected stable `vX.Y.Z` tag. It packages an **unsigned**
per-user Windows installer and an **unsigned** Linux AppImage. It never creates
or moves a tag, changes release notes, or replaces an existing asset.

All three packages must be built from the same exact source commit before
publication. GitHub Actions artifacts are temporary candidates, not public
downloads. The final Release has exactly the versioned macOS DMG, Windows
installer, and Linux AppImage attached. Checksums, inventories, manifests,
SBOMs, appcasts, and source/license archives stay local; notices stay bundled.
GitHub's generated source-code links are not Release attachments.

## Pages and signing preparation

GitHub Pages hosts the stable macOS appcast and signed Windows/Linux metadata
at `https://OWNER.github.io/REPO/updates/`. The repository's Pages site must
be enabled and its real HTTPS URL verified before production apps embed it.
The production `choscor/choscordb` Pages site was bootstrapped on 2026-09-29
with the signed `v0.1.7` appcast; the public appcast bytes were checked through
the canonical URL after the site built. Recheck with `verify-site` before each
tagged build. The bootstrap steps below are for first setup or a fork.
The feed and package URLs must bind to the same configured `OWNER/REPO`; forks
must configure their own repository and update key. The checked-in public key
pin and workflow setting belong to the production repository; a fork must
replace both with its own public key before building and sign with its own
private key. It must not reuse or request the production private key.
If a site's feed has not yet been bootstrapped, download its existing signed
`v0.1.7` appcast and check that
its enclosure uses the immutable versioned DMG URL, and bootstrap Pages:

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

Bootstrap writes only the appcast to `gh-pages`, configures Pages if needed,
and verifies the old public DMG length and Sparkle signature before accepting
it. The default expected version is `0.1.7`; forks with a different existing
stable version pass `--expected-version X.Y.Z` and their own signed appcast.
Bootstrap leaves older Releases untouched. A local file or Pages settings
screen is not proof that clients can
fetch the feed; record the fetched URL and a cache refresh check. Stop before
packaging if the Pages URL cannot be verified.

Before a tagged Windows/Linux candidate build, the workflow runs
`python scripts/release/update_feeds.py verify-site --repo OWNER/REPO` and
refuses to embed an unavailable Pages URL. Pull-request builds keep the updater
disabled.

The production repository's dedicated Ed25519 Windows/Linux key has already
been created outside the repository, separate from Sparkle. **Do not run key
generation again or overwrite the active PEM.** Verify the existing restricted
key against the checked-in public pin before a release:

```sh
python scripts/release/update_feeds.py inspect-key \
  --private-key ~/.config/choscordb/update-signing.pem
```

This command must report key ID `windows-linux-v1` and the same public base64
used by `update_feeds.py` and the tagged build workflow. If it fails, stop;
do not generate a replacement under the same path. Compare the public key to
the one embedded in both packages before deployment. Keep
the private PEM in restricted maintainer storage and an
encrypted offline backup; only the raw public key is embedded in packages.
The key ID is `windows-linux-v1`. Never put private bytes in Git, Actions
secrets/artifacts, packages, Pages, or Release notes. Restore a backup and
verify it against an existing signed feed before use. If the key is lost or
compromised, existing clients must not silently trust a replacement; a manual
reinstall is the safe fallback unless an authenticated rotation path exists.

For a new fork or a one-time key initialization on a fresh path, generate only
when that path does not exist:

```sh
python scripts/release/update_feeds.py keygen \
  --private-key ~/.config/choscordb/update-signing.pem
python scripts/release/update_feeds.py inspect-key --allow-unpinned \
  --private-key ~/.config/choscordb/update-signing.pem
```

Review and pin the resulting public base64 in `update_feeds.py`, the
tagged workflow's CMake settings, and any corresponding fixtures **before**
building the first updater-enabled package. Then `inspect-key` must pass.
An existing client cannot silently adopt this new key; treat key rotation as
a manual reinstall unless an authenticated rotation path is implemented.

Create the encrypted offline backup once:

```sh
if test -e /secure/offline/update-signing-backup.pem; then
  printf 'Backup already exists; do not overwrite it.\n' >&2
else
  openssl pkey -in ~/.config/choscordb/update-signing.pem -aes-256-cbc \
    -out /secure/offline/update-signing-backup.pem
fi
```

Restore only if the active key is lost:

```sh
if test -e ~/.config/choscordb/update-signing.pem; then
  printf 'Active key exists; do not overwrite it.\n' >&2
else
  openssl pkey -in /secure/offline/update-signing-backup.pem \
    -out ~/.config/choscordb/update-signing.pem
fi
chmod 600 ~/.config/choscordb/update-signing.pem
python scripts/release/update_feeds.py inspect-key \
  --private-key ~/.config/choscordb/update-signing.pem
```

Store the encrypted backup and its prompted passphrase separately. Run the
restore commands only for recovery, then verify a previously signed feed
before deploying. `/secure/offline` is a placeholder outside the repository.

The JSON envelope signs exact payload bytes. It names the stable version,
platform, architecture, immutable versioned asset URL, size, SHA-256, and
release notes. Clients reject bad signatures, unsupported key IDs, malformed
or stale records, unexpected URLs, and altered package bytes before any
installer or replacement starts. Pages outages leave the current app usable;
manual checks report an error and background failures stay quiet.

## First release preparation

1. After pushing the verified exact-commit tag, run the workflow for that tag.
   Inspect the Windows installer, Linux
   AppImage, inventories, checksums, and native test logs. Install each package
   on a clean target system and check launch, SQLite, PostgreSQL, settings
   persistence, and the native credential backend. CTest uses Qt's offscreen
   platform; the packaged smoke tests only launch and exit.
2. Review the bundled Qt, QScintilla, Cargo, icon, and font notices against the
   actual shipped files. The packager refuses missing pinned Qt/QScintilla
   notices and Cargo license texts. The generated inventory supports a separate
   dependency and redistribution review.
3. State in the release notes that the Windows installer is unsigned. Windows
   SmartScreen may warn, and Smart App Control or organization policy may block
   it. The separate update signature does not grant SmartScreen reputation.
   Do not claim that this unsigned installer works on all protected devices.

## Build candidates

After the macOS manifest is verified, create or reuse its exact-commit
annotated tag and push that tag as described in the [macOS
guide](MACOS_RELEASE.md). Then dispatch **Windows and Linux release assets**
with the selected stable tag.
The workflow checks out the peeled tag commit, installs Qt 6.8.3 and
SHA-256-pinned QScintilla 2.14.1, runs native CTest, builds a production updater
without developer tools, and creates:

- `ChoscorDB-X.Y.Z-windows-x64-setup.exe`, a per-user installer with Qt and
  QScintilla DLLs and licenses;
- `ChoscorDB-X.Y.Z-linux-x86_64.AppImage` with Qt and QScintilla runtime files
  and licenses.

The Linux packager verifies pinned SHA-256 hashes before running linuxdeploy
and its Qt plugin. Both packages are smoke tested with isolated application
data. Download the successful workflow run's artifacts into an ignored local
directory, for example:

```sh
gh run download RUN_ID --repo OWNER/REPO \
  --name windows-unsigned-vX.Y.Z --dir build/candidates
gh run download RUN_ID --repo OWNER/REPO \
  --name linux-vX.Y.Z --dir build/candidates
```

Keep each package beside its `.candidate.json` provenance sidecar and
`.sha256` file when downloading it; the publisher requires the sidecars.
Inspect the recorded source commit and require it to match the macOS manifest
and remote tag. The AppImage is built on Ubuntu 24.04;
compatibility with older Linux distributions is not claimed.

On a clean standard Windows account, install version N without elevation,
check the per-user path and profile/credential persistence, then update to N+1
through the UI. The installer waits for the old process to finish its approved
close path, upgrades in place, and relaunches only after success. Test download
cancellation, dirty SQL, pending edits, active database work, canceled close,
and retry. Windows OS code signing is outside this release. SmartScreen may
warn or block an unsigned installer; see [Microsoft's SmartScreen
guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

On Linux, update an AppImage from a writable directory and verify that the
file at its original path has the new bytes and app data survives. Repeat from
a read-only directory and a symlink: the app must offer a manual route without
damaging the original. Exercise failed download, hash, replacement, and
relaunch. Offscreen CTest and package smoke checks do not replace these real
installed-package checks.

## Publish all packages, then advance feeds

After reviewing the macOS manifest and both candidates, recheck that the
already-pushed tag still resolves to the manifest's source commit and prepare
reviewed notes as described in the [macOS guide](MACOS_RELEASE.md). Publish
from one maintainer machine:

```sh
python scripts/release/publish.py \
  --manifest build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json \
  --windows build/candidates/ChoscorDB-X.Y.Z-windows-x64-setup.exe \
  --linux build/candidates/ChoscorDB-X.Y.Z-linux-x86_64.AppImage \
  --notes-file build/release-notes-X.Y.Z.md --dry-run
python scripts/release/publish.py \
  --manifest build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json \
  --windows build/candidates/ChoscorDB-X.Y.Z-windows-x64-setup.exe \
  --linux build/candidates/ChoscorDB-X.Y.Z-linux-x86_64.AppImage \
  --notes-file build/release-notes-X.Y.Z.md
```

The publisher stages exactly three packages on a draft, verifies remote bytes
and tag/commit identity, then publishes the complete stable Release. A partial
draft can be retried with identical bytes; unexpected assets, conflicts, stale
versions, and tag changes fail without overwrite or rollback. The former
post-publication `attach_platforms.py` flow must not be used.

After verifying all three public versioned URLs, sign locally and deploy the
feeds. The private key never enters GitHub Actions:

```sh
python scripts/release/update_feeds.py deploy \
  --manifest build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json \
  --windows build/candidates/ChoscorDB-X.Y.Z-windows-x64-setup.exe \
  --linux build/candidates/ChoscorDB-X.Y.Z-linux-x86_64.AppImage \
  --notes-file build/release-notes-X.Y.Z.md \
  --private-key ~/.config/choscordb/update-signing.pem --dry-run
python scripts/release/update_feeds.py deploy \
  --manifest build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json \
  --windows build/candidates/ChoscorDB-X.Y.Z-windows-x64-setup.exe \
  --linux build/candidates/ChoscorDB-X.Y.Z-linux-x86_64.AppImage \
  --notes-file build/release-notes-X.Y.Z.md \
  --private-key ~/.config/choscordb/update-signing.pem
```

The deployer verifies public package bytes and Release asset count, signs the
Windows/Linux metadata with the pinned key, advances all three Pages feeds in
one `gh-pages` commit, then verifies fetched bytes. A feed must never offer a
missing package. If deployment or cache verification fails, diagnose the
partial state and retry with the same verified inputs. Do not delete a public
Release, move its tag, or silently roll back a feed. Keep the previous runnable
packages and local manifests for recovery; a faulty public release normally
needs a higher version.

## One-time transition for existing installations

`v0.1.7` and older Releases stay intact. Their macOS bundles embed the old
`releases/latest/download/choscordb-appcast.xml` URL. When a three-asset
Release becomes latest, that URL stops resolving because the appcast is no
longer attached. These users must download and install the new versioned DMG
once to enter the Pages feed. The same app identity preserves their data and
credentials.

Existing Windows ZIP and Linux AppImage builds have no updater. Windows ZIP
users install the per-user package once, leaving the portable folder in place
until they choose to remove it. Linux users manually download and launch the
new AppImage once at their chosen path. These installs preserve normal app
data and credentials. State the manual transition in the first three-asset
release notes; do not silently migrate or delete old packages.
