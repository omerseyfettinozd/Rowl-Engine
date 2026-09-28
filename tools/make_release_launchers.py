#!/usr/bin/env python3
"""Emit the release launcher(s) for a packaged Rowl desktop release.

Single source of truth for launcher CONTENT. The Linux and Windows
packaging steps in .github/workflows/ci.yml both call this helper instead
of inlining their own copy, so the two platform paths cannot drift.

Why this exists: tools/verify_release_package.py requires a non-empty
run_game.sh OR run_game.bat at the release root. Commit b388a65 ("ship
launchers in linux standalone verify layout") taught only the Linux step
to write launchers; the Windows step wrote neither, so a Windows-shaped
release reached the verifier with no launcher and the gate failed with
"missing release launcher". Duplicating the launcher text into the
Windows YAML step would have re-created exactly the drift this helper is
meant to remove, so the content lives here and both jobs derive it.

Platform selection is explicit rather than inferred from the host so the
same helper can be exercised for both platforms from one machine (the
launcher contract is a CI-parity gate, and a Windows launcher produced
on Linux is what makes that gate testable).
"""

import argparse
import os
import stat
import sys


# POSIX launcher: resolves its own directory so the release tree stays
# relocatable, and exports the release dir on LD_LIBRARY_PATH because
# libRowlEngineCore.so ships next to the player.
POSIX_LAUNCHER_NAME = "run_game.sh"
POSIX_LAUNCHER = (
    "#!/bin/bash\n"
    'SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"\n'
    'export LD_LIBRARY_PATH="$SCRIPT_DIR:$LD_LIBRARY_PATH"\n'
    'exec "$SCRIPT_DIR/RowlGame" "$@"\n'
)

# Windows launcher: cmd.exe reads CRLF here, and `cd /d "%~dp0"` pins the
# working directory to the release root so the .dll files sitting next to
# RowlGame.exe resolve and a relative launch works from any directory.
WINDOWS_LAUNCHER_NAME = "run_game.bat"
WINDOWS_LAUNCHER = (
    "@echo off\r\n"
    'cd /d "%~dp0"\r\n'
    "RowlGame.exe %*\r\n"
)

LAUNCHERS = {
    "posix": ((POSIX_LAUNCHER_NAME, POSIX_LAUNCHER, True),),
    "windows": ((WINDOWS_LAUNCHER_NAME, WINDOWS_LAUNCHER, False),),
    "both": (
        (POSIX_LAUNCHER_NAME, POSIX_LAUNCHER, True),
        (WINDOWS_LAUNCHER_NAME, WINDOWS_LAUNCHER, False),
    ),
}


def host_platform():
    return "windows" if os.name == "nt" else "posix"


def emit(release_root, platform):
    """Write the launcher(s) for `platform` into `release_root`.

    Returns the list of (name, absolute path) written.
    """
    root = os.path.abspath(release_root)
    if not os.path.isdir(root):
        raise ValueError("release root does not exist: " + root)

    written = []
    for name, content, executable in LAUNCHERS[platform]:
        path = os.path.join(root, name)
        # newline="" keeps the literal \r\n inside the Windows launcher
        # from being rewritten to \n by text-mode translation.
        with open(path, "w", encoding="utf-8", newline="") as launcher:
            launcher.write(content)
        if executable:
            mode = os.stat(path).st_mode
            os.chmod(path, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        written.append((name, path))

    for name, path in written:
        print(f"[ReleaseLaunchers] wrote {name} "
              f"({os.path.getsize(path)} bytes) -> {path}")
    return written


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_root")
    parser.add_argument(
        "--platform", default="host",
        choices=["host", "posix", "windows", "both"],
        help="which launcher to emit; 'host' follows the running OS "
             "(default), 'both' emits the portable pair")
    args = parser.parse_args()

    platform = host_platform() if args.platform == "host" else args.platform
    try:
        emit(args.release_root, platform)
    except (OSError, ValueError) as error:
        print("[ReleaseLaunchers] ERROR: " + str(error), file=sys.stderr)
        return 1
    print(f"[ReleaseLaunchers] platform={platform}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
