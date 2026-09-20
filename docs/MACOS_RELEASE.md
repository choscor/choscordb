# macOS releases

Production supports Apple Silicon and macOS 26.0 or later. Cargo.toml's
`workspace.package.version` is the release version; CMake derives bundle and app
versions from it. Add a matching `## X.Y.Z` heading to CHANGELOG.md and commit all
source changes before packaging. Packaging refuses tracked or untracked changes
and does not change versions, tags, branches, feed history, or remote objects.
When changing the workspace version, run `cargo check --workspace` once to
refresh workspace package versions in Cargo.lock, review that diff, then run the
locked quality checks and commit both files. Release commands use `--locked` and
will not repair a stale lockfile during packaging.

The production identity is `com.choscor.ChoscorDB`, including the credential
service and Application Support directory. Existing development data under the
old identity is left untouched and is not imported. Subsequent releases using
the new identity retain profiles, history, recovery, preferences and credential
references. Keep existing development data separately if it is still needed.

## Tools and dependencies

Use Apple Silicon macOS with Xcode command-line tools and SDK 26.0 or later,
Rustup with the checked-in Rust toolchain, Python 3.12+, CMake and Ninja. Install
`scripts/ci/requirements.txt` and `scripts/release/requirements.txt` in a virtual
environment and activate it. Icon generation uses the pinned CairoSVG Python
converter with native Cairo and Apple iconutil; Cairo is a build tool and is not
bundled. Install the Cairo runtime separately if unavailable. The
commands below never use Homebrew Qt or QScintilla implicitly.

```sh
python scripts/release/macos.py prepare
```

Preparation uses `build/release-dependencies`: Qt 6.8.3 through aqtinstall 3.3.0
with upstream archive checksum verification and only qtbase/qttools/qtsvg archives, QScintilla 2.14.1 built from its
pinned SHA-256 source with deployment target 26.0, and Sparkle 2.9.6 framework and
matching tools from the same pinned archive. Sparkle archive SHA-256 is
`52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192`, checked against
[the upstream release](https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6).
Downloaded Qt binaries are retained beneath `cache/qt` for reuse, with aqt logs
inside the dependency prefix. Verified Qt and QScintilla source archives are
retained for distribution.
`--dependencies PATH` selects another isolated prefix. If the automatically
selected mirror is slow, choose an HTTPS mirror from Qt's official mirror list,
for example `prepare --qt-mirror https://ftp.fau.de/qtproject`; aqt still fetches
SHA-256 checksums from its trusted upstream source independently of that mirror. Prepared payload hashes
are checked before packaging. Rerunning prepare restores Sparkle and rebuilds
QScintilla; discard a corrupted Qt cache and prepare it again.

Universal upstream Qt/Sparkle binaries are thinned to arm64 in the staged app
before signing. Every bundled Mach-O must contain arm64 only and declare a macOS
minimum no later than 26.0. Locally compiled app, Rust native code and QScintilla
share `MACOSX_DEPLOYMENT_TARGET=26.0`. An older minimum in an upstream library is
compatible with the app's 26.0 requirement.

Production and rehearsal builds replace local filesystem prefixes in compiler
diagnostics with stable paths such as `/choscordb`, `/build`, and `/cargo`.
The macOS linker omits debug-map paths from the release executable. Packaging
checks payload bytes for the builder's home and source paths before signing,
and verification repeats that check before publication.

## Signing setup

Keep certificates and private keys in Keychain. The packager discovers the one
available Developer ID Application certificate; use `--identity SHA1` or
`CHOSCORDB_SIGNING_IDENTITY` when several exist. It reuses the reference app's
`agents-notary` profile by default; `--notary-profile` or
`CHOSCORDB_NOTARY_PROFILE` overrides it. It never stores personal certificate
names in source or ordinary progress output.

Create a dedicated Sparkle key explicitly, once:

```sh
build/release-dependencies/sparkle/bin/generate_keys --account com.choscor.ChoscorDB
```

Do not use Sparkle's default `ed25519` account or the Agents app's key. Packaging
only looks up an existing account and fails if missing. Only the public key is
embedded in the bundle and manifest. Back up the private key using Sparkle's
`generate_keys --account com.choscor.ChoscorDB -x /secure/offline/location` outside
the repository, in encrypted offline storage; restrict access. Restore with the
same tool's `-f` option into a secured Keychain. Test a restored key against a
previous release signature before using it. Losing this key can break update
continuity. Certificate renewal must preserve the Developer team and Sparkle
key; automatic key rotation is not provided.

## Package and verify

