# macOS release

This page tells you how to build, sign, verify and publish the macOS package of ChoscorDB.
It also gives the GitHub Release and Pages feed procedure for all three platforms.
For the Windows and Linux packages, read [Windows and Linux release](release-windows-linux.md).
For the rules that come from earlier release failures, read [Release rules and lessons](release-lessons.md).
The [release-new-version skill](../.claude/skills/release-new-version/SKILL.md) uses these pages as its procedure.

## Release scope

A release has exactly three public package assets:

- `ChoscorDB-X.Y.Z.dmg` for macOS on Apple Silicon.
- `ChoscorDB-X.Y.Z-windows-x64-setup.exe` for Windows x64.
- `ChoscorDB-X.Y.Z-linux-x86_64.AppImage` for Linux x86_64.

All three packages come from one exact source commit.
The verified macOS manifest, the two candidate receipts and the remote `vX.Y.Z` tag must all name that commit.
GitHub Releases hosts the three packages.
GitHub Pages hosts the update feeds.
GitHub Release publication and Pages feed deployment are two separate steps.

The macOS package supports Apple Silicon and macOS 26.0 or later.
Intel and universal macOS packages are not supported.
The production bundle identifier is `com.choscor.ChoscorDB`.
The credential service and the Application Support directory use the same identifier.

## Version and changelog

`workspace.package.version` in `Cargo.toml` is the release version.
CMake derives the bundle version and the app version from it.
Packaging does not change versions, tags, branches, feed history or remote objects.

1. Set `workspace.package.version` in `Cargo.toml` to the stable `X.Y.Z` version.
2. Run `cargo check --workspace` once to refresh `Cargo.lock`.
3. Review the `Cargo.lock` diff.
4. Add a `## [X.Y.Z]` heading to `CHANGELOG.md`.
5. Run `python scripts/ci/quality.py full`.
6. Commit the version, the lockfile and the changelog.

The release commands use `--locked`.
They do not repair a stale lockfile.
`preflight` refuses tracked or untracked changes, a version mismatch and a missing changelog heading.

## Tools and dependencies

Use an Apple Silicon Mac with these tools:

- Xcode command-line tools and SDK 26.0 or later.
- Rustup with the checked-in Rust toolchain.
- Python 3.12 or later, CMake and Ninja.
- The native Cairo library.
  The icon step uses CairoSVG and Apple `iconutil`.
  Cairo is a build tool and does not ship in ChoscorDB.

Install the Python requirements in a virtual environment and activate it:

```sh
python -m pip install -r scripts/ci/requirements.txt -r scripts/release/requirements.txt
```

Then prepare the pinned dependencies:

```sh
python scripts/release/macos.py prepare
```

`prepare` writes to `build/release-dependencies`.
Use `--dependencies PATH` to select a different isolated prefix.
The release commands never use Homebrew Qt or QScintilla.
`prepare` installs these pinned items:

- Qt 6.8.3 through aqtinstall 3.3.0, with only the `qtbase`, `qttools` and `qtsvg` archives.
- QScintilla 2.14.1, built from its pinned SHA-256 source with deployment target 26.0.
- Sparkle 2.9.6 framework and tools from one archive with this SHA-256:

```text
52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192
```

aqtinstall checks each Qt archive against the upstream checksum.
The prepared Qt binaries stay in `cache/qt` under the dependency prefix.
The verified Qt and QScintilla source archives stay there for distribution.
If the selected Qt mirror is slow, use an HTTPS mirror from the official Qt mirror list.
For example, run `prepare --qt-mirror https://ftp.fau.de/qtproject`.
aqtinstall still gets the checksums from its trusted upstream source.

If a Qt cache is corrupt, delete it and run `prepare` again.
A new `prepare` run restores Sparkle and builds QScintilla again.
`package` checks the hashes of the prepared payload before it builds.

## Architecture and path privacy

The packager thins the universal Qt and Sparkle binaries to `arm64` in the staged app before it signs them.
Each bundled Mach-O file must contain `arm64` only.
Each one must declare a minimum macOS version of 26.0 or earlier.
The app, the Rust native code and QScintilla build with `MACOSX_DEPLOYMENT_TARGET=26.0`.

Production and rehearsal builds replace local path prefixes in compiler output with stable paths.
Examples are `/choscordb`, `/build` and `/cargo`.
The macOS linker omits debug-map paths from the release executable.
`package` scans the payload for the home and source paths of the builder before it signs.
`verify` does the scan again before publication.

## Signature keys

