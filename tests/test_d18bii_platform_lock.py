#!/usr/bin/env python3
"""D18b-ii R4 — platform-lock probu (test_platform_gates.py klonu).

Mevcut tests/test_platform_gates.py DEGISTIRILMEZ; 7 kapi buradaki
string-tabanli klonla fixture kopyalari uzerinde kilitlenir:
  GREEN  gercek repo kaynaklari -> 0 ihlal (exit 0).
  RED-a  fixture kopyaya processSdlEvent switch'i geri eklenir -> yalniz G3
         duser, diger 6 yesil (izolasyon).
  RED-b  fixture host header'a `virtual foo()=0;` eklenir -> yalniz G2 duser.
  RED-c  fixture TARGET_OS_IPHONE blogundaki `return false` silinir -> G6 duser.

Kapi mantigi orijinalle birebir aynidir (yorum-strip judging dahil);
tamper'lar bellek-ici string kopyalarda, repo kaynagina dokunulmaz.
Cikti: her prob PASS/FAIL satiri; herhangi biri duserse exit 1.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WINDOW_CPP = ROOT / "engine" / "src" / "render" / "window.cpp"
PLATFORM_HOST_HPP = ROOT / "engine" / "include" / "rowl" / "platform" / "platform_host.hpp"
MOBILE_INPUT_CPP = ROOT / "engine" / "src" / "platform" / "mobile_input.cpp"
MOBILE_INPUT_HPP = ROOT / "engine" / "include" / "rowl" / "platform" / "mobile_input.hpp"

FAILURES = []


def check(name, condition, detail):
    print(f"[D18b-ii][{'PASS' if condition else 'FAIL'}] {name}"
          + ("" if condition else f": {detail}"))
    if not condition:
        FAILURES.append(name)


def strip_comments(source):
    """Remove // and /* */ comments so gates judge code, not prose about it."""
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return "\n".join(line.split("//", 1)[0] for line in source.splitlines())


def embedded_region(window_source):
    start = window_source.find("bool Window::initializeEmbedded")
    if start < 0:
        return None
    end = window_source.find("\nvoid Window::reloadFonts", start)
    if end < 0:
        return None
    return window_source[start:end]


def run_gates(window_source, host_header, touch_cpp, touch_hpp):
    """Orijinal 7 kapinin string-tabanli klonu; ihlal listesi doner."""
    failures = []
    region = embedded_region(window_source)
    if region is None:
        return ["Window::initializeEmbedded/reloadFonts boundary missing"]
    code = strip_comments(region)

    # G1: explicit Linux X11 branch + fail-closed final else (+lease release).
    if not re.search(r"#\s*elif\b[^\n]*__linux__", code):
        failures.append("G1: initializeEmbedded lacks an '#elif ... __linux__' X11 path")
    else_lines = [i for i, line in enumerate(region.splitlines())
                  if line.strip() == "#else"]
    if not else_lines:
        failures.append("G1: initializeEmbedded lacks a fail-closed final '#else' branch")
    for number in else_lines:
        following = region.splitlines()[number + 1:number + 6]
        code_following = [strip_comments(line) for line in following]
        if any(("X11_WINDOW" in line or "XID" in line) for line in code_following):
            failures.append("G1: naked '#else'-assumes-X11 found")
        branch = "\n".join(region.splitlines()[number:number + 25])
        if "return false" not in branch:
            failures.append("G1: final '#else' branch does not fail closed")
    if "SdlSubsystemLease::release" not in region:
        failures.append("G1: rejection paths do not release the SDL video lease")

    # G2: no pure-virtual host members.
    host_code = strip_comments(host_header)
    if re.search(r"virtual\b[^;{]*=\s*0\s*;", host_code):
        failures.append("G2: PlatformHost declares pure-virtual ('= 0') members")

    # G3: single touch path.
    for name, source in (("mobile_input.cpp", touch_cpp),
                         ("mobile_input.hpp", touch_hpp),
                         ("window.cpp", window_source)):
        if "processSdlEvent" in strip_comments(source):
            failures.append(f"G3: dead touch path 'processSdlEvent' still present in {name}")
    if "classifyTouchGesture" not in (touch_cpp + touch_hpp):
        failures.append("G3: single live touch path 'classifyTouchGesture' missing")

    # G4: Wayland diagnostic rejection.
    lowered = code.lower()
    if "wayland" not in lowered:
        failures.append("G4: initializeEmbedded has no Wayland rejection path")
    elif "unsupported" not in lowered:
        failures.append("G4: Wayland rejection path carries no 'unsupported' diagnostic")
    if "SDL_GetCurrentVideoDriver" not in code:
        failures.append("G4: Wayland rejection lacks the SDL_GetCurrentVideoDriver query")

    # G5: macOS NSView* view-pointer contract.
    if "COCOA_VIEW_POINTER" not in code:
        failures.append("G5: macOS embed does not feed COCOA_VIEW_POINTER")
    if "COCOA_WINDOW_POINTER" in code:
        failures.append("G5: COCOA_WINDOW_POINTER still fed in initializeEmbedded")

    # G6: iOS fail-closed split.
    if "TARGET_OS_IPHONE" not in region:
        failures.append("G6: Apple embed branch has no TARGET_OS_IPHONE split")
    else:
        lines = code.splitlines()
        guard = next((i for i, line in enumerate(lines)
                      if "TARGET_OS_IPHONE" in line), None)
        if guard is None:
            failures.append("G6: TARGET_OS_IPHONE guard vanished after stripping")
        elif "return false" not in "\n".join(lines[guard:guard + 14]):
            failures.append("G6: iOS embed path does not fail closed")

    # G7: IME enablement.
    window_code = strip_comments(window_source)
    if "SDL_StartTextInput" not in window_code:
        failures.append("G7: SDL_StartTextInput missing")
    if "SDL_StopTextInput" not in window_code:
        failures.append("G7: SDL_StopTextInput missing")
    if "wantsTextInput" not in host_code:
        failures.append("G7: PlatformHost offers no wantsTextInput()")

    return failures