```sh
python scripts/release/macos.py preflight
python scripts/release/macos.py package --output build/releases/0.1.0
python scripts/release/macos.py verify \
  --manifest build/releases/0.1.0/ChoscorDB-0.1.0-manifest.json
```

An optional `--version X.Y.Z` must match Cargo.toml. Output must be a new directory
beneath ignored `build/`. No skip-signing or skip-notarization production mode is
provided. Preflight checks source/version/changelog; package additionally checks
tools, SDK, dependency hashes, certificate, notary profile and dedicated key
before compiling. Interrupted output retains `INCOMPLETE.json` and useful
sanitized failure/notary logs; use a new output directory for a fresh attempt.
A failed notarization is never treated as accepted. The saved submission ID can
also be inspected with `xcrun notarytool log ID --keychain-profile agents-notary`.

The workflow builds Release without tests, installs and verifies the existing
relocatable stage, signs nested code with hardened runtime, notarizes/staples the
app, creates a drag-to-install DMG with an Applications link, then signs,
notarizes and staples the DMG. It generates notices and an SPDX inventory from
the final signed app, and checks signatures, tickets, Gatekeeper, architecture,
minimum OS, feed metadata and final hashes. Verification mounts the DMG read-only
and compares its app with the inventoried staged app. The separate staged app
and dependency tools must remain available for local re-verification/publishing.
The public manifest contains no local Sparkle tool path. Verification and
publication select tools with `--sparkle-tools PATH`, then the local
`CHOSCORDB_SPARKLE_TOOLS` environment setting, otherwise
`build/release-dependencies/sparkle` beneath this repository. For custom dependency
locations, pass that location's `sparkle` directory to both commands. Packaging
uses its prepared dependency directory directly. Old manifests containing
`sparkle_tools` must be rebuilt; verification refuses them before executing tools.
For example:

```sh
python scripts/release/macos.py verify --manifest build/releases/0.1.0/ChoscorDB-0.1.0-manifest.json --sparkle-tools /local/dependencies/sparkle
python scripts/release/publish.py --manifest build/releases/0.1.0/ChoscorDB-0.1.0-manifest.json --sparkle-tools /local/dependencies/sparkle --dry-run
```


Outputs include `ChoscorDB-X.Y.Z.dmg`, `ChoscorDB.dmg`,
`choscordb-appcast.xml`, version-associated source archives (application, Cargo
vendor sources, Qt and QScintilla), notices/license texts, SPDX inventory,
checksums and the release manifest. The source candidate remains labeled a
working-tree snapshot; the release manifest binds it to the clean checked-in
commit. Software inventory uses a `candidate-X.Y.Z` identifier because that is
the existing source provenance convention, while production bundle/feed/manifest
versions remain X.Y.Z. Release signature metadata contains no private key.

## GitHub Release publication

The repository's [release-new-version skill](../.agents/skills/release-new-version/SKILL.md)
coordinates notes, quality checks, packaging, an exact-source-commit tag push,
and GitHub Release publication. Invoke it with a version and the intended scope
(prepare, publish an existing build, or release end to end). Packaging itself
never tags or publishes.

GitHub Releases is the only upload destination. Use `gh` authenticated locally
with permission to create releases and upload assets, and `curl` for public
verification. Credentials and tool configuration stay outside the repository.
The default release base is `https://github.com/choscor/choscordb/releases`.
Forks select `https://github.com/OWNER/REPO/releases` with `package --base-url`
or `CHOSCORDB_RELEASE_BASE_URL`. The publisher infers the repository from the
verified manifest and rejects a conflicting explicit `--repo`.

Sparkle uses these URLs:

- Feed: `https://github.com/choscor/choscordb/releases/latest/download/choscordb-appcast.xml`
- Latest DMG: `https://github.com/choscor/choscordb/releases/latest/download/ChoscorDB.dmg`
- Versioned DMG: `https://github.com/choscor/choscordb/releases/download/vX.Y.Z/ChoscorDB-X.Y.Z.dmg`

