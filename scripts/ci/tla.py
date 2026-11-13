#!/usr/bin/env python3

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
TLA_VERSION = "1.7.4"
JAR_URL = (
    f"https://github.com/tlaplus/tlaplus/releases/download/v{TLA_VERSION}/tla2tools.jar"
)
JAR_SHA256 = "936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88"
JAVA_INSTALL = "install a Java 17 runtime (for example: brew install openjdk@17, or apt-get install openjdk-17-jre-headless)"
TRACE_MODULE = "ConnectionTrace"
TRACE_FAMILY = "connection"
TRACE_COMPLETE_INVARIANT = "TraceIncomplete"
TLC_SAFETY_VIOLATION = 12
TOOL_MISSING = 2


class ToolMissing(RuntimeError):
    pass


class CheckFailed(RuntimeError):
    pass


def install_command():
    return f"{sys.executable} scripts/ci/tla.py install"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_tools(root):
    java = shutil.which("java")
    if java is None:
        raise ToolMissing(
            f"Required tool 'java' was not found. Install it: {JAVA_INSTALL}"
        )
    jar = root / "build/ci/tla/tla2tools.jar"
    if not jar.is_file():
        raise ToolMissing(
            f"TLC {TLA_VERSION} was not found at {jar.relative_to(root)}. "
            f"Install it with: {install_command()}"
        )
    if sha256(jar) != JAR_SHA256:
        raise ToolMissing(
            f"{jar.relative_to(root)} does not match the pinned TLC {TLA_VERSION} sha256. "
            f"Install it again with: {install_command()}"
        )
    return java, jar


def install(root):
    jar = root / "build/ci/tla/tla2tools.jar"
    jar.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=jar.parent, delete=False) as download:
        partial = Path(download.name)
    try:
        print(f"Downloading {JAR_URL}", flush=True)
        try:
            with (
                urllib.request.urlopen(JAR_URL, timeout=120) as response,
                partial.open("wb") as output,
            ):
                shutil.copyfileobj(response, output)
        except OSError as error:
            raise CheckFailed(f"Could not download {JAR_URL}: {error}") from error
        digest = sha256(partial)
        if digest != JAR_SHA256:
            raise CheckFailed(
                f"Downloaded TLC has sha256 {digest}, but the pinned sha256 is {JAR_SHA256}."
            )
        partial.replace(jar)
    finally:
        partial.unlink(missing_ok=True)
    print(f"Installed TLC {TLA_VERSION} at {jar.relative_to(root)}")


def tla_value(value):
    if isinstance(value, bool):
        return "TRUE" if value else "FALSE"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(value, list):
        return "<<" + ", ".join(tla_value(item) for item in value) + ">>"
    if isinstance(value, dict):
        fields = ", ".join(
            f"{name} |-> {tla_value(item)}" for name, item in sorted(value.items())
        )
        return f"[{fields}]"
    raise CheckFailed(f"Cannot write {value!r} as a TLA+ value.")


def tla_set(values):
    return "{" + ", ".join(tla_value(value) for value in values) + "}"


def checked_assumption(name, table, spec):
    return (
        f"ASSUME Assert({table} = {spec}, "
        f'<<"{name} differs from the spec", {table} \\ {spec}, {spec} \\ {table}>>)'
    )


STANDARD_TABLES = ("states", "events", "transitions", "presentation")


def operator_name(key):
    return "".join(part.capitalize() for part in key.split("_"))


def presentation_rows(presentation):
    return [
        [state, row["tone"], row["busy"], row["body"], row["pane"], row["toast"]]
        for state, row in presentation.items()
    ]


def table_module(module, table):
    missing = [key for key in STANDARD_TABLES[:3] if key not in table]
    if missing:
        raise CheckFailed(f"The {module} table has no {', '.join(missing)}.")
    tables = {
        "States": table["states"],
        "Events": table["events"],
        "Transitions": table["transitions"],
    }
    if "presentation" in table:
        tables["Presentation"] = presentation_rows(table["presentation"])
    for key, rows in table.items():
        if key not in STANDARD_TABLES:
            tables[operator_name(key)] = rows
    lines = [f"---- MODULE {module}Table ----", f"EXTENDS {module}, TLC"]
    for name, rows in tables.items():
        lines.append(f"Table{name} == {tla_set(rows)}")
    for name in tables:
        lines.append(checked_assumption(f"The {name} table", f"Table{name}", name))
    lines.extend(["====", ""])
    return "\n".join(lines)


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CheckFailed(f"{path}: {error}") from error


def workspace(root, name):
    directory = root / "build/ci/tla/work" / name
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)
    return directory


def copy_specs(root, work):
    for source in (root / "specs").glob("*/*.tla"):
        shutil.copy2(source, work / source.name)


