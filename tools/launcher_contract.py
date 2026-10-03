#!/usr/bin/env python3
"""The release-launcher contract, in one place, checkable from anywhere.

Why this module exists (P2-16): every gate that touched the launchers before
checked only their NAMES and, at most, that the .bat used CRLF. Nothing
asserted that the .bat actually pins its working directory to the release root
(`cd /d "%~dp0"`) or that it invokes the player executable that the release
actually ships. Deleting the `cd /d "%~dp0"` line and corrupting the exe name
left `check_release_launcher_parity.py`, `verify_release_package.py` and
`test_release_package.py` all green — the bug class this module closes.

Two tiers, because the two consumers have different reach:

  FULL    — the shape a SHIPPED launcher must have. Enforced by
            check_release_launcher_parity.py (the gate that runs on Linux and
            therefore stands in for the Windows packaging job) and by
            verify_release_package.py for run_game.bat.
  MINIMAL — the shape the release verifier accepts for run_game.sh. The POSIX
            launcher degrades gracefully when invoked from inside the release
            directory, so the verifier only demands that it is a script that
            execs the player with "$@". The relocatable form (SCRIPT_DIR +
            LD_LIBRARY_PATH) is what the helper emits and what FULL demands.

`player_names` is the set of executable basenames the release verifier accepts
for the standalone player. The launchers must name one of them: a launcher that
invokes `RowlGameX.exe` is broken even though every name-based check passes.

Every function returns a list of human-readable problem strings. An empty list
means the contract holds. Nothing in here raises on a contract violation —
callers decide how loud to be.
"""

import re


POSIX_LAUNCHER_NAME = "run_game.sh"
WINDOWS_LAUNCHER_NAME = "run_game.bat"

POSIX_PLAYER_NAME = "RowlGame"
WINDOWS_PLAYER_NAME = "RowlGame.exe"

# `cd /d "%~dp0"` — the working-directory pin. `/d` also changes drive, which a
# bare `cd` does not; `%~dp0` is the batch file's own directory with a trailing
# backslash. cmd.exe has no equivalent of the shell's implicit "start where the
# script lives", so without this line `RowlGame.exe` is looked up in whatever
# directory the user happened to be in and the .dll files next to the exe fail
# to resolve.
WINDOWS_CD_LINE = 'cd /d "%~dp0"'
WINDOWS_ECHO_LINE = "@echo off"

# Structural line splitting is lenient (any of the three endings) so a
# CRLF violation is reported once, by crlf_problems, instead of producing a
# pile of downstream "line not found" noise.
_ANY_LINE_SPLIT = re.compile(r"\r\n|\n|\r")
_WINDOWS_EXEC_LINE = re.compile(r'^"?([^\s"/\\]+\.exe)"?\s+%\*$', re.IGNORECASE)
_POSIX_SCRIPT_DIR = re.compile(
    r"""SCRIPT_DIR=["']?\$\(\s*cd\s+"\$\(\s*dirname\s+"\$\{\s*BASH_SOURCE\[0\]\s*\}""",
    re.IGNORECASE,
)
_POSIX_SCRIPT_DIR_DOLLAR_ZERO = re.compile(
    r"""SCRIPT_DIR=["']?\$\(\s*cd\s+"\$\(\s*dirname\s+"\$0""",
    re.IGNORECASE,
)
_POSIX_LD_LIBRARY_PATH = re.compile(r"^\s*export\s+LD_LIBRARY_PATH=", re.MULTILINE)
_POSIX_EXEC = re.compile(
    r"""^\s*exec\s+(?P<command>"[^"]*"|'[^']*'|\S+)(?P<arguments>\s+.*)?$""",
    re.MULTILINE)


def _normalize_space(line):
    return " ".join(line.split())


def _player_basename(token):
    """Strip a directory part, surrounding quotes and a trailing `.exe`."""
    name = token.strip().strip('"').strip("'")
    name = name.replace("\\", "/").rsplit("/", 1)[-1]
    if name.lower().endswith(".exe"):
        name = name[: -len(".exe")]
    return name


def crlf_problems(text, label):
    """cmd.exe is the consumer: every line ending must be CRLF.

    A lone LF survives a text-mode write here, and a bare CR is worse than
    both, so both directions are checked.
    """
    problems = []
    if "\r\n" not in text:
        problems.append(f"{label} has no CRLF line ending")
    remainder = text.replace("\r\n", "")
    if "\n" in remainder:
        problems.append(f"{label} has a bare LF line ending")
    if "\r" in remainder:
        problems.append(f"{label} has a bare CR line ending")
    return problems