The stable feed is embedded in the signed app; the appcast enclosure references
its immutable versioned DMG. Each release carries a single-release appcast.
Historical assets remain under their original tags. GitHub's latest-release
redirect selects the stable feed, so publishing a prerelease or an older version
must not change the stable channel. See [GitHub's release API](https://docs.github.com/en/rest/releases/releases)
and [Sparkle's publishing guide](https://sparkle-project.org/documentation/publishing/).

After local verification, create an annotated `vX.Y.Z` tag at the manifest's exact
`source_commit` and push only that tag. The publisher requires that remote tag
to exist and resolve to the same commit. Prepare reviewed notes in an ignored
file under `build/`, then run:

```sh
python scripts/release/publish.py --manifest build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json --notes-file build/release-notes-X.Y.Z.md --dry-run
python scripts/release/publish.py --manifest build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json --notes-file build/release-notes-X.Y.Z.md
```

Publication creates or resumes a GitHub draft and uploads exactly three files:
`ChoscorDB-X.Y.Z.dmg` first, identical bytes as `ChoscorDB.dmg` next, and
`choscordb-appcast.xml` last. It verifies uploaded bytes before proceeding and
publishes the draft as latest only after all assets match. Public downloads and
the stable feed are verified afterward. A public-verification failure reports
that publication has already occurred; it cannot atomically undo a GitHub release.

Source archives, license archives, checksums, SBOMs, and manifests stay local;
full local verification still checks them. Retain these materials and bundled
notices for dependency redistribution requirements. No build logs, credential
configuration, or local paths belong in notes or attachments.

Retry with the same manifest, notes, and bytes. Matching assets are reused;
conflicting assets, tags, and releases are not overwritten. A local lock prevents
overlapping commands on this machine; use one maintainer at a time across
machines. Ship a higher version to fix a faulty public release. The workflow
never force-moves tags or deletes previous releases.

### Existing 0.1.0 installations

The original 0.1.0 app embeds the former hosting URL. Changing the source
configuration does not change installed copies. The maintainer requested a
rebuilt 0.1.0 release with the GitHub feed and new icon, replacing that tag and
release. Users who installed the original build must download and reinstall
the replacement manually; Sparkle cannot discover a same-version replacement
through a different feed. Subsequent releases should increase the version.
Legacy manifests using the former host are rejected and must not be relabeled
as new GitHub-configured builds.

## Required rollout evidence

The [local implementation verification record](testing/macos-release-verification.md)
distinguishes completed checks from the remaining real release/rehearsal evidence.

The automated checks do not demonstrate an install on macOS 26.0. Before the
first public release, install and launch the signed DMG on a clean macOS 26.0
machine/VM and record the version, architecture and launch result. This remains
pending when no such environment is available.

Perform the signed A → B update rehearsal in an isolated account/VM using the
explicit rehearsal CMake configuration, separate identity/storage, a separate
Sparkle key account and a nonproduction feed. Populate profiles, history,
preferences, SQL recovery and test-only credentials in A; verify them after B
installs/relaunches. Exercise unsaved edits, active query/transaction, canceling
shutdown and a persistence failure before allowing restart. Serve a modified B
DMG and verify rejection leaves A installed and usable. Verify up-to-date and
network-failure UI. Neither an unsigned fixture nor a mocked signing command
counts as this evidence. Rehearsal outputs must never be passed off as production
manifests. Normal development, smoke and test runs suppress update checks.

Windows/Linux installers and updater backends, Intel/universal distribution,
beta channels and automatic key rotation remain outside this implementation.

## Isolated rehearsal commands

Use a disposable checkout and a dedicated macOS test account or VM. Do not
install the rehearsal over production. In that checkout, set the checked-in
Cargo workspace version to A (for example `0.1.0`), then to a strictly higher B
(for example `0.1.1`) for the second build, with corresponding changelog entries.
The commands below do not create commits or tags. The rehearsal bundle ID,
Application Support directory and credential service are fixed separately as
`com.choscor.ChoscorDB.tests.rehearsal` by CMake. Select an HTTPS feed under your
control; the `.invalid` URL below is a placeholder to replace.

```sh
export CHOSCORDB_REHEARSAL_FEED='https://rehearsal.example.invalid/choscordb-appcast.xml'
export CHOSCORDB_REHEARSAL_OUT="$PWD/build/rehearsal-A"
export CHOSCORDB_REHEARSAL_DEPS="$PWD/build/release-dependencies"
export CHOSCORDB_REHEARSAL_KEY_ACCOUNT='com.choscor.ChoscorDB.tests.rehearsal'
"$CHOSCORDB_REHEARSAL_DEPS/sparkle/bin/generate_keys" \
  --account "$CHOSCORDB_REHEARSAL_KEY_ACCOUNT"
CHOSCORDB_REHEARSAL_PUBLIC_KEY="$("$CHOSCORDB_REHEARSAL_DEPS/sparkle/bin/generate_keys" \
  --account "$CHOSCORDB_REHEARSAL_KEY_ACCOUNT" -p)"
export MACOSX_DEPLOYMENT_TARGET=26.0
cmake -S . -B "$CHOSCORDB_REHEARSAL_OUT/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCHOSCORDB_PRODUCTION_RELEASE=OFF -DCHOSCORDB_UPDATE_REHEARSAL=ON \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DCHOSCORDB_SPARKLE_ROOT="$CHOSCORDB_REHEARSAL_DEPS/sparkle" \
  -DCHOSCORDB_SPARKLE_PUBLIC_KEY="$CHOSCORDB_REHEARSAL_PUBLIC_KEY" \
  -DCHOSCORDB_REHEARSAL_FEED_URL="$CHOSCORDB_REHEARSAL_FEED" \
  -DCMAKE_PREFIX_PATH="$CHOSCORDB_REHEARSAL_DEPS/qt/6.8.3/macos;$CHOSCORDB_REHEARSAL_DEPS/qscintilla"
cmake --build "$CHOSCORDB_REHEARSAL_OUT/build"
python scripts/release/source_archive.py --output "$CHOSCORDB_REHEARSAL_OUT/source"
python scripts/release/stage.py --build "$CHOSCORDB_REHEARSAL_OUT/build" \
  --output "$CHOSCORDB_REHEARSAL_OUT/staged" \
  --qt-bin "$CHOSCORDB_REHEARSAL_DEPS/qt/6.8.3/macos/bin" \
  --qscintilla-prefix "$CHOSCORDB_REHEARSAL_DEPS/qscintilla" \
  --source-candidate-manifest "$CHOSCORDB_REHEARSAL_OUT/source/manifest.json"
```

Set `CHOSCORDB_SIGNING_IDENTITY` to the selected Developer ID certificate SHA-1
and use the following signing step for each rehearsal build. It uses existing
repository signing/notarization functions but never writes a production manifest.
It checks the separate identity, thins the payload, and assesses the result.

```sh
PYTHONPATH=scripts/release python - <<'PY'
import os, pathlib, plistlib
import macos
out = pathlib.Path(os.environ['CHOSCORDB_REHEARSAL_OUT']).resolve()
app = next((out / 'staged').glob('*.app'))
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
assert info['CFBundleIdentifier'] == 'com.choscor.ChoscorDB.tests.rehearsal'
assert info['SUFeedURL'] == os.environ['CHOSCORDB_REHEARSAL_FEED']
identity = os.environ['CHOSCORDB_SIGNING_IDENTITY']
profile = os.environ.get('CHOSCORDB_NOTARY_PROFILE', 'agents-notary')
logs = out / 'logs'
logs.mkdir(exist_ok=True)
macos.LOG_DIRECTORY = logs
macos.strip_development_payload(app)
macos.thin_and_check(app, thin=True)
macos.sign(app, identity)
archive = out / 'rehearsal-app.zip'
macos.run(['ditto', '-c', '-k', '--keepParent', app, archive])
macos.notarize(archive, profile, logs)
macos.run(['xcrun', 'stapler', 'staple', app])
image = out / 'image'
image.mkdir()
macos.run(['ditto', app, image / app.name])
(image / 'Applications').symlink_to('/Applications')
dmg = out / ('ChoscorDB-rehearsal-' + info['CFBundleVersion'] + '.dmg')
macos.run(['hdiutil', 'create', '-volname', 'ChoscorDB Rehearsal', '-srcfolder', image, '-format', 'UDZO', dmg])
macos.run(['codesign', '--sign', identity, '--timestamp', dmg])
macos.notarize(dmg, profile, logs)
macos.run(['xcrun', 'stapler', 'staple', dmg])
macos.assess(app, dmg)
print(dmg)
PY
```

Install A only in the dedicated environment. Change the version to B and repeat
with `CHOSCORDB_REHEARSAL_OUT="$PWD/build/rehearsal-B"`. Generate B's enclosure
signature using its final stapled DMG and the separate account:

```sh
"$CHOSCORDB_REHEARSAL_DEPS/sparkle/bin/sign_update" \
  --account "$CHOSCORDB_REHEARSAL_KEY_ACCOUNT" \
  "$CHOSCORDB_REHEARSAL_OUT/ChoscorDB-rehearsal-0.1.1.dmg"
```

Host that DMG on the controlled HTTPS service and use the returned signature and
length in its feed enclosure, with version/shortVersionString `0.1.1` and
minimumSystemVersion `26.0`. Use the production package's XML structure as a
template, replacing all URLs, versions and signature fields with rehearsal
values. This hosting step is deliberately separate; do not use `publish.py`,
which rejects the rehearsal bundle identity. Record the test outcomes listed
above. For the tamper scenario, change one byte of a copy of B while retaining
its original feed signature; never overwrite the good B artifact. Remove only
the dedicated test installation/data after the rehearsal, not production data.
