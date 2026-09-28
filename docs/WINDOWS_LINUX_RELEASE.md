# Windows and Linux GitHub Release assets

The [cross-platform workflow](../.github/workflows/cross-platform-release.yml)
builds and tests native Windows x64 and Linux x86_64 binaries on pull requests
or from a manually selected existing `vX.Y.Z` tag. It packages an **unsigned**
portable Windows ZIP and an **unsigned** Linux AppImage. It never creates or
moves a tag, changes release notes, or replaces an existing asset.

The existing macOS publisher creates the stable GitHub Release and its signed
DMG and Sparkle feed. Run the cross-platform workflow after that release is
published, using the same tag. GitHub Actions artifacts are temporary build
candidates; the final files are attached to the versioned GitHub Release.
Because assets are attached after macOS publication, GitHub Release immutability
must remain disabled until all platforms can publish in one transaction.

## First release preparation

1. Run the workflow with `publish=false`. Inspect the Windows ZIP, Linux
   AppImage, inventories, checksums, and native test logs. Install each package
   on a clean target system and check launch, SQLite, PostgreSQL, settings
   persistence, and the native credential backend. CTest uses Qt's offscreen
   platform; the packaged smoke tests only launch and exit.
2. Review the bundled Qt, QScintilla, Cargo, icon, and font notices against the
   actual shipped files. The packager refuses missing pinned Qt/QScintilla
   notices and Cargo license texts. The generated inventory supports a separate
   dependency and redistribution review.
3. State in the release notes that the Windows ZIP is unsigned. Windows
   SmartScreen may warn, and Smart App Control or organization policy may block
   it. A GitHub download URL or SHA-256 checksum does not remove those warnings.
   Do not claim that this unsigned ZIP works on all protected Windows devices.

## Build candidates

From GitHub Actions, dispatch **Windows and Linux release assets** with the
published stable tag, for example `v0.1.7`, and `publish=false`. The workflow
checks out that tag, installs Qt 6.8.3 and SHA-256-pinned QScintilla 2.14.1,
runs native CTest, builds without developer tools, and creates:

- `ChoscorDB-X.Y.Z-windows-x64.zip` with Qt and QScintilla DLLs and licenses;
- `ChoscorDB-X.Y.Z-linux-x86_64.AppImage` with Qt and QScintilla runtime files
  and licenses.

The Linux packager verifies pinned SHA-256 hashes before running linuxdeploy
and its Qt plugin. Both packages are smoke tested with isolated application
data. The AppImage is built on Ubuntu 24.04; compatibility with older Linux
distributions is not claimed.

## Attach assets

After reviewing the candidates, dispatch the same tag with `publish=true`.
The publishing job requires an existing stable GitHub Release and checks that
its remote tag resolves to the checked-out commit. It uploads only the unsigned
Windows ZIP, Linux AppImage, and their versioned SHA-256 file. Matching assets
are reused; conflicting assets fail without replacement. Uploaded bytes and
public download URLs are verified. If publication stops after one asset, rerun
with the same tag and bytes.

The [macOS release guide](MACOS_RELEASE.md) still controls the DMG and Sparkle
feed. This workflow does not add Windows/Linux automatic updates or installers.
