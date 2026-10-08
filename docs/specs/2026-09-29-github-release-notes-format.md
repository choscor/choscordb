# GitHub release notes format

Status: Ready for implementation  
Date: 2026-09-29  
Source: User request and brainstorm decisions in this session.

## Outcome and current state

Make future ChoscorDB GitHub release notes easy for users to scan by version and change type. The `release-new-version` skill currently drafts a flat `CHANGELOG.md` entry, then prepares public notes in an ignored `build/` file from the changelog at the verified source commit. It requires platform requirements, relevant limitations, a versioned GitHub DMG link, and the Windows unsigned-package notice. `CHANGELOG.md` currently has version headings with ungrouped bullets. The release publisher reads a reviewed notes file and rejects a retry if its text differs from an existing draft or release.

Relevant files: `.claude/skills/release-new-version/SKILL.md`, `CHANGELOG.md`, `docs/MACOS_RELEASE.md`, `docs/WINDOWS_LINUX_RELEASE.md`, and `scripts/release/publish.py`.

Research basis: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) recommends versioned, dated, curated, grouped changes rather than commit-log dumps. [GitHub's release-note style guide](https://docs.github.com/en/contributing/style-guide-and-content-model/style-guide) distinguishes features, changes, fixes, security fixes, and known issues, and asks writers to explain user impact. This spec adapts those conventions to the section names chosen here.

## Scope and decisions

- Update the release-writing instructions in `.claude/skills/release-new-version/SKILL.md` for **GitHub release notes only**. Keep `CHANGELOG.md` as the reviewed source material and leave its existing format unchanged.
- Apply the format to future GitHub releases. Do not rewrite published release notes or historical changelog entries.
- Start the notes body with the version alone as `# X.Y.Z`, followed by the intended publication date in ISO `YYYY-MM-DD` form. Verify the date immediately before creating a remote draft; if publication slips, review the date and any remote-note conflict deliberately under the existing publisher rules.
- Under the heading, use `## Features`, `## Improvements`, and `## Fixes`, in that order, when each has entries. Omit empty sections. A release with no notable item in one category remains valid.
- Use `Features` for new user capability, `Improvements` for notable changes to existing behavior or usability, and `Fixes` for corrected unexpected behavior. Write concise, user-facing bullets describing the effect, with relevant platform or scope qualifiers. Merge related changes; verify every claim against the release diff and changelog. Do not paste raw commits, implementation trivia, or unsupported claims.
- Add `## Security`, `## Upgrade notes`, or `## Known issues` only when applicable. Security covers verified user-relevant security fixes. Upgrade notes cover breaking changes, migrations, changed requirements, deprecations, or user action. Known issues identify a current limitation and any workaround or next action, without presenting an unverified condition as fact. Put these conditional headings after the three core categories, in that order. If an item warrants prominent user action, surface it clearly in Upgrade notes even if it also belongs to a change category.
- Keep the existing mandatory release content: applicable platform requirements, versioned GitHub DMG link, Windows/Linux availability status, and the warning that the Windows ZIP is unsigned and may be warned about or blocked. Include a verified DMG SHA-256 only when available. Place this material under `## Downloads and requirements` after the change and conditional sections. Do not link unpublished local files or imply an unavailable platform package is already public.
- Continue excluding credentials, private paths, private endpoints, real connection data, and raw provider diagnostics from public notes. Keep publication and retry behavior unchanged.

An illustrative structure, with placeholders rather than release claims:

```markdown
# X.Y.Z

Released: YYYY-MM-DD

## Features
- [New capability and user benefit.]

## Improvements
- [Notable refinement to existing behavior.]

## Fixes
- [Problem users experienced and corrected behavior.]

## Upgrade notes
- [Only when users need to act or compatibility changes.]

## Downloads and requirements
- [Verified platform requirements and versioned macOS DMG link.]
- [Accurate Windows/Linux availability and unsigned Windows ZIP notice.]
```

`Security` and `Known issues` appear only when supported by release evidence. The template must not encourage publishing placeholder bullets or empty headings.

## Acceptance criteria and public test seams

| Observable outcome | Test seam |
| --- | --- |
| A future release prepared with the skill produces a GitHub notes file beginning with the matching version alone, then a checked ISO publication date. | Inspect the ignored `build/` notes file that would be passed to `scripts/release/publish.py --notes-file`; compare version with the selected manifest and source changelog. |
| The notes group verified user-facing changes as Features, Improvements, Fixes in that order, omitting empty headings. | Review a representative prepared notes file against the release diff and `CHANGELOG.md` at `source_commit`; include a scenario with one empty category. |
| Security, Upgrade notes, and Known issues appear only with substantiated content; relevant upgrade actions and limitations are legible. | Review a prepared notes file for a release with an applicable condition and one without; inspect the associated source evidence. |
| Download and platform information remains accurate, including macOS requirements and link, Windows/Linux status, and the Windows unsigned notice. | Inspect the prepared notes file against the verified manifest, release guides, and actual public asset state before publication. |
| Existing releases, changelog format, and publisher retry protections remain intact. | Review the skill diff; run existing `scripts/release/test_publish.py` only if publisher behavior changes. No new format validator is required for a skill-only edit. |

## Constraints, failure behavior, and rollout

This is an instruction change, not a release or publication. Do not modify release scripts, package assets, or prior notes merely to satisfy the new prose format. The release workflow still drafts notes from the changelog tied to the exact verified source commit. If the source does not support a claim, remove or correct the claim before publishing. If the expected release date or remote draft body changes, use the existing conflict review flow rather than silently replacing a remote body. Existing verification, signing, upload, and publication gates remain in force.

No data migration is needed. The format starts with the next future release for which the skill is used. The exact wording of an individual release remains editorial judgment grounded in its actual diff and verification evidence.

## Fresh-session handoff

Read this entire spec, inspect the current workspace and release workflow, then invoke `$implement` with this spec path. Implement the skill instruction change and verify it against the acceptance seams above.
