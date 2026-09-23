#!/usr/bin/env python3
"""W8-c bulgu-4 kilidi: dinamik tabloda RowlEngine_-disi T sembol export yok.

Kilitlenen sozlesme: paylasilan libRowlEngineCore yalnizca public C ABI'yi
(`RowlEngine_` onekli) export eder. Internal C++ helper'lari (5
`Rowl::Audio::record*Locked` + 4 `Rowl::Platform::RowlCrash_*`)
-fvisibility=hidden ile gizlidir; internal basliklarda ROWL_API yoktur.

Prob: `nm -D --defined-only --format=posix` ciktisinda tipi T olup adi
`RowlEngine_` ile baslamayan sembol sayisi 0 olmalidir (bulgu-1 dersi:
bu prob kok CMakeLists.txt'e kayitlidir).

Derlenmis kutuphane bulunamazsa SKIP (exit 0). Repo'ya yazim yok.
"""

import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ABI_PREFIX = "RowlEngine_"


def find_built_runtime():
    """Derlenmis libRowlEngineCore.so arar.

    W8-c fix-turu: ctest kaydi ROWL_LIB_DIR ile derlenen agacin lib
    dizinini verir (bayat build/ golgelemesi yok); env yoksa eski
    build-taramasina duser.
    """
    env_dir = os.environ.get("ROWL_LIB_DIR")
    if env_dir:
        for pattern in ("libRowlEngineCore.so", "libRowlEngineCore.dylib",
                        "RowlEngineCore.dll"):
            hit = pathlib.Path(env_dir) / pattern
            if hit.is_file():
                return str(hit)
        return None
    patterns = ("libRowlEngineCore.so", "libRowlEngineCore.dylib",
                "RowlEngineCore.dll")
    search_roots = [ROOT / "build", ROOT]
    search_roots += sorted(ROOT.glob("build-*"))
    for root in search_roots:
        if not root.is_dir():
            continue
        for pattern in patterns:
            for hit in root.rglob(pattern):
                if hit.is_file():
                    return str(hit)
    return None


def main():
    runtime = find_built_runtime()
    if runtime is None:
        print("[NoInternalExports][SKIP] derlenmis RowlEngineCore yok")
        return 0
    if shutil.which("nm") is None:
        print("[NoInternalExports][SKIP] nm arac yok")
        return 0
    proc = subprocess.run(
        ["nm", "-D", "--defined-only", "--format=posix", runtime],
        capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        print("[NoInternalExports][FAIL] nm calisamadi: "
              f"{proc.stderr.strip()[:200]}")
        return 1
    internal = []
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        name, kind = parts[0], parts[1]
        if kind == "T" and not name.startswith(ABI_PREFIX):
            internal.append(name)
    if internal:
        print(f"[NoInternalExports][FAIL] {len(internal)} internal T sembol "
              "hala exportlu:")
        for name in sorted(internal):
            print(f"  T {name}")
        return 1
    print("[NoInternalExports][PASS] RowlEngine_-disi T sembol yok "
          f"({runtime})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
