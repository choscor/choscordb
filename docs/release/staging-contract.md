# Relocatable application staging

Status: deferred on 2026-09-12 when the user moved all distribution work to a later iteration.

This contract defines the macOS installation boundary for PRD §17 when distribution work resumes.

The staging command invokes `cmake --install` into a new temporary prefix, then a platform adapter deploys the dynamically linked Qt and QScintilla runtime dependencies, required Qt platform plugins, the ChoscorDB license, and available dependency license/source notices. The relocated staged application must launch with development Qt/QScintilla search paths removed and with an owned temporary application-data location. The smoke flow opens the event loop, creates exactly one metadata database inside that location, and exits normally. It must not read or alter the user's regular metadata database.

Staging fails when an expected dependency, platform plugin, license, source-candidate manifest, or executable is missing. It records the source manifest's SHA-256 and provenance alongside a sorted manifest of payload regular files with relative path, byte size, SHA-256 and detected executable status; the manifest and checksum control files are not self-listed. Absolute paths, symlinks escaping the prefix, unresolved non-system dependencies, and references to workspace/build dependency paths are rejected. A checksum file covers the manifest. Existing output prefixes are not overwritten implicitly.

The platform layout is a native `.app` bundle with Frameworks and plugins. The manifest and smoke verifier retain an adapter boundary so other platforms can be added later without duplicating dependency discovery.

Unsigned staging is not a release publication. Full acceptance still needs package metadata, third-party notice completeness, SBOM, source archive, checksums and clean-machine macOS smoke evidence.
