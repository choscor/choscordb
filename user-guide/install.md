# Install and update ChoscorDB

ChoscorDB packages are on the [GitHub Releases page](https://github.com/choscor/choscordb/releases).
Each release has one package for each platform.

| Platform | Package | Signature |
| --- | --- | --- |
| macOS on Apple silicon, macOS 26 or later | `ChoscorDB-X.Y.Z.dmg` | Signed and notarized by Apple |
| Windows x64 | `ChoscorDB-X.Y.Z-windows-x64-setup.exe` | Not signed |
| Linux x86_64 | `ChoscorDB-X.Y.Z-linux-x86_64.AppImage` | Not signed |

`X.Y.Z` is the version number.

## Install on macOS

1. Download the `.dmg` file of the latest release.
2. Open the `.dmg` file.
3. Drag `ChoscorDB` to the `Applications` folder.
4. Open ChoscorDB from the `Applications` folder.

The macOS package contains only Apple silicon (arm64) code.
It needs macOS 26 or later.

## Install on Windows

The Windows installer is not signed.
Windows SmartScreen can show a warning when you start it.
Smart App Control or the policy of your organization can also block it.

1. Download the `-windows-x64-setup.exe` file of the latest release.
2. Start the installer.
3. If SmartScreen shows a warning, make sure that the file came from the GitHub Releases page.
4. Complete the installer steps.
5. Open ChoscorDB from the Start menu.

The installer installs ChoscorDB for your user account only.
It does not need administrator rights.
It installs ChoscorDB in `%LOCALAPPDATA%\Programs\ChoscorDB`.
To remove ChoscorDB, use the list of installed apps in Windows.

## Install on Linux

The Linux package is an AppImage, and it is not signed.
ChoscorDB builds the AppImage on Ubuntu 24.04.
ChoscorDB does not state support for older Linux distributions.

1. Download the `.AppImage` file of the latest release.
2. Move the file to a directory that your user can write to, for example `~/Applications`.
3. Make the file executable with `chmod +x ChoscorDB-X.Y.Z-linux-x86_64.AppImage`.
4. Start the file.

Put the AppImage in a directory that your user can write to.
ChoscorDB can then replace the file when it installs an update.

## Saved passwords

ChoscorDB keeps saved passwords in the credential store of the operating system.
It does not write passwords to its own files.

- On macOS, ChoscorDB uses the Keychain.
- On Windows, ChoscorDB uses the Windows Credential Manager.
- On Linux, ChoscorDB uses the Secret Service, for example GNOME Keyring or KWallet.

## Update on macOS

ChoscorDB uses Sparkle to find and install updates on macOS.
Sparkle checks the signature of each update before it installs it.
ChoscorDB does not install an update without your approval.

1. Open the `ChoscorDB` application menu.
2. Choose `Check for Updates…`.
3. If an update is available, follow the instructions in the update window.

## Update on Windows and Linux

ChoscorDB has its own updater on Windows and Linux.
The updater checks the signature, the size and the SHA-256 hash of each package before it installs it.

The first time that ChoscorDB starts, it asks if it can check for updates in the background.
If you agree, ChoscorDB checks once each 24 hours while it is open.
To change this choice, use `Updates` > `Check Automatically`.

To check for an update now:

1. Choose `Updates` > `Check for Updates…`.
2. Read the release notes in the dialog.
3. Click `Yes` to download the update.
4. Wait while ChoscorDB downloads and verifies the update.
5. When ChoscorDB asks if you want to install the update and restart, click `Yes`.

If you do not install the update immediately, choose `Updates` > `Install Downloaded Update…` later.
Before the update starts, ChoscorDB asks you to save or confirm your open work.
If the update fails on Windows, the previous version stays available.

ChoscorDB cannot replace an AppImage in a directory that your user cannot write to.
In that case, ChoscorDB offers to open the release download page.
Download the new AppImage and replace the old file yourself.

If the update server is not available, ChoscorDB continues to operate.
A manual check shows an error.
A background check shows no message.

## Your data

ChoscorDB keeps connection profiles, history, preferences and recovery data in its application data directory.
An update or a new installation of the same package keeps these data.

| Platform | Application data directory |
| --- | --- |
| macOS | `~/Library/Application Support/com.choscor.ChoscorDB` |
| Windows | `%LOCALAPPDATA%\com.choscor.ChoscorDB` |
| Linux | `~/.local/share/com.choscor.ChoscorDB` |