Keep certificates and private keys in Keychain.
The packager finds the one Developer ID Application certificate.
If there are more than one, set `--identity SHA1` or `CHOSCORDB_SIGNING_IDENTITY`.
The default notary profile is `agents-notary`.
Set `--notary-profile` or `CHOSCORDB_NOTARY_PROFILE` to use a different profile.
The packager does not write certificate names to source or to normal progress output.

ChoscorDB uses a dedicated Sparkle key account, `com.choscor.ChoscorDB`.
Create it once:

```sh
build/release-dependencies/sparkle/bin/generate_keys --account com.choscor.ChoscorDB
```

- Do not use the default Sparkle `ed25519` account.
- Do not use a key from a different application.
- Export a backup with `generate_keys --account com.choscor.ChoscorDB -x PATH`.
- Keep the backup in encrypted offline storage outside the repository.
- Restore a backup with the `-f` option of the same tool into a secure Keychain.
- Test a restored key against the signature of an earlier release before you use it.

Packaging only looks up the existing account.
If the account is missing, packaging stops.
Only the public key goes into the bundle and the manifest.
If you lose the private key, installed copies of ChoscorDB cannot verify new updates.
When you renew a certificate, keep the same Developer team and the same Sparkle key.
No automatic key rotation is available.

## Package and verify

1. Run `python scripts/release/macos.py preflight`.
2. Run `python scripts/release/macos.py package --output build/releases/X.Y.Z`.
3. Set `CHOSCORDB_RELEASE_MANIFEST` to `build/releases/X.Y.Z/ChoscorDB-X.Y.Z-manifest.json`.
4. Run `python scripts/release/macos.py verify --manifest "$CHOSCORDB_RELEASE_MANIFEST"`.

The `--version X.Y.Z` option is optional.
If you set it, it must match `Cargo.toml`.
The output must be a new directory under `build/`, which Git ignores.
No production mode skips the code signature or notarization.
`preflight` checks the source, the version and the changelog.
`package` also checks the tools, the SDK, the dependency hashes, the certificate, the notary profile and the Sparkle key before it compiles.

`package` does these steps:

1. Builds the Release configuration without tests.
2. Installs and verifies the relocatable stage.
3. Signs nested code with the hardened runtime.
4. Notarizes and staples the app.
5. Makes a DMG with an `Applications` link.
6. Signs, notarizes and staples the DMG.
7. Writes the notices and an SPDX inventory from the final signed app.

`verify` checks signatures, notarization tickets, Gatekeeper, architecture, minimum OS, feed metadata and final hashes.
It mounts the DMG read-only and compares its app with the inventoried staged app.
Keep the staged app and the dependency tools for local verification and publication.

If a package run stops, the output directory keeps `INCOMPLETE.json` and sanitized logs.
Use a new output directory for the next attempt.
A failed notarization is never accepted.
To read a notarization result, run `xcrun notarytool log ID --keychain-profile agents-notary`.

### Sparkle tools for verification

The public manifest contains no local Sparkle tool path.
`verify` and `publish.py` select the Sparkle tools in this order:

1. The `--sparkle-tools PATH` option.
2. The `CHOSCORDB_SPARKLE_TOOLS` environment variable.
3. `build/release-dependencies/sparkle` in the repository.

If you use a custom dependency prefix, give its `sparkle` directory to both commands.
`verify` refuses an old manifest that contains `sparkle_tools`.
Build that release again.

### Package outputs

The output directory contains these files:

- `ChoscorDB-X.Y.Z.dmg` and a local `choscordb-appcast.xml`.
- Source archives for the application, the vendored Cargo sources, Qt and QScintilla.
- Notices, license texts and the SPDX inventory.
- `SHA256SUMS` and the release manifest.

The source archive includes these directories:

```text
.github cmake crates desktop docs scripts tests user-guide
```

The source candidate has the label of a working-tree snapshot.
The release manifest binds it to the clean committed source.
The software inventory uses the `candidate-X.Y.Z` identifier.
The bundle, feed and manifest use `X.Y.Z`.
The release signature metadata contains no private key.

### Licenses and notices

The bundle contains the license and source files for the Geist font, the Lucide icons and the database logos.
`LICENSE-DEVICON` covers the MySQL logo from devicon under the MIT license.
It is installed next to `LICENSE-LUCIDE`.
The icons ship as SVG files only.

`scripts/release/cargo_licenses.py` collects the license files of each Rust dependency.
If a Cargo package has no license file, the index records `origin: "spdx-only"`.
That record keeps the SPDX license expression and the repository URL, but no license text.
The notices file shows the repository URL for each `spdx-only` package.

