#!/usr/bin/env python3
"""D6 #154 curutme kaydi + yazar-sayimi pini (BGM defaults yarisi iddiasi).

HUKUM (2026-09-20, 3'lu oy 2-1): CURUTULDU — kod degisikligi yok, agac temiz.
İddia: `m_defaultBgmTransition[*]` uyelerine engine.cpp:103-108 ile
c_api_story.cpp:423-428 bandindan yarisan yazanlar (BGM defaults yarisi).
Çöküş nedeni: mekanizma doğru (üyeler kilitsiz, engine.hpp) ama ERİŞİLEMEZ —
uretimde yazanlar tek worker-thread sahipliginde siralanir:

  1. story-load yazani (c_api_story.cpp: ~448): boot sirasinda, tek thread.
  2. C API setter (c_api_story.cpp: ~542): `invokeNoexcept` kuyrugu uzerinden
     Step ile ayni tek worker'a siralanir.
  3. resetSessionProfile (engine.cpp: ~2552-2553): owner-gated shutdown
     supurmesi (D2 kilidi, test_lifecycle_shutdown_sweep.cpp).
  4. editor cephesi: EditorSettingsSyncService.cs:80 -> EngineHost.
     SetBgmTransitionDefaults -> InvokeNative -> OffscreenRuntimeWorker.Invoke
     (yine ayni tek worker kuyrugu).

Azınlık şerhi (kayıtta): sahipsiz-handle TOCTOU teoride açık, repo-dışı
host gerekir — bu pin yalnizca repo-ici uretim census'unu kilitler.

PIN SOZLESMESI (bu dosya calisir, derleme gerektirmez, repo'ya yazim yok):
yazar-sayimi bugunku census'a esit kalmalidir. Yeni bir dogrudan yazan
belirirse curutme onermesi kirilir ve test KIRMIZI verir (yeniden-hakemlik
gerekir, sessiz genisleme yok). Satir numaralari degisebilir; sayim +
dosya/icerik eslesmesi kilitlidir.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ENGINE_SRC = ROOT / "engine" / "src"
EDITOR = ROOT / "editor"

FAILURES = []


def check(name, condition, detail=""):
    status = "PASS" if condition else "FAIL"
    print(f"  [{status}] {name}" + (f" — {detail}" if detail and not condition else ""))
    if not condition:
        FAILURES.append(name)


def grep_count(pattern, path, flags=0):
    rx = re.compile(pattern, flags)
    hits = []
    for file in sorted(path.rglob("*")):
        if not file.is_file():
            continue
        if file.suffix not in (".cpp", ".hpp", ".h", ".cs"):
            continue
        if "build" in file.parts:
            continue
        try:
            text = file.read_text(encoding="utf-8", errors="strict")
        except (OSError, UnicodeError):
            continue
        for i, line in enumerate(text.splitlines(), 1):
            if rx.search(line):
                hits.append(f"{file.relative_to(ROOT)}:{i}:{line.strip()}")
    return hits


def main():
    print("D6 #154 curutme kaydi (BGM defaults yarisi: CURUTULDU, census pini)")

    # 1) setBgmTransitionDefaults: 1 tanim + 2 cagri (load + kuyruklu setter).
    calls = grep_count(r"->setBgmTransitionDefaults\s*\(", ENGINE_SRC)
    defs = grep_count(r"void Engine::setBgmTransitionDefaults\s*\(", ENGINE_SRC)
    check("setter cagrisi == 2 (load + kuyruklu C API)",
          len(calls) == 2, f"bulundu={len(calls)}: {calls}")
    check("setter tanimi == 1 (engine.cpp)",
          len(defs) == 1 and "engine/src/core/engine.cpp" in defs[0],
          f"bulundu={defs}")
    check("kuyruklu setter invokeNoexcept ile sirali",
          any("c_api_story.cpp" in h and "invokeNoexcept" in h for h in calls),
          f"bulundu={calls}")

    # 2) Dogrudan uye atamasi: yalniz engine.cpp'de 2+2 (setter govde + reset).
    kind_assign = grep_count(r"m_defaultBgmTransition\s*=", ENGINE_SRC)
    dur_assign = grep_count(r"m_defaultBgmTransitionDurationSeconds\s*=", ENGINE_SRC)
    check("m_defaultBgmTransition atamasi == 2, hepsi engine.cpp",
          len(kind_assign) == 2 and all("engine/src/core/engine.cpp" in h for h in kind_assign),
          f"bulundu={kind_assign}")
    check("m_defaultBgmTransitionDurationSeconds atamasi == 2, hepsi engine.cpp",
          len(dur_assign) == 2 and all("engine/src/core/engine.cpp" in h for h in dur_assign),
          f"bulundu={dur_assign}")

    # 3) Uye okuma/yazma engine cekirdegi disina sizamaz (okurlar ayni TU'da).
    member_all = grep_count(r"m_defaultBgmTransition", ENGINE_SRC)
    outside = [h for h in member_all if "engine/src/core/engine.cpp" not in h]
    check("uye temasi engine.cpp disinda yok",
          len(outside) == 0, f"sizan={outside}")

    # 4) Editor hunisi: P/Invoke bildirimi + kuyruklu wrapper + tek cagiran.
    ed = grep_count(r"SetBgmTransitionDefaults", EDITOR)
    native_bridge = [h for h in ed if "NativeBridge.cs" in h]
    engine_host = [h for h in ed if "EngineHost.cs" in h]
    sync_svc = [h for h in ed if "EditorSettingsSyncService.cs" in h]
    other = [h for h in ed if "NativeBridge.cs" not in h and "EngineHost.cs" not in h
             and "EditorSettingsSyncService.cs" not in h]
    check("editor hunisi 3 dosya (NativeBridge + EngineHost + SyncService)",
          len(native_bridge) >= 1 and len(engine_host) >= 1 and len(sync_svc) >= 1,
          f"bulundu={ed}")
    check("editor'da huni-disi yazan yok",
          len(other) == 0, f"sizan={other}")
    host_text = (EDITOR / "Src" / "Native" / "EngineHost.cs").read_text(encoding="utf-8")
    check("EngineHost wrapper InvokeNative kuyruguna girer",
          "InvokeNative(handle => NativeBridge.RowlEngine_SetBgmTransitionDefaults" in host_text)

    # 5) resetSessionProfile fabrika-degeri pinler (D2 supurme ile ayni sozlesme).
    reset_text = (ENGINE_SRC / "core" / "engine.cpp").read_text(encoding="utf-8")
    reset_fn = reset_text.split("void Engine::resetSessionProfile()")[1].split("\n}\n")[0]
    check("reset fabrika-degerine dondurur (instant + 1.0f)",
          'm_defaultBgmTransition = "instant"' in reset_fn
          and "m_defaultBgmTransitionDurationSeconds = 1.0f" in reset_fn)

    if FAILURES:
        print(f"\nCURUTME-PINI KIRMIZI ({len(FAILURES)}): {FAILURES}")
        print("Yazar census degisti -> #154 yeniden-hakemlik gerekir (sessiz gecme yok).")
        return 1
    print("\nCURUTME-PINI YESIL: #154 CURUTULDU hukmu gecerli (census birebir).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
