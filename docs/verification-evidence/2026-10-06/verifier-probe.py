"""Reproduce T1 in temporary files; execute from anywhere in this checkout."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="rowl-verifier-recheck-") as directory:
    release = Path(directory)
    package = release / "Assets/packages/game.rowlpkg"
    package.parent.mkdir(parents=True)
    subprocess.run([sys.executable, str(ROOT / "tools/package_assets.py"),
                    str(ROOT / "Assets"), str(package)],
                   check=True, stdout=subprocess.DEVNULL)
    (release / "mods").mkdir()
    (release / "mods/README.md").write_text("mods", encoding="utf-8")
    (release / "README.txt").write_text("release", encoding="utf-8")
    shutil.copy2(ROOT / "packaging/THIRD_PARTY_NOTICES.md",
                 release / "THIRD_PARTY_NOTICES.md")
    (release / "RowlGame").write_bytes(b"player")
    (release / "libRowlEngineCore.so").write_bytes(b"runtime")
    (release / "run_game.sh").write_bytes(b'#!/bin/sh\nexec ./RowlGame "$@"\n')

    def verify(label):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/verify_release_package.py"), str(release)],
            capture_output=True, text=True, timeout=30)
        print(label, "exit=", result.returncode)
        print(result.stdout.strip(), result.stderr.strip())

    verify("control_fixture")
    (release / "RowlGame").write_bytes(b"")
    (release / "libRowlEngineCore.so").unlink()
    (release / "libRowlEngineCore.so").mkdir()
    verify("empty_player_and_runtime_directory")