## Publish on GitHub

Do these steps after `verify` passes and the Windows and Linux candidates pass review.
Use `gh` with local authentication and `curl` for public checks.
Keep credentials and tool configuration outside the repository.

### Release URLs

The default release base is `https://github.com/choscor/choscordb/releases`.
A fork sets `https://github.com/OWNER/REPO/releases` with `package --base-url` or `CHOSCORDB_RELEASE_BASE_URL`.
`publish.py` gets the repository from the verified manifest.
It refuses a different `--repo` value.

Sparkle uses these production URLs:

- Feed: `https://choscor.github.io/choscordb/updates/choscordb-appcast.xml`
- DMG: `https://github.com/choscor/choscordb/releases/download/vX.Y.Z/ChoscorDB-X.Y.Z.dmg`

The signed app contains the feed URL.
The appcast enclosure refers to the immutable versioned DMG.
The Pages site must serve the feed before you build a package that contains its URL.
For the Pages bootstrap, read [Windows and Linux release](release-windows-linux.md#pages-site).
See also the [GitHub releases API](https://docs.github.com/en/rest/releases/releases) and the [Sparkle publication guide](https://sparkle-project.org/documentation/publishing/).

### Tag the source commit

1. Read the full `source_commit` from the verified manifest.
2. Set `CHOSCORDB_RELEASE_COMMIT` to that commit.
3. Set `CHOSCORDB_RELEASE_TAG` to `vX.Y.Z`.
4. Set `CHOSCORDB_RELEASE_REMOTE` to the configured Git remote.
5. Look for local and remote `vX.Y.Z` tags immediately before you create one.
6. If no tag exists, create and push only that tag.
7. Verify that the peeled remote tag is the same as `source_commit`.

```sh
git tag -a "$CHOSCORDB_RELEASE_TAG" "$CHOSCORDB_RELEASE_COMMIT" -m "ChoscorDB $CHOSCORDB_RELEASE_TAG"
git push "$CHOSCORDB_RELEASE_REMOTE" "refs/tags/$CHOSCORDB_RELEASE_TAG:refs/tags/$CHOSCORDB_RELEASE_TAG"
```

If an existing tag points to the same commit, use it again.
If an existing tag points to a different commit, stop.
For a replacement of an unpublished tag, read [Release rules and lessons](release-lessons.md#tags-and-assets).
Packaging does not create tags.
`publish.py` never creates a tag.

### Release notes

Write the release notes in an ignored file under `build/`.
Use the changelog at the manifest `source_commit` as the source.
The [release-new-version skill](../.claude/skills/release-new-version/SKILL.md) gives the layout of the notes.
`publish.py` does not check the text of the notes.

- Start the notes with `# X.Y.Z` and `Released: YYYY-MM-DD`.
- State that the Windows installer is unsigned and that Windows can warn about it or block it.
- State that users of 0.1.7 or older must install the new DMG manually once.
- Do not link to local files or to unpublished downloads.
- Do not put logs, credentials or local paths in the notes.

### Run the publisher

Set `CHOSCORDB_WINDOWS_CANDIDATE`, `CHOSCORDB_LINUX_CANDIDATE` and `CHOSCORDB_RELEASE_NOTES` to the local files.
Run the dry run first.
The dry run reads the remote state and validates the release, but it does not publish.

```sh
python scripts/release/publish.py --manifest "$CHOSCORDB_RELEASE_MANIFEST" \
  --windows "$CHOSCORDB_WINDOWS_CANDIDATE" --linux "$CHOSCORDB_LINUX_CANDIDATE" \
  --notes-file "$CHOSCORDB_RELEASE_NOTES" --dry-run
python scripts/release/publish.py --manifest "$CHOSCORDB_RELEASE_MANIFEST" \
  --windows "$CHOSCORDB_WINDOWS_CANDIDATE" --linux "$CHOSCORDB_LINUX_CANDIDATE" \
  --notes-file "$CHOSCORDB_RELEASE_NOTES"
```

The publisher does these steps:

1. Creates a draft Release or continues an existing draft.
2. Uploads exactly the DMG, the Windows installer and the AppImage.
3. Verifies the remote bytes and the tag and commit identity.
4. Publishes the complete stable Release.

After the publisher completes, fetch the three public versioned URLs.
Then deploy the Pages feeds with `scripts/release/update_feeds.py deploy`.
Read [Windows and Linux release](release-windows-linux.md#publish-and-deploy-the-feeds) for that step.

### Rules for publication

- Do not upload the output directory or use a wildcard path.
- Do not use `gh release upload --clobber`.
- Keep source archives, license archives, checksums, SBOMs, manifests and the appcast local.
- Keep build logs, credential configuration and local paths out of notes and assets.
- Use one maintainer machine at a time; a local lock stops overlapping runs on one machine.
- Do not use `scripts/release/attach_platforms.py` to publish; it cannot attach files.

The publisher also refuses a version that is older than the latest stable Release.
To retry, use the same manifest, notes and package bytes.
The publisher uses matching assets again and uploads only missing files.
It does not overwrite a conflicting asset, tag or Release.
If a check fails after publication, the Release is already public.
Retry the check without a new build, and do not delete the Release.
To fix a faulty public release, publish a higher version.

## Installations of 0.1.7 and older

The `v0.1.7` bundle and older bundles contain the URL `releases/latest/download/choscordb-appcast.xml`.
A three-asset Release has no appcast asset.
Thus that URL stops when a three-asset Release becomes the latest Release.
A change to the source cannot change an installed bundle.

- Keep all older Releases unchanged.
- Do not attach a fourth compatibility asset.
- Tell users of 0.1.7 or older to install the new versioned DMG manually once.

The same app identifier keeps connection profiles, history, recovery, preferences and credentials.

## Rollout checks

Automated checks do not show an installation on macOS 26.0.
Do these manual checks before a release, and record the result of each check.
Keep manual attestations separate from automated evidence.
If you cannot do a check, record it as pending.

1. Install the signed DMG on a clean macOS 26.0 machine or virtual machine.
2. Start ChoscorDB and record the version, the architecture and the result.
3. Do the signed update rehearsal from version A to version B.

In the rehearsal, use an isolated account or virtual machine:

1. Use the rehearsal CMake configuration, a separate Sparkle key account and a test feed.
2. In A, add connection profiles, history, preferences, SQL recovery data and test credentials.
3. Install B through the update and start it again.
4. Verify that all data from A is still there.
5. Test unsaved edits, an active query, an active transaction and a canceled shutdown.
6. Test a persistence failure before the restart.
7. Serve a changed B DMG and verify that A stays installed and usable.
8. Verify the up-to-date message and the network-failure message.

An unsigned fixture or a mocked signature command is not evidence for this check.
Never use a rehearsal output as a production manifest.
Development, smoke and test runs do not check for updates.

## Rehearsal commands

Use a disposable checkout and a dedicated macOS test account or virtual machine.
Do not install the rehearsal build over a production installation.
CMake sets the rehearsal bundle identifier, Application Support directory and credential service to `com.choscor.ChoscorDB.tests.rehearsal`.
The rehearsal feed must be an HTTPS URL that you control.
The `.invalid` URL below is a placeholder.
These commands do not create commits or tags.

1. In the checkout, set the Cargo workspace version to A, for example `0.1.0`.
2. Add a matching changelog entry.
3. Run the build commands below.
4. Run the signature step below.

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

Set `CHOSCORDB_SIGNING_IDENTITY` to the SHA-1 of the Developer ID certificate.
The signature step uses the signature and notarization functions in `scripts/release/macos.py`.
It checks the rehearsal identifier, thins the payload and assesses the result.
It never writes a production manifest.

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

Then make version B:

1. Install A only in the dedicated test environment.
2. Set the version to a higher B, for example `0.1.1`, with a changelog entry.
3. Set `CHOSCORDB_REHEARSAL_OUT="$PWD/build/rehearsal-B"`.
4. Run the build commands and the signature step again.
5. Sign the final stapled B DMG with the rehearsal key account.

```sh
"$CHOSCORDB_REHEARSAL_DEPS/sparkle/bin/sign_update" \
  --account "$CHOSCORDB_REHEARSAL_KEY_ACCOUNT" \
  "$CHOSCORDB_REHEARSAL_OUT/ChoscorDB-rehearsal-0.1.1.dmg"
```

1. Put the B DMG on the HTTPS server that you control.
2. Copy the XML structure of the production appcast into the rehearsal feed.
3. Replace all URLs, versions and signature fields with the rehearsal values.
4. Set `version` and `shortVersionString` to `0.1.1` and `minimumSystemVersion` to `26.0`.
5. Use the signature and length from `sign_update` in the enclosure.
6. Do the rehearsal checks in [Rollout checks](#rollout-checks) and record the results.

Do not use `publish.py` for the rehearsal; it refuses the rehearsal bundle identifier.
For the changed-DMG check, change one byte in a copy of B and keep its original feed signature.
Do not overwrite the good B DMG.
After the rehearsal, remove only the test installation and its test data.
