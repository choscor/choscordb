# Claude Code instructions

The repository instructions live in `AGENTS.md` and apply in full:

@AGENTS.md

Before finishing a C++/Qt change, run `python3 scripts/ci/quality.py fast` or at
least these focused gates: `cpp_ownership.py`, `ui_policy.py`, `qss_policy.py`,
`ui_consistency.py`, `perf_policy.py`, and `source_inventory.py` under
`scripts/ci/`. Fix findings rather than adding markers or exceptions; when an
exception is necessary, give the reason in the marker or exception entry.