def run_tlc(java, jar, work, module, config):
    shutil.copy2(config, work / f"{module}.cfg")
    temporary = work / "tmp"
    temporary.mkdir(exist_ok=True)
    command = [
        java,
        f"-Djava.io.tmpdir={temporary}",
        "-XX:+UseParallelGC",
        "-cp",
        str(jar),
        "tlc2.TLC",
        "-workers",
        "1",
        "-deadlock",
        "-cleanup",
        "-metadir",
        str(work / "states"),
        "-config",
        f"{module}.cfg",
        f"{module}.tla",
    ]
    print(f"Running: {subprocess.list2cmdline(command)}", flush=True)
    return subprocess.run(
        command,
        cwd=work,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def family_configs(root):
    return sorted(
        config
        for config in (root / "specs").glob("*/*.cfg")
        if config.stem != TRACE_MODULE
    )


def check_family(java, jar, root, config):
    family = config.parent
    module = config.stem
    table_path = family / "transitions.json"
    if not table_path.is_file():
        raise CheckFailed(f"{table_path.relative_to(root)} is missing.")
    work = workspace(root, family.name)
    copy_specs(root, work)
    (work / f"{module}Table.tla").write_text(
        table_module(module, load_json(table_path)), encoding="utf-8"
    )
    completed = run_tlc(java, jar, work, f"{module}Table", config)
    if completed.returncode != 0:
        print(completed.stdout, end="")
        raise CheckFailed(
            f"TLC found a violation in {config.relative_to(root)} "
            f"(exit code {completed.returncode})."
        )
    print(f"{config.relative_to(root)}: no violations")


def run_all(task, items):
    def attempt(item):
        try:
            task(item)
        except CheckFailed as error:
            return str(error)
        return None

    with ThreadPoolExecutor(max_workers=4) as pool:
        failures = [failure for failure in pool.map(attempt, items) if failure]
    if failures:
        raise CheckFailed("\n".join(failures))


def check(root):
    java, jar = require_tools(root)
    configs = family_configs(root)
    if not configs:
        raise CheckFailed("No TLA+ configurations were found in specs/*/*.cfg.")
    run_all(lambda config: check_family(java, jar, root, config), configs)


def trace_lines(path):
    lines = []
    for number, text in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        if not text.strip():
            continue
        try:
            record = json.loads(text)
        except json.JSONDecodeError as error:
            raise CheckFailed(f"{path}:{number}: {error}") from error
        if not isinstance(record, dict):
            raise CheckFailed(f"{path}:{number}: a trace line must be a JSON object.")
        lines.append(record)
    if not lines:
        raise CheckFailed(f"{path} has no trace lines.")
    return lines


def trace_module(lines):
    return "\n".join(
        [
            f"---- MODULE {TRACE_MODULE}Run ----",
            f"EXTENDS {TRACE_MODULE}",
            f"Recorded == {tla_value(lines)}",
            "====",
            "",
        ]
    )


def trace_config(root):
    return root / "specs" / TRACE_FAMILY / f"{TRACE_MODULE}.cfg"


def check_trace(java, jar, root, path):
    work = workspace(root, f"trace-{re.sub(r'[^A-Za-z0-9_-]', '_', path.stem)}")
    copy_specs(root, work)
    (work / f"{TRACE_MODULE}Run.tla").write_text(
        trace_module(trace_lines(path)), encoding="utf-8"
    )
    completed = run_tlc(java, jar, work, f"{TRACE_MODULE}Run", trace_config(root))
    accepted = (
        completed.returncode == TLC_SAFETY_VIOLATION
        and f"Invariant {TRACE_COMPLETE_INVARIANT} is violated" in completed.stdout
    )
    if not accepted:
        print(completed.stdout, end="")
        raise CheckFailed(
            f"{path}: the trace is not a behavior of Connection.tla "
            f"(TLC exit code {completed.returncode})."
        )
    print(f"{path.name}: accepted")


def traces(root, directory):
    java, jar = require_tools(root)
    paths = sorted(directory.glob("*.ndjson"))
    if not paths:
        raise CheckFailed(
            f"No traces were found in {directory}. Run cargo test --workspace --locked first."
        )
    run_all(lambda path: check_trace(java, jar, root, path), paths)


def main(argv=None, root=ROOT):
    parser = argparse.ArgumentParser(
        description="Install TLC, model-check specs/ and validate connection traces."
    )
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("install")
    commands.add_parser("check")
    trace_parser = commands.add_parser("traces")
    trace_parser.add_argument(
        "--directory", type=Path, default=root / "target/tla-traces"
    )
    args = parser.parse_args(argv)
    try:
        if args.command == "install":
            install(root)
        elif args.command == "check":
            check(root)
        else:
            traces(root, args.directory)
    except ToolMissing as error:
        print(f"tla: {error}", file=sys.stderr)
        return TOOL_MISSING
    except CheckFailed as error:
        print(f"tla: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