def gate_ids(failures):
    return sorted({item.split(":")[0] for item in failures})


def main():
    window_source = WINDOW_CPP.read_text(encoding="utf-8")
    host_header = PLATFORM_HOST_HPP.read_text(encoding="utf-8")
    touch_cpp = MOBILE_INPUT_CPP.read_text(encoding="utf-8")
    touch_hpp = MOBILE_INPUT_HPP.read_text(encoding="utf-8")

    # GREEN: gercek kaynaklar temiz.
    green = run_gates(window_source, host_header, touch_cpp, touch_hpp)
    check("GREEN-repo-sources-clean", not green, f"ihlal: {green}")

    # RED-a: olu switch geri eklenir -> yalniz G3.
    tampered_touch = (touch_cpp
                      + "\n// D18b-ii fixture-tamper (bellek-ici, repo'ya yazilmaz)\n"
                        "void processSdlEvent(int type) { switch (type) { case 1: break; } }\n")
    red_a = run_gates(window_source, host_header, tampered_touch, touch_hpp)
    check("RED-a-processSdlEvent-caught",
          any(item.startswith("G3:") for item in red_a),
          "geri eklenen switch G3'u dusuremedi")
    check("RED-a-isolated-to-G3", gate_ids(red_a) == ["G3"],
          f"izolasyon bozuk, dusen kapilar: {gate_ids(red_a)}")

    # RED-b: host'a pure-virtual eklenir -> yalniz G2.
    tampered_host = (host_header
                     + "\n// D18b-ii fixture-tamper (bellek-ici)\n"
                       "virtual void fixtureProbe() = 0;\n")
    red_b = run_gates(window_source, tampered_host, touch_cpp, touch_hpp)
    check("RED-b-pure-virtual-caught",
          any(item.startswith("G2:") for item in red_b),
          "'= 0' uyesi G2'yi dusuremedi")
    check("RED-b-isolated-to-G2", gate_ids(red_b) == ["G2"],
          f"izolasyon bozuk, dusen kapilar: {gate_ids(red_b)}")

    # RED-c: iOS blogundaki `return false`lar silinir -> G6 duser.
    # Guard orijinal kapidaki gibi yorum-strip edilmis kodda aranir (ham
    # satirlardaki yorumda gecen makro adi yaniltmamalidir); strip satir
    # sayisini korudugundan indeksler ham satira birebir eslenir.
    lines = window_source.splitlines(keepends=True)
    stripped = strip_comments(window_source).splitlines()
    guard_index = next((i for i, line in enumerate(stripped)
                        if "TARGET_OS_IPHONE" in line), None)
    red_c = None
    if guard_index is not None:
        drop = {j for j in range(guard_index, min(guard_index + 14, len(lines)))
                if "return false" in stripped[j]}
        if drop:
            red_c = run_gates("".join(line for i, line in enumerate(lines)
                                      if i not in drop),
                              host_header, touch_cpp, touch_hpp)
    check("RED-c-ios-fail-closed-caught",
          red_c is not None and any(item.startswith("G6:") for item in red_c),
          "silinen 'return false' G6'yi dusuremedi")

    if FAILURES:
        raise SystemExit(f"{len(FAILURES)} platform-lock probu dustu")
    print("OK: platform lock green (repo-clean, RED-a/G3, RED-b/G2, RED-c/G6)")


if __name__ == "__main__":
    sys.exit(main())
