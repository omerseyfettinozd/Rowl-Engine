#!/usr/bin/env python3
"""Contract tests for portable release package verification."""

import ast
import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
VERIFIER = ROOT / "tools" / "verify_release_package.py"
LAUNCHER_TOOL = ROOT / "tools" / "make_release_launchers.py"
PARITY_CHECK = ROOT / "tools" / "check_release_launcher_parity.py"

sys.path.insert(0, str(ROOT / "tools"))
import launcher_contract as contract


def run_verifier(release_root):
    return subprocess.run([sys.executable, str(VERIFIER), str(release_root)],
                          capture_output=True, text=True, check=False)


def require_rejection(release_root, message):
    result = run_verifier(release_root)
    if result.returncode == 0 or message not in result.stderr:
        raise SystemExit(f"expected verifier rejection containing {message!r}: "
                         f"stdout={result.stdout!r}, stderr={result.stderr!r}")


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    release = root / "release"
    package = release / "Assets" / "packages" / "game.rowlpkg"
    package.parent.mkdir(parents=True)
    subprocess.run([sys.executable, str(PACKAGER), str(ROOT / "Assets"), str(package)], check=True)
    (release / "mods").mkdir()
    (release / "mods" / "README.md").write_text("# mods\n", encoding="utf-8")
    (release / "README.txt").write_text("release\n", encoding="utf-8")
    shutil.copy2(ROOT / "packaging" / "THIRD_PARTY_NOTICES.md",
                 release / "THIRD_PARTY_NOTICES.md")
    (release / "RowlGame").write_bytes(b"player")
    (release / "libRowlEngineCore.so").write_bytes(b"runtime")
    # Binary write. pathlib.write_text() defaults to newline=None, which maps
    # every "\n" onto the host's os.linesep: LF here, CRLF on the Windows
    # runner. The verifier then rejected its own valid fixture with "run_game.sh
    # has a CR" and turned rowl_release_package_tool_tests red in the Windows
    # matrix only. The bytes are asserted so a host that still rewrites them
    # fails HERE, with a message naming the cause, instead of three CI jobs
    # later.
    launcher_bytes = b"#!/bin/sh\nexec ./RowlGame \"$@\"\n"
    (release / "run_game.sh").write_bytes(launcher_bytes)
    if b"\r" in (release / "run_game.sh").read_bytes():
        raise SystemExit(f"run_game.sh fixture carries a CR: {launcher_bytes!r}")

    valid = run_verifier(release)
    if valid.returncode != 0 or "Valid release" not in valid.stdout:
        raise SystemExit("valid release package was rejected: " + valid.stderr)

    missing_launcher = root / "missing-launcher"
    shutil.copytree(release, missing_launcher)
    (missing_launcher / "run_game.sh").unlink()
    require_rejection(missing_launcher, "missing release launcher")

    # --- P2-16: the Windows launcher contract (ADIM 3/4/5) ---
    #
    # Until this block the only launcher fixture in the repo was run_game.sh,
    # so every launcher assertion in the suite described the POSIX launcher.
    # run_game.bat had NO fixture at all: deleting `cd /d "%~dp0"` from it, or
    # corrupting the exe name, shipped a release that cannot start while
    # check_release_launcher_parity.py, this file and the CI packaging step
    # all stayed green. The release verifier used to stop at "a launcher file
    # exists and is non-empty".
    #
    # The Windows layout is built here from the REAL producer --
    # tools/make_release_launchers.py -- rather than from an inlined string,
    # so the fixture cannot drift from what CI actually writes.
    def stage_windows_release(tag, launcher_bytes=None, emit_platform="windows"):
        """A release shaped like the Windows packaging step's output."""
        staged = root / tag
        staged.mkdir()
        (staged / "Assets" / "packages").mkdir(parents=True)
        shutil.copy2(release / "Assets" / "packages" / "game.rowlpkg",
                     staged / "Assets" / "packages" / "game.rowlpkg")
        shutil.copy2(release / "Assets" / "packages" / "game.rowlpkg.sha256",
                     staged / "Assets" / "packages" / "game.rowlpkg.sha256")
        (staged / "mods").mkdir()
        (staged / "mods" / "README.md").write_text("# mods\n", encoding="utf-8")
        (staged / "README.txt").write_text("release\n", encoding="utf-8")
        shutil.copy2(ROOT / "packaging" / "THIRD_PARTY_NOTICES.md",
                     staged / "THIRD_PARTY_NOTICES.md")
        (staged / "RowlGame.exe").write_bytes(b"player")
        (staged / "RowlEngineCore.dll").write_bytes(b"runtime")
        emit = subprocess.run(
            [sys.executable, str(LAUNCHER_TOOL), str(staged),
             "--platform", emit_platform],
            capture_output=True, text=True, check=False)
        if emit.returncode != 0:
            raise SystemExit("make_release_launchers.py failed on the "
                             f"{tag} fixture: stdout={emit.stdout!r} "
                             f"stderr={emit.stderr!r}")
        if launcher_bytes is not None:
            (staged / "run_game.bat").write_bytes(launcher_bytes)
        return staged

    # Positive: the launcher CI really writes must be accepted.
    windows_release = stage_windows_release("windows-release")
    if not (windows_release / "run_game.bat").is_file():
        raise SystemExit("the Windows fixture did not get a run_game.bat")
    if (windows_release / "run_game.sh").exists():
        raise SystemExit("a --platform windows fixture must not ship run_game.sh")
    accepted = run_verifier(windows_release)
    if accepted.returncode != 0 or "Valid release" not in accepted.stdout:
        raise SystemExit("verifier rejected the Windows-shaped release: "
                         + accepted.stderr)

    # The emitted .bat must really be CRLF on disk -- the constant can be
    # right while the writer translates the newlines away.
    emitted = (windows_release / "run_game.bat").read_bytes()
    if b"\r\n" not in emitted or b"\n" in emitted.replace(b"\r\n", b""):
        raise SystemExit(f"emitted run_game.bat is not CRLF: {emitted!r}")

    # Negative 0: the P2-16 demo fixture, verbatim. A Windows-shaped release
    # (RowlGame.exe + DLLs) carrying only a POSIX run_game.sh. The old
    # existence check accepted it, and so did a content check at MINIMAL
    # tier -- a shell script that execs the player IS a valid .sh. What makes
    # it broken is that the .bat its platform needs is absent.
    sh_only = stage_windows_release("windows-release-sh-only")
    (sh_only / "run_game.bat").unlink()
    (sh_only / "run_game.sh").write_bytes(b'#!/bin/sh\nexec ./RowlGame.exe "$@"\n')
    require_rejection(sh_only, "ships RowlGame.exe but no run_game.bat")

    # Negative 1: a Windows release whose only launcher is removed. This is
    # the shape the P2-16 report called out: run_game.bat exists in no fixture
    # anywhere in the suite, so "delete the launcher" was only ever rehearsed
    # against run_game.sh.
    bare = stage_windows_release("no-windows-launcher")
    (bare / "run_game.bat").unlink()
    require_rejection(bare, "missing release launcher")

    # Negative 1b: the launcher is present but does not start the player.
    # The old check only asked "exists and is non-empty".
    require_rejection(
        stage_windows_release("empty-windows-launcher",
                              b"@echo off\r\ncd /d \"%~dp0\"\r\n"),
        "no line that runs the player with %*")

    # Negative 2: `cd /d "%~dp0"` deleted (the P2-16 mutation verbatim).
    require_rejection(
        stage_windows_release(
            "bat-no-cd",
            b"@echo off\r\nRowlGame.exe %*\r\n"),
        "missing the working-directory pin")

    # Negative 3: exe name corrupted.
    require_rejection(
        stage_windows_release(
            "bat-bad-exe",
            b'@echo off\r\ncd /d "%~dp0"\r\nRowlGameX.exe %*\r\n'),
        "is not a player executable the release verifier accepts")

    # Negative 4: LF instead of CRLF.
    require_rejection(
        stage_windows_release(
            "bat-lf-endings",
            b'@echo off\ncd /d "%~dp0"\nRowlGame.exe %*\n'),
        "bare LF line ending")

    # Negative 5: the player runs before the working directory is pinned.
    require_rejection(
        stage_windows_release(
            "bat-cd-after-exec",
            b'@echo off\r\nRowlGame.exe %*\r\ncd /d "%~dp0"\r\n'),
        "before the 'cd /d")

    # Negative 6: a .bat that execs nothing at all.
    require_rejection(
        stage_windows_release(
            "bat-no-exec",
            b'@echo off\r\ncd /d "%~dp0"\r\necho nothing to run\r\n'),
        "no line that runs the player with %*")

    print("[ReleasePackageTests] Windows launcher contract: the emitted .bat "
          "is accepted; missing pin, bad exe name, LF endings, wrong order and "
          "no exec line are all rejected.")

    # --- P2-16: the POSIX launcher must still exec the shipped player ---
    for tag, content, fragment in [
        ("sh-bad-exe", "#!/bin/sh\nexec ./RowlGameX \"$@\"\n",
         "is not a player executable"),
        ("sh-no-args", "#!/bin/sh\nexec ./RowlGame\n",
         'forwards "$@"'),
    ]:
        staged = root / tag
        shutil.copytree(release, staged)
        (staged / "run_game.sh").write_bytes(content.encode("utf-8"))
        require_rejection(staged, fragment)
    # A CR anywhere in the POSIX launcher: /bin/sh will not run it. The bytes
    # are built with the SAME function the parity gate uses to model a Windows
    # text-mode write, so this case cannot drift away from the defect it
    # stands for -- it is literally the payload the Windows runner produced.
    staged = root / "sh-crlf"
    shutil.copytree(release, staged)
    (staged / "run_game.sh").write_bytes(
        contract.windows_text_mode_write("#!/bin/sh\nexec ./RowlGame \"$@\"\n")
        .encode("utf-8"))
    require_rejection(staged, "has a CR")

    print("[ReleasePackageTests] POSIX launcher contract: bad exe name, "
          "dropped argument forward and CR line endings are rejected.")

    # --- P2-16 ADIM 5: the gates must be able to go RED ---
    #
    # Everything above mutates a fixture. That proves the VERIFIER can fail,
    # but not that the parity gate -- the one CI actually runs on Linux to
    # stand in for the Windows job -- can. So the real source of
    # tools/make_release_launchers.py is mutated here, in a scratch copy of
    # the repo, and tools/check_release_launcher_parity.py is run against it.
    # This is the exact P2-16 mutation: delete `cd /d "%~dp0"`, break the exe
    # name, downgrade CRLF to LF. Every one of them was green before this
    # block existed.
    def parity_against_mutated_tool(tag, mutate, mutate_editor=None):
        """Copy the repo, mutate a launcher producer, run the parity gate.

        Returns the CompletedProcess; the caller decides whether red is the
        expected outcome.
        """
        scratch = root / f"parity-{tag}"
        tools_dir = scratch / "tools"
        tools_dir.mkdir(parents=True)
        for name in ("make_release_launchers.py", "check_release_launcher_parity.py",
                     "launcher_contract.py", "verify_release_package.py"):
            shutil.copy2(ROOT / "tools" / name, tools_dir / name)
        (scratch / ".github" / "workflows").mkdir(parents=True)
        shutil.copy2(ROOT / ".github" / "workflows" / "ci.yml",
                     scratch / ".github" / "workflows" / "ci.yml")
        editor_dir = scratch / "editor" / "Services"
        editor_dir.mkdir(parents=True)
        editor_source = (ROOT / "editor" / "Services"
                         / "ProjectBuildService.cs").read_text(encoding="utf-8")
        if mutate_editor is not None:
            editor_source = mutate_editor(editor_source)
        (editor_dir / "ProjectBuildService.cs").write_text(editor_source,
                                                          encoding="utf-8")

        producer = tools_dir / "make_release_launchers.py"
        source = producer.read_text(encoding="utf-8")
        producer.write_text(mutate(source), encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(tools_dir / "check_release_launcher_parity.py")],
            capture_output=True, text=True, check=False)

    def drop_cd_line(source):
        mutated = source.replace("    'cd /d \"%~dp0\"\\r\\n'\n", "")
        if mutated == source:
            raise SystemExit("drop_cd_line changed nothing; the P2-16 "
                             "mutation no longer matches the source")
        return mutated

    def break_exe_name(source):
        mutated = source.replace('"RowlGame.exe %*\\r\\n"', '"RowlGameX.exe %*\\r\\n"')
        if mutated == source:
            raise SystemExit("break_exe_name changed nothing; the P2-16 "
                             "mutation no longer matches the source")
        return mutated

    def downgrade_to_lf(source):
        mutated = source.replace('\\r\\n"', '\\n"')
        if mutated == source:
            raise SystemExit("downgrade_to_lf changed nothing; the P2-16 "
                             "mutation no longer matches the source")
        return mutated

    def route_windows_to_posix(source):
        """--platform windows shipping the POSIX launcher.

        The content checks below all still pass on this: the POSIX launcher
        satisfies the contract, it is just the wrong file. Only checking the
        LAUNCHERS routing table catches it.
        """
        mutated = source.replace(
            '"windows": ((WINDOWS_LAUNCHER_NAME, WINDOWS_LAUNCHER, False),)',
            '"windows": ((POSIX_LAUNCHER_NAME, POSIX_LAUNCHER, True),)')
        if mutated == source:
            raise SystemExit("route_windows_to_posix changed nothing; the "
                             "LAUNCHERS table no longer has that shape")
        return mutated

    def revert_to_text_mode_write(source):
        """Put the writer back into text mode -- the Windows-only bug itself.

        This is not a proxy for the defect, it IS the defect: a text-mode write
        maps every "\\n" onto the host's os.linesep, so run_game.sh ships with a
        CR when produced on a Windows runner and the release verifier rejects
        its own valid package. On Linux it is invisible -- os.linesep is
        already "\\n" -- which is how it reached CI with every Linux, sanitizer
        and arm64 job green.

        The gate must catch it two ways: statically (emit() no longer writes
        through the binary writer) and behaviourally (the real emit() is run
        against a simulated Windows host and the bytes it writes are checked).
        """
        mutated = source.replace(
            "launcher_contract.write_launcher_bytes(path, name, content)",
            'with open(path, "w", encoding="utf-8") as handle:\n'
            "            handle.write(content)")
        if mutated == source:
            raise SystemExit("revert_to_text_mode_write changed nothing; "
                             "emit() no longer calls write_launcher_bytes")
        return mutated

    def drop_executable_bit(source):
        mutated = source.replace(
            "mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH", "mode")
        if mutated == source:
            raise SystemExit("drop_executable_bit changed nothing")
        return mutated

    # Control first: the unmutated copy must be GREEN, otherwise "red" below
    # would only prove the harness is broken.
    control = parity_against_mutated_tool("control", lambda source: source + "\n")
    if control.returncode != 0:
        raise SystemExit("the parity gate rejected the unmutated launcher "
                         "producer: stdout={0.stdout!r} "
                         "stderr={0.stderr!r}".format(control))

    # Some mutations only MEAN something on some platforms. `no-exec-bit` is
    # the clear case, and this run is its CI failure:
    #
    #     the parity gate stayed GREEN on mutation 'no-exec-bit'
    #
    # The gate was right. os.chmod() on Windows honours nothing but the
    # read-only flag -- "you can only set the file's read-only flag with it.
    # All other bits are ignored" (Python docs, os.chmod) -- and os.access(p,
    # os.X_OK) there is an existence check, not an executable check: "On
    # Windows, the X_OK mode determines whether the file exists" (Python docs,
    # os.access). So stripping S_IXUSR|S_IXGRP|S_IXOTH changes no observable
    # state on a Windows runner: the mutation is a NO-OP there. A gate that
    # went red on it would have to invent a violation that does not exist.
    #
    # So the mutation is scoped, NOT skipped. A silent `continue` is exactly
    # how fake-green gates are born: nobody reads the log, the mutation
    # quietly stops testing anything, and the next person to look sees a
    # green gate and a test file with no failing case in it. Instead each
    # mutation declares the platforms it is meaningful on, the run records
    # which ones were vacuous HERE and says so out loud, and it asserts the
    # vacuous ones still came back GREEN -- a scoped mutation that went red
    # would mean the scope list is wrong, and that is also a failure.
    #
    # Scoping NARROWS nothing that matters: on a POSIX host every mutation
    # below is in scope and must still go red, which is what the
    # "scoped mutations must stay green" check is guarding against someone
    # later "fixing" CI by widening the scope.
    def host_supports_executable_bit():
        """Does os.access(path, os.X_OK) mean anything on this host?

        Probed rather than assumed, so the scope below cannot drift away
        from the platform it is actually running on.

        The two platforms have different signatures, and both are recognised:

          POSIX    0o644 -> False, 0o755 -> True. The answers differ, so
                   clearing the execute bits is observable and the mutation
                   means something here.
          Windows  0o644 -> True,  0o755 -> True. X_OK degenerated into an
                   existence check and the file exists either way, so the
                   answers are equal and the bit carries no information.

        Anything else is refused rather than guessed at -- in particular a
        0o755 file reading non-executable, which means something other than
        platform semantics is in play (a noexec mount, a filesystem that
        does not carry the bit) and the probe would be proving nothing.
        """
        probe = root / "exec-bit-probe.sh"
        probe.write_bytes(b"#!/bin/sh\nexit 0\n")
        os.chmod(probe, 0o644)
        executable_without_x_bit = os.access(probe, os.X_OK)
        os.chmod(probe, 0o755)
        executable_with_x_bit = os.access(probe, os.X_OK)
        if executable_without_x_bit is False and executable_with_x_bit is True:
            return True   # POSIX: clearing the bits is observable
        if executable_without_x_bit is True and executable_with_x_bit is True:
            # Windows: X_OK answered "it exists" both times, so the bit
            # carries no information and the mutation is vacuous here.
            return False
        raise SystemExit(
            "the executable-bit probe is inconclusive on this host "
            f"(0o644 -> {executable_without_x_bit}, 0o755 -> "
            f"{executable_with_x_bit}); neither platform's signature "
            "matched, so the mutation scope below cannot be decided from "
            "it. A 0o755 file that reads non-executable usually means a "
            "noexec mount or a filesystem that does not carry the bit.")

    executable_bits_are_real = host_supports_executable_bit()

    # Every mutation, with the platforms it is meaningful on. "any" means the
    # mutation is platform-independent and must be red EVERYWHERE -- these
    # are the ones that carry the contract, so scoping them away would be
    # the real regression. "posix" means the mutation only removes observable
    # state on POSIX, and on any other platform it is expected to stay green.
    MUTATIONS = [
        ("no-cd", drop_cd_line, "missing the working-directory pin", "any"),
        ("bad-exe", break_exe_name,
         "is not a player executable the release verifier accepts", "any"),
        ("lf-endings", downgrade_to_lf, "bare LF line ending", "any"),
        ("windows-routes-to-posix", route_windows_to_posix,
         "would write ['run_game.sh'], not 'run_game.bat'", "any"),
        ("text-mode-write", revert_to_text_mode_write,
         "simulated Windows host", "any"),
        ("no-exec-bit", drop_executable_bit, "is not executable", "posix"),
    ]

    def in_scope(platforms):
        if platforms == "any":
            return True
        if platforms == "posix":
            return executable_bits_are_real
        raise SystemExit(f"unknown mutation scope {platforms!r}")

    scoped_green = []
    red_count = 0

    for tag, mutate, fragment, platforms in MUTATIONS:
        result = parity_against_mutated_tool(tag, mutate)
        if not in_scope(platforms):
            # Not skipped -- checked, and the outcome asserted. A mutation
            # that cannot violate the contract on this platform must not
            # manufacture a violation; if it went red anyway, the scope
            # declaration is stale and that is worth failing over.
            if result.returncode != 0:
                raise SystemExit(
                    f"mutation {tag!r} is declared out of scope on this host "
                    f"({platforms!r}) but the parity gate went RED on it, so "
                    f"the scope declaration is wrong: "
                    f"stdout={result.stdout!r}\nstderr={result.stderr!r}")
            scoped_green.append(tag)
            print(f"[ReleasePackageTests] parity gate GREEN on {tag}: "
                  f"declared {platforms!r}, vacuous on this host (verified "
                  f"out of scope, not skipped)")
            continue
        red_count += 1
        if result.returncode == 0:
            raise SystemExit(
                f"the parity gate stayed GREEN on mutation {tag!r} - the "
                f"launcher contract is not actually enforced:\n"
                f"stdout={result.stdout!r}\nstderr={result.stderr!r}")
        if fragment not in result.stderr:
            raise SystemExit(
                f"the parity gate went red on {tag!r} but not for the expected "
                f"reason ({fragment!r}): stderr={result.stderr!r}")
        print(f"[ReleasePackageTests] parity gate RED on {tag}: {fragment}")

    # The scope list is only honest if it is doing something. On a host with
    # real executable bits every mutation must have been exercised, and on a
    # host without them the POSIX-only ones must have been reported as
    # vacuous rather than quietly vanishing from the log. Either way the two
    # counts have to add up to the number of mutations.
    if red_count + len(scoped_green) != len(MUTATIONS):
        raise SystemExit(
            f"mutation accounting is broken: {red_count} red + "
            f"{len(scoped_green)} scoped-green != {len(MUTATIONS)} mutations")
    if executable_bits_are_real and scoped_green:
        raise SystemExit(
            f"this host has real executable bits, so every mutation is in "
            f"scope, but {scoped_green} were skipped as vacuous; the scope "
            f"declarations are too narrow")

    # The editor (editor/Services/ProjectBuildService.cs) writes its own copy
    # of the launcher text. It is a second producer, so it is held to the same
    # contract: dropping the cd pin there must turn the gate red even though
    # tools/make_release_launchers.py is untouched.
    def editor_drops_cd(source):
        mutated = source.replace('\\r\\ncd /d \\"%~dp0\\"', '\\r\\n')
        if mutated == source:
            raise SystemExit("editor_drops_cd changed nothing; the editor "
                             "launcher literal no longer has that shape")
        return mutated

    editor_mutation = parity_against_mutated_tool(
        "editor-drops-cd", lambda source: source + "\n",
        mutate_editor=editor_drops_cd)
    if editor_mutation.returncode == 0:
        raise SystemExit(
            "the parity gate stayed GREEN when the editor's launcher lost its "
            f"cd pin: stdout={editor_mutation.stdout!r}")
    if "missing the working-directory pin" not in editor_mutation.stderr:
        raise SystemExit(
            "the editor mutation went red for the wrong reason: "
            f"{editor_mutation.stderr!r}")
    print("[ReleasePackageTests] parity gate RED on editor-drops-cd: the "
          "editor's launcher copy is checked too.")
    red_count += 1  # the editor is a second producer, held to the same contract

    # --- Console output must be ASCII ---
    #
    # This block's own failure message once read:
    #
    #   the parity gate stayed GREEN on mutation 'no-exec-bit' <U+2014> the
    #   launcher contract is not actually enforced
    #
    # The em dash is invisible in the source and in a Linux terminal, and on
    # the Windows runner it reached the console as U+FFFD, so the log showed
    #
    #   ... on mutation 'no-exec-bit' - the launcher contract ...
    #
    # Nothing failed. Exit codes matched, assertions passed, and the log
    # simply disagreed with the source -- which is the worst failure mode
    # there is, because the one artifact a human reads to find out what CI
    # did is the artifact that got mangled.
    #
    # Fixing the characters would only hold until the next person typed a
    # dash, so the invariant is enforced instead. This walks the AST rather
    # than grepping bytes, because the distinction that matters is REACHABLE:
    # a non-ASCII character inside a string literal can be printed to a
    # Windows console, while one inside a comment or docstring cannot and is
    # perfectly safe. A grep for U+2014 would forbid both and train people to
    # work around the check.
    def console_safe_sources():
        """Files whose diagnostics land in a CI log."""
        return [ROOT / "tests" / "test_release_package.py",
                ROOT / "tools" / "check_release_launcher_parity.py",
                ROOT / "tools" / "launcher_contract.py",
                ROOT / "tools" / "make_release_launchers.py"]

    def non_ascii_literals(path):
        """(line, char) for non-ASCII chars inside printable string literals.

        Docstrings are excluded. They are string literals to the AST, but a
        docstring is documentation, not a diagnostic: nothing prints it unless
        it is handed to something like argparse's `description=`, and
        launcher_contract.py -- where the remaining dashes live -- is a pure
        library with no argparse and no main() at all. Keeping them would mean
        the guard was enforcing "no non-ASCII anywhere" while claiming to
        enforce "no non-ASCII on the console", and a check that overstates
        what it checks is one people learn to route around.
        """
        tree = ast.parse(path.read_text(encoding="utf-8"))
        docstrings = set()
        for node in ast.walk(tree):
            if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef,
                                 ast.AsyncFunctionDef)):
                body = getattr(node, "body", None)
                if (body and isinstance(body[0], ast.Expr)
                        and isinstance(body[0].value, ast.Constant)
                        and isinstance(body[0].value.value, str)):
                    docstrings.add(id(body[0].value))
        found = []
        for node in ast.walk(tree):
            if (isinstance(node, ast.Constant) and isinstance(node.value, str)
                    and id(node) not in docstrings):
                for char in node.value:
                    if ord(char) > 127:
                        found.append((node.lineno, char))
        return found

    for source_path in console_safe_sources():
        offenders = non_ascii_literals(source_path)
        if offenders:
            listed = ", ".join(
                f"line {line} U+{ord(char):04X}" for line, char in offenders)
            raise SystemExit(
                f"{source_path.name} prints non-ASCII text to the console: "
                f"{listed}. The Windows runner renders it as U+FFFD, so the "
                f"CI log stops matching the source. Use an ASCII dash ('-') "
                f"in any string that reaches stdout or stderr; non-ASCII in "
                f"a comment or docstring is fine and is not checked here.")
    print("[ReleasePackageTests] console output is ASCII; the Windows log "
          "cannot mangle these diagnostics.")

    # The counts are reported as measured, not as assumed. The previous
    # wording hardcoded "7 launcher mutations red", which was true on Linux
    # and false on the Windows runner -- and a summary that states a fixed
    # number cannot tell a reader which number it actually got.
    if scoped_green:
        print("[ReleasePackageTests] check_release_launcher_parity.py is "
              f"red-capable: control green, {red_count} of {len(MUTATIONS) + 1} "
              f"mutations red, {len(scoped_green)} verified out of scope on "
              f"this host ({', '.join(scoped_green)}).")
    else:
        print("[ReleasePackageTests] check_release_launcher_parity.py is "
              f"red-capable: control green, all {len(MUTATIONS) + 1} mutations "
              f"red (missing cd pin in the tool and in the editor, broken exe "
              f"name, LF endings, wrong platform routing, text-mode write, "
              f"missing exec bit).")

    missing_runtime = root / "missing-runtime"
    shutil.copytree(release, missing_runtime)
    (missing_runtime / "libRowlEngineCore.so").unlink()
    require_rejection(missing_runtime, "missing native RowlEngineCore runtime library")

    corrupt_package = root / "corrupt-package"
    shutil.copytree(release, corrupt_package)
    (corrupt_package / "Assets" / "packages" / "game.rowlpkg").write_bytes(b"ROWL")
    require_rejection(corrupt_package, "package is shorter than its v1 header")

    loose_assets = root / "loose-assets"
    shutil.copytree(release, loose_assets)
    (loose_assets / "Assets" / "images").mkdir()
    require_rejection(loose_assets, "release Assets contains loose content")

    unsafe_mod = root / "unsafe-mod"
    shutil.copytree(release, unsafe_mod)
    outside = root / "outside.txt"
    outside.write_text("outside", encoding="utf-8")
    try:
        (unsafe_mod / "mods" / "override.txt").symlink_to(outside)
    except OSError as error:
        raise SystemExit("test host cannot create a symbolic link: " + str(error))
    require_rejection(unsafe_mod, "mods override contains a symbolic link")

    # --- Faz 6 Dilim 2: packer `verify` + `.sha256` sidecar contract ---
    def run_packer(*args):
        return subprocess.run([sys.executable, str(PACKAGER), *[str(a) for a in args]],
                              capture_output=True, text=True, check=False)

    def require_verify(package_path, code, fragment=None, json_mode=False):
        args = ["verify", package_path] + (["--json"] if json_mode else [])
        result = run_packer(*args)
        combined = result.stdout + result.stderr
        if result.returncode != code:
            raise SystemExit(f"expected packer verify exit {code}, got {result.returncode}: "
                             f"stdout={result.stdout!r}, stderr={result.stderr!r}")
        if fragment is not None and fragment not in combined:
            raise SystemExit(f"expected packer verify output containing {fragment!r}: "
                             f"stdout={result.stdout!r}, stderr={result.stderr!r}")
        return result

    pack_src = root / "pack-src"
    (pack_src / "sub").mkdir(parents=True)
    (pack_src / "a.png").write_bytes(b"fake-png-bytes-0001")
    (pack_src / "sub" / "b.wav").write_bytes(b"fake-wav-bytes-0002")
    pkg_a = root / "a.rowlpkg"

    pack_result = run_packer(pack_src, pkg_a)
    if pack_result.returncode != 0:
        raise SystemExit("packer failed on small asset dir: " + pack_result.stderr)

    # .sha256 sidecar: canonical `<hash><two-spaces><basename>\n`, sha256sum-compatible.
    sidecar_a = pathlib.Path(str(pkg_a) + ".sha256")
    if not sidecar_a.is_file():
        raise SystemExit("packer did not write the .sha256 sidecar")
    package_digest = hashlib.sha256(pkg_a.read_bytes()).hexdigest()
    expected_sidecar = f"{package_digest}  {pkg_a.name}\n"
    if sidecar_a.read_text(encoding="utf-8") != expected_sidecar:
        raise SystemExit("sidecar is not in canonical '<hash>  <basename>\\n' form: "
                         f"{sidecar_a.read_text(encoding='utf-8')!r}")

    # pack -> verify OK (human-readable and --json).
    require_verify(pkg_a, 0, "OK")
    json_ok = require_verify(pkg_a, 0, '"ok": true', json_mode=True)
    payload = json.loads(json_ok.stdout)
    if payload.get("entries") != 3 or payload.get("sidecar") != "ok":
        raise SystemExit("verify --json payload is wrong: " + json_ok.stdout)

    # Deterministic .sha256: packing the same tree to the same path twice
    # yields identical sidecar bytes (and identical package bytes).
    sidecar_before = sidecar_a.read_bytes()
    package_before = pkg_a.read_bytes()
    pack_again = run_packer(pack_src, pkg_a)
    if pack_again.returncode != 0:
        raise SystemExit("second packer run failed: " + pack_again.stderr)
    if sidecar_a.read_bytes() != sidecar_before or pkg_a.read_bytes() != package_before:
        raise SystemExit("deterministic pack produced different bytes")

    # 1 flipped byte -> exit 1 (stale sidecar mismatch). The flip targets the
    # LAST byte (index-table tail: the manifest entry path), never the payload:
    # payload bytes of zstd-compressed entries are not re-hashed by verify
    # (see KI-11), so a payload flip is red in raw mode but green when the
    # `zstandard` module is present. The index tail is covered in both modes.
    pkg_corrupt = root / "corrupt.rowlpkg"
    shutil.copy2(pkg_a, pkg_corrupt)
    pathlib.Path(str(pkg_corrupt) + ".sha256").write_text(
        f"{package_digest}  {pkg_corrupt.name}\n", encoding="utf-8")
    raw = bytearray(pkg_corrupt.read_bytes())
    raw[-1] ^= 1
    pkg_corrupt.write_bytes(bytes(raw))
    require_verify(pkg_corrupt, 1, "does not match")

    # Missing sidecar -> still exit 1 (fail-closed distribution gate), even
    # though the package itself is internally consistent.
    pkg_nosidecar = root / "nosidecar.rowlpkg"
    shutil.copy2(pkg_a, pkg_nosidecar)
    require_verify(pkg_nosidecar, 1, "missing")

    # Manifest-broken but sidecar-fresh -> exit 1 via the deep manifest check.
    fresh_digest = hashlib.sha256(pkg_corrupt.read_bytes()).hexdigest()
    pathlib.Path(str(pkg_corrupt) + ".sha256").write_text(
        f"{fresh_digest}  {pkg_corrupt.name}\n", encoding="utf-8")
    require_verify(pkg_corrupt, 1, "manifest")

    # Missing package file -> exit 1.
    require_verify(root / "does-not-exist.rowlpkg", 1, "does not exist")

    # Malformed sidecar -> exit 1 (unreadable digest, not a hash compare).
    pkg_malformed = root / "malformed.rowlpkg"
    shutil.copy2(pkg_a, pkg_malformed)
    pathlib.Path(str(pkg_malformed) + ".sha256").write_text(
        "not-a-digest\n", encoding="utf-8")
    require_verify(pkg_malformed, 1, "malformed")

    # Usage error -> exit 2.
    usage = run_packer("verify")
    if usage.returncode != 2:
        raise SystemExit("expected packer verify usage error exit 2, got "
                         f"{usage.returncode}")

    print("[ReleasePackageTests] packer verify + .sha256 contract holds.")

    # --- W8-g: traversal curtain mirrors the engine reader ---
    # normalizePackagePath (engine/src/vfs/rowlpkg_reader.cpp:191-204)
    # fail-closes on NUL bytes and on a surviving ".." segment. The packer
    # can never emit these names (real files cannot be called ".." or
    # contain NUL), so the fixtures are crafted byte-by-byte below - fully
    # manifest-consistent, so only the traversal curtain can reject them.
    _HEADER = struct.Struct("<4sHIQ")
    _ENTRY = struct.Struct("<QIQQQI")

    def craft_package(package_path, names):
        blobs = [(name, b"w8g-payload:" + name.replace(b"\x00", b"_"))
                 for name in names]
        payload = bytearray(_HEADER.size)
        offsets = {}
        for name, blob in blobs:
            offsets[name] = len(payload)
            payload.extend(blob)
        records = [{
            "compressed_size": len(blob),
            "flags": 0,
            "path": name.decode("utf-8"),
            "sha256": hashlib.sha256(blob).hexdigest(),
            "size": len(blob),
        } for name, blob in blobs]
        records.sort(key=lambda record: record["path"])
        manifest_bytes = (json.dumps({"files": records, "format": 1},
                                     sort_keys=True, separators=(",", ":"))
                          + "\n").encode("utf-8")
        manifest_offset = len(payload)
        payload.extend(manifest_bytes)
        index_offset = len(payload)
        index = bytearray()
        manifest_name = b"rowl/manifest.json"
        for name, blob in blobs + [(manifest_name, manifest_bytes)]:
            offset = offsets.get(name, manifest_offset)
            index.extend(_ENTRY.pack(0, len(name), offset, len(blob),
                                     len(blob), 0))
            index.extend(name)
        _HEADER.pack_into(payload, 0, b"ROWL", 1, len(blobs) + 1, index_offset)
        payload.extend(index)
        pathlib.Path(package_path).write_bytes(bytes(payload))

    def stage_with_package(tag, package_path):
        staged = root / tag
        shutil.copytree(release, staged)
        shutil.copy2(package_path,
                     staged / "Assets" / "packages" / "game.rowlpkg")
        return staged

    graph_name = b"json/full_story_graph.json"
    for tag, evil in [("bare-dotdot", b".."),
                      ("trailing-dotdot", b"sub/.."),
                      ("inner-dotdot", b"a/../b"),
                      ("backslash-dotdot", b"..\\evil"),
                      ("embedded-nul", b"a\x00b")]:
        evil_pkg = root / f"w8g-{tag}.rowlpkg"
        craft_package(evil_pkg, [evil, graph_name])
        require_rejection(stage_with_package(f"w8g-{tag}", evil_pkg),
                          "unsafe or duplicate entry path")

    # False-positive control: dotty but harmless names stay green.
    control_pkg = root / "w8g-control.rowlpkg"
    craft_package(control_pkg, [b"a/..b", b"a/b..", b"...", graph_name])
    control = run_verifier(stage_with_package("w8g-control", control_pkg))
    if control.returncode != 0 or "Valid release" not in control.stdout:
        raise SystemExit("traversal curtain rejected harmless dotty names: "
                         + control.stderr)

    print("[ReleasePackageTests] traversal curtain mirrors the reader "
          "(dot-dot + NUL rejected, dotty names green).")
