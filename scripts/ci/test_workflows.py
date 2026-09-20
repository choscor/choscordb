"""Structural regression tests for the repository's GitHub Actions policy."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"


def yaml_block(text, key, indent=0):
    """Return one indentation-delimited YAML mapping block."""
    lines = text.splitlines()
    header = f"{' ' * indent}{key}:"
    start = next(index for index, line in enumerate(lines) if line == header)
    end = len(lines)
    for index in range(start + 1, len(lines)):
        line = lines[index]
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        current_indent = len(line) - len(line.lstrip())
        if current_indent <= indent:
            end = index
            break
    return "\n".join(lines[start:end])


def require_values(block, values):
    active = "\n".join(
        line.partition(" #")[0]
        for line in block.splitlines()
        if not line.lstrip().startswith("#")
    )
    for value in values:
        if value not in active:
            raise AssertionError(f"missing {value!r} from YAML block")


def validate_change_triggers(text):
    triggers = yaml_block(text, "on")
    require_values(
        triggers,
        ("pull_request:", "push:", "branches: [main, master]", "workflow_dispatch:"),
    )


def validate_action_pins(text):
    actions = re.findall(r"^\s*-?\s*uses:\s*([^\s#]+)", text, re.MULTILINE)
    if not actions or any(
        not re.fullmatch(r"[^@]+@[0-9a-f]{40}", item) for item in actions
    ):
        raise AssertionError("every action must use a full immutable commit")


def validate_ci(ci):
    validate_change_triggers(ci)
    quality = yaml_block(ci, "quality", 2)
    require_values(
        quality,
        (
            "quality.py fast",
            "github.com/rhysd/actionlint/cmd/actionlint@v1.7.7",
            'echo "$(go env GOPATH)/bin" >> "$GITHUB_PATH"',
            "cargo install cargo-deny --version 0.20.2 --locked",
            "clang-format-23",
        ),
    )

    platform = yaml_block(ci, "platform", 2)
    require_values(
        platform,
        (
            "fail-fast: false",
            "ubuntu-24.04",
            "windows-2022",
            "macos-15",
            "macos-15-intel",
            "RUSTFLAGS: -D warnings",
            "quality.py rust-check",
            "quality.py rust-clippy",
            "quality.py rust-tests",
            "quality.py native-dependencies",
            "quality.py native-build",
            "quality.py native-tests",
        ),
    )

    static = yaml_block(ci, "native-static-analysis", 2)
    require_values(
        static,
        (
            "clang-tidy-23",
            "choscordb-clang-tidy",
            "choscordb-header-check",
            "choscordb-iwyu",
            "continue-on-error: true",
        ),
    )

    postgres = yaml_block(ci, "postgres-integration", 2)
    require_values(
        postgres,
        (
            "postgresql-17",
            "max-parallel: 1",
            "postgres_fixture.py start",
            "--include-ignored --test-threads=1",
            "quality.py native-tests",
        ),
    )
    if "cargo audit" in ci or re.search(r"retry|rerun-failed", ci, re.IGNORECASE):
        raise AssertionError(
            "required gates must not be duplicated or automatically retried"
        )


def validate_security(security):
    validate_change_triggers(security)
    dependency = yaml_block(security, "dependency-review", 2)
    require_values(
        dependency, ("github.event_name == 'pull_request'", "fail-on-severity: high")
    )
    codeql = yaml_block(security, "codeql", 2)
    languages = set(re.findall(r"^\s+- language: ([a-z-]+)$", codeql, re.MULTILINE))
    if languages != {"rust", "c-cpp", "python", "actions"}:
        raise AssertionError(f"unexpected CodeQL language matrix: {languages}")
    require_values(
        codeql, ("codeql-action/init@", "codeql-action/analyze@", "security-extended")
    )


def validate_coverage(coverage):
    validate_change_triggers(coverage)
    for job, report in (("rust", "rust.lcov"), ("cpp", "cpp.lcov")):
        block = yaml_block(coverage, job, 2)
        require_values(
            block,
            (
                report,
                f"lcov2xml build/coverage/{report}",
                "actions/upload-artifact@",
                "if-no-files-found: error",
            ),
        )
    if re.search(r"fail-under|threshold", coverage, re.IGNORECASE):
        raise AssertionError("coverage is artifact-only during baseline collection")
    rust = yaml_block(coverage, "rust", 2)
    require_values(rust, ("pkg-config", "libdbus-1-dev", "libssl-dev"))


class WorkflowPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.files = {path.name: path.read_text() for path in WORKFLOWS.glob("*.yml")}

    def test_every_action_is_pinned_to_a_full_commit(self):
        for name, text in self.files.items():
            with self.subTest(workflow=name):
                validate_action_pins(text)

    def test_workflows_have_supply_chain_defaults(self):
        for name, text in self.files.items():
            with self.subTest(workflow=name):
                self.assertRegex(text, r"(?m)^permissions:\n  contents: read$")
                self.assertNotIn("permissions: write-all", text)
                self.assertEqual(text.count("runs-on:"), text.count("timeout-minutes:"))
                self.assertIn("concurrency:", text)
                self.assertIn("cancel-in-progress: true", text)
                for checkout in re.finditer(r"uses:\s*actions/checkout@", text):
                    nearby = text[checkout.start() : checkout.start() + 240]
                    self.assertIn("persist-credentials: false", nearby)

    def test_ci_has_required_blocking_and_advisory_jobs(self):
        validate_ci(self.files["ci.yml"])

    def test_security_has_exact_advanced_analysis_matrix(self):
        validate_security(self.files["security.yml"])

    def test_coverage_is_parser_validated_and_separate(self):
        validate_coverage(self.files["coverage.yml"])

    def test_dynamic_analysis_is_scheduled_and_manual_only(self):
        dynamic = self.files["dynamic-analysis.yml"]
        triggers = yaml_block(dynamic, "on")
        require_values(triggers, ("schedule:", "workflow_dispatch:"))
        self.assertNotIn("pull_request:", triggers)
        sanitizer = yaml_block(dynamic, "sanitizers", 2)
        require_values(sanitizer, ("address,undefined", "cmake --preset sanitizers"))
        stress = yaml_block(dynamic, "stress", 2)
        require_values(stress, ("--repeat until-fail:20", "stop on the first failure"))
        self.assertNotRegex(dynamic.lower(), r"retry|rerun-failed")

    def test_linux_cmake_prefix_paths_use_platform_separator(self):
        for name in ("ci.yml", "coverage.yml", "dynamic-analysis.yml"):
            with self.subTest(workflow=name):
                values = re.findall(
                    r"^\s*CMAKE_PREFIX_PATH:\s*(.+)$", self.files[name], re.MULTILINE
                )
                self.assertTrue(values)
                for value in values:
                    self.assertIn("gcc_64:${{ github.workspace }}", value)
                    self.assertNotIn("gcc_64;${{ github.workspace }}", value)

    def test_dependabot_and_cargo_policy_cover_all_dependency_classes(self):
        dependabot = (ROOT / ".github" / "dependabot.yml").read_text()
        for ecosystem in ("cargo", "pip", "github-actions"):
            match = re.search(
                rf"(?ms)^  - package-ecosystem: {ecosystem}$.*?(?=^  - package-ecosystem:|\Z)",
                dependabot,
            )
            self.assertIsNotNone(match)
            require_values(match.group(), ("interval: weekly", "groups:"))
        deny = (ROOT / "deny.toml").read_text()
        require_values(
            deny,
            (
                "[advisories]",
                'yanked = "deny"',
                "[bans]",
                'multiple-versions = "warn"',
                "[licenses]",
                "[sources]",
                'unknown-registry = "deny"',
                'unknown-git = "deny"',
            ),
        )

    def test_mutations_of_material_policy_are_rejected(self):
        security = self.files["security.yml"].replace(
            "          - language: actions\n            build-mode: none\n", ""
        )
        with self.assertRaises(AssertionError):
            validate_security(security)
        coverage = self.files["coverage.yml"].replace(
            "lcov2xml build/coverage/rust.lcov", "true # parser removed"
        )
        with self.assertRaises(AssertionError):
            validate_coverage(coverage)
        ci = self.files["ci.yml"].replace("choscordb-iwyu", "advisory-removed")
        with self.assertRaises(AssertionError):
            validate_ci(ci)
        commented_gate = self.files["ci.yml"].replace(
            "run: python scripts/ci/quality.py fast",
            "run: true # python scripts/ci/quality.py fast",
        )
        with self.assertRaises(AssertionError):
            validate_ci(commented_gate)
        unpinned = self.files["security.yml"].replace(
            "actions/dependency-review-action@a1d282b36b6f3519aa1f3fc636f609c47dddb294",
            "actions/dependency-review-action@v4",
        )
        with self.assertRaises(AssertionError):
            validate_action_pins(unpinned)


if __name__ == "__main__":
    unittest.main()
