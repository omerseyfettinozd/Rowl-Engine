#!/usr/bin/env python3
"""Static gate: every release-packaging job emits a launcher before verifying.

The Windows packaging step cannot be executed from a Linux runner, so
"the Windows job is fixed" is otherwise unverifiable until someone reads
a CI log. This gate makes the contract statically checkable on any
machine: it parses .github/workflows/ci.yml and asserts that every step
which runs tools/verify_release_package.py also runs
tools/make_release_launchers.py, with a platform that matches the job's
runner.

It also checks the two ends against each other -- the launcher names the
helper emits must be exactly the names the verifier accepts -- so a
rename on either side fails here instead of silently turning the release
gate red on one platform only.

Exit 0 = parity holds. Exit 1 = drift (diagnostics on stderr).
"""

import os
import re
import sys

import yaml


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKFLOW = os.path.join(ROOT, ".github", "workflows", "ci.yml")
VERIFIER = os.path.join(ROOT, "tools", "verify_release_package.py")
LAUNCHER_TOOL = os.path.join(ROOT, "tools", "make_release_launchers.py")

VERIFIER_REF = "tools/verify_release_package.py"
LAUNCHER_REF = "tools/make_release_launchers.py"

# A windows runner must receive the .bat; every other runner here is
# POSIX and must receive at least the .sh. "both" is accepted on POSIX
# because the portable release layout ships the pair.
POSIX_PLATFORMS = {"posix", "both"}
WINDOWS_PLATFORMS = {"windows"}


def fail(message):
    print("[LauncherParity] ERROR: " + message, file=sys.stderr)


def load_workflow():
    with open(WORKFLOW, "r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    jobs = data.get("jobs") or {}
    if not jobs:
        raise ValueError("workflow defines no jobs")
    return jobs


def step_run(step):
    run = step.get("run")
    return run if isinstance(run, str) else ""


def platform_argument(script):
    """Extract the --platform value from a shell snippet, if present."""
    match = re.search(r"--platform[= ]+([A-Za-z]+)", script)
    return match.group(1) if match else None


def runner_platform(runs_on):
    label = " ".join(runs_on) if isinstance(runs_on, list) else str(runs_on)
    if "windows" in label.lower():
        return "windows"
    if "ubuntu" in label.lower() or "linux" in label.lower() or "macos" in label.lower():
        return "posix"
    return "unknown"


def check_workflow():
    problems = []
    checked = []
    for job_name, job in load_workflow().items():
        runs_on = job.get("runs-on")
        expected = runner_platform(runs_on)
        steps = job.get("steps") or []
        for index, step in enumerate(steps):
            script = step_run(step)
            if VERIFIER_REF not in script:
                continue
            title = "%s / %s" % (job_name, step.get("name") or f"step[{index}]")
            checked.append(title)
            if LAUNCHER_REF not in script:
                problems.append(
                    f"{title}: runs the release verifier but never calls "
                    f"{LAUNCHER_REF} — the packaged release would reach the "
                    f"verifier with no launcher and the gate would fail")
                continue
            chosen = platform_argument(script)
            if chosen is None:
                problems.append(
                    f"{title}: calls {LAUNCHER_REF} without an explicit "
                    f"--platform; the platform is inferred from the host at "
                    f"runtime and cannot be audited statically")
                continue
            allowed = WINDOWS_PLATFORMS if expected == "windows" else POSIX_PLATFORMS
            if expected == "unknown":
                problems.append(
                    f"{title}: unrecognized runs-on {runs_on!r}; refusing to "
                    f"guess the launcher platform")
                continue
            if chosen not in allowed:
                problems.append(
                    f"{title}: runner {runs_on!r} requires one of "
                    f"{sorted(allowed)}, but the step requests {chosen!r}")
    if not checked:
        problems.append(
            f"no step in {os.path.relpath(WORKFLOW, ROOT)} runs "
            f"{VERIFIER_REF}; the packaging gate was renamed away and this "
            f"check is now vacuous")
    return checked, problems


def check_launcher_contract():
    """The emitted names must be exactly the names the verifier accepts."""
    problems = []
    with open(LAUNCHER_TOOL, "r", encoding="utf-8") as handle:
        source = handle.read()
    with open(VERIFIER, "r", encoding="utf-8") as handle:
        verifier_source = handle.read()

    namespace = {}
    exec(compile(source, LAUNCHER_TOOL, "exec"), namespace)  # noqa: S102

    windows_launcher = namespace["WINDOWS_LAUNCHER"]
    posix_launcher = namespace["POSIX_LAUNCHER"]
    if not windows_launcher.strip():
        problems.append("the Windows launcher is empty")
    if not posix_launcher.strip():
        problems.append("the POSIX launcher is empty")
    if not posix_launcher.startswith("#!"):
        problems.append("the POSIX launcher has no shebang line")
    # cmd.exe is the consumer of the .bat: every newline must be CRLF.
    # A lone LF survives a text-mode write here (open() in text mode would
    # translate the LF half of a CRLF), so assert both directions.
    if "\r\n" not in windows_launcher:
        problems.append("the Windows launcher has no CRLF line ending")
    if "\n" in windows_launcher.replace("\r\n", ""):
        problems.append("the Windows launcher has a bare LF line ending")
    if "\r" in posix_launcher:
        problems.append("the POSIX launcher has a CR; /bin/sh will not run it")

    emitted = {namespace["POSIX_LAUNCHER_NAME"], namespace["WINDOWS_LAUNCHER_NAME"]}
    match = re.search(r'for name in \(([^)]*)\)', verifier_source)
    if not match:
        problems.append(
            "could not locate the verifier's launcher name list; the "
            "producer/consumer name contract is unverified")
    else:
        accepted = set(re.findall(r'"([^"]+)"', match.group(1)))
        if not accepted:
            problems.append("the verifier's launcher name list is empty")
        for name in sorted(emitted - accepted):
            problems.append(
                f"the helper emits {name!r} but the verifier does not accept it")
        for name in sorted(accepted - emitted):
            problems.append(
                f"the verifier accepts {name!r} but the helper never emits it")
    return problems


def main():
    try:
        checked, problems = check_workflow()
        problems.extend(check_launcher_contract())
    except (OSError, ValueError, yaml.YAMLError) as error:
        print("[LauncherParity] ERROR: " + str(error), file=sys.stderr)
        return 1

    if problems:
        for problem in problems:
            fail(problem)
        print(f"[LauncherParity] {len(problems)} problem(s) across "
              f"{len(checked)} packaging step(s).", file=sys.stderr)
        return 1

    for title in checked:
        print(f"[LauncherParity] OK  {title}")
    print(f"[LauncherParity] {len(checked)} packaging step(s) emit a launcher "
          f"before verification; launcher names match the verifier.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