def windows_launcher_problems(text, player_names):
    """FULL-tier contract for run_game.bat."""
    problems = []
    if not text.strip():
        return [f"{WINDOWS_LAUNCHER_NAME} is empty"]

    problems.extend(crlf_problems(text, WINDOWS_LAUNCHER_NAME))
    lines = _ANY_LINE_SPLIT.split(text)
    normalized = [_normalize_space(line) for line in lines]

    if not normalized or normalized[0].lower() != WINDOWS_ECHO_LINE:
        problems.append(
            f"{WINDOWS_LAUNCHER_NAME} must start with {WINDOWS_ECHO_LINE!r}; "
            f"found {lines[0]!r}")

    wanted = _normalize_space(WINDOWS_CD_LINE)
    cd_indices = [index for index, line in enumerate(normalized)
                  if line.lower() == wanted.lower()]
    if not cd_indices:
        problems.append(
            f"{WINDOWS_LAUNCHER_NAME} is missing the working-directory pin "
            f"{WINDOWS_CD_LINE!r}; cmd.exe would look for RowlGame.exe in the "
            f"caller's directory and the DLLs next to the exe would not resolve")
    cd_index = cd_indices[0] if cd_indices else None

    exec_indices = [index for index, line in enumerate(lines)
                    if _WINDOWS_EXEC_LINE.match(_normalize_space(line))]
    if not exec_indices:
        problems.append(
            f"{WINDOWS_LAUNCHER_NAME} has no line that runs the player with %*; "
            f"expected e.g. {WINDOWS_PLAYER_NAME} %*")
    else:
        index = exec_indices[0]
        executable = _WINDOWS_EXEC_LINE.match(
            _normalize_space(lines[index])).group(1)
        if _player_basename(executable) not in player_names:
            problems.append(
                f"{WINDOWS_LAUNCHER_NAME} invokes {executable!r}, which is not a "
                f"player executable the release verifier accepts "
                f"({sorted(player_names)})")
        if cd_index is not None and index < cd_index:
            problems.append(
                f"{WINDOWS_LAUNCHER_NAME} runs the player before the "
                f"{WINDOWS_CD_LINE!r} pin")
    return problems


def posix_launcher_problems(text, player_names, full=True):
    """FULL (or MINIMAL) contract for run_game.sh."""
    problems = []
    if not text.strip():
        return [f"{POSIX_LAUNCHER_NAME} is empty"]
    if "\r" in text:
        problems.append(f"{POSIX_LAUNCHER_NAME} has a CR; /bin/sh will not run it")
    if not text.startswith("#!"):
        problems.append(f"{POSIX_LAUNCHER_NAME} has no shebang line")

    if full:
        if not (_POSIX_SCRIPT_DIR.search(text) or
                _POSIX_SCRIPT_DIR_DOLLAR_ZERO.search(text)):
            problems.append(
                f"{POSIX_LAUNCHER_NAME} does not resolve its own directory into "
                f"SCRIPT_DIR; a relocated or double-clicked launcher would run "
                f"from the caller's directory")
        if not _POSIX_LD_LIBRARY_PATH.search(text) or "SCRIPT_DIR" not in text:
            problems.append(
                f"{POSIX_LAUNCHER_NAME} does not put the release directory on "
                f"LD_LIBRARY_PATH; libRowlEngineCore.so ships next to the player")

    found_player = False
    for match in _POSIX_EXEC.finditer(text):
        arguments = match.group("arguments") or ""
        if '"$@"' not in arguments and '"$*"' not in arguments:
            continue
        command = match.group("command")
        found_player = True
        if _player_basename(command) not in player_names:
            problems.append(
                f"{POSIX_LAUNCHER_NAME} execs {command!r}, which is not a "
                f"player executable the release verifier accepts "
                f"({sorted(player_names)})")
        break
    if not found_player:
        problems.append(
            f"{POSIX_LAUNCHER_NAME} has no exec line that runs the player and "
            f"forwards \"$@\"")
    return problems


def launcher_problems(name, text, player_names, full=True):
    """Dispatch on the launcher file name. Unknown names are not checked."""
    if name == WINDOWS_LAUNCHER_NAME:
        return windows_launcher_problems(text, player_names)
    if name == POSIX_LAUNCHER_NAME:
        return posix_launcher_problems(text, player_names, full=full)
    return []


# ---------------------------------------------------------------------------
# The producer/consumer name contract.
# ---------------------------------------------------------------------------

# These are the names the release verifier accepts, defined HERE so the
# verifier, the launcher producer and the parity gate all read one list. It
# used to be scraped out of the verifier's source with a regex; that regex
# matched whichever `for name in (...)` came first in the file, so adding an
# unrelated loop silently repointed the name contract at the wrong tuple.
PLAYER_NAMES = frozenset({"RowlGame", "RowlGame.exe"})
LAUNCHER_NAMES = frozenset({POSIX_LAUNCHER_NAME, WINDOWS_LAUNCHER_NAME})


def player_basenames():
    """Player names without the .exe suffix, as the contract compares them."""
    return {name[:-len(".exe")] if name.lower().endswith(".exe") else name
            for name in PLAYER_NAMES}