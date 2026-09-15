#!/usr/bin/env python3
"""Faz 4.5 Dilim 4 mobile-safe desktop hardening gates (stdlib only).

Asserts the locked structural contracts that behavior tests cannot pin:
  1. window.cpp initializeEmbedded selects the platform with an explicit
     '#elif defined(__linux__)' X11 branch plus a fail-closed '#else'
     (log + lease/release cleanup + return false) — never a naked '#else'
     that silently assumes X11.
  2. PlatformHost exposes no pure-virtual ('= 0') members.
  3. Touch input is single-path: the dead processSdlEvent switch is absent.
  4. The Wayland rejection error path exists (diagnostic, not crash/silent).
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


def fail(message):
    FAILURES.append(message)
    print(f"FAIL: {message}")


def read(path):
    if not path.is_file():
        fail(f"expected source file missing: '{path}'")
        return None
    return path.read_text(encoding="utf-8")


def strip_comments(source):
    """Remove // and /* */ comments so gates judge code, not prose about it."""
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return "\n".join(line.split("//", 1)[0] for line in source.splitlines())


def embedded_region(window_source):
    start = window_source.find("bool Window::initializeEmbedded")
    if start < 0:
        fail("Window::initializeEmbedded not found in window.cpp")
        return ""
    end = window_source.find("\nvoid Window::reloadFonts", start)
    if end < 0:
        fail("Window::reloadFonts boundary not found after initializeEmbedded")
        return ""
    return window_source[start:end]


def main():
    window_source = read(WINDOW_CPP)
    host_header = read(PLATFORM_HOST_HPP)
    touch_cpp = read(MOBILE_INPUT_CPP)
    touch_hpp = read(MOBILE_INPUT_HPP)
    if window_source is None or host_header is None or touch_cpp is None or touch_hpp is None:
        raise SystemExit(1)

    region = embedded_region(window_source)
    # Gates judge comment-stripped code so prose about a construct can
    # neither satisfy a gate spuriously nor trip one.
    code = strip_comments(region)

    # Gate 1: explicit Linux X11 branch, no naked '#else'-assumes-X11.
    # The elif may use any standard spelling (defined(__linux__),
    # defined (__linux__), bare __linux__) but must name the platform.
    if not re.search(r"#\s*elif\b[^\n]*__linux__", code):
        fail("initializeEmbedded lacks an '#elif ... __linux__' X11 path")
    else_lines = [i for i, line in enumerate(region.splitlines())
                  if line.strip() == "#else"]
    if not else_lines:
        fail("initializeEmbedded lacks a fail-closed final '#else' branch")
    for number in else_lines:
        following = region.splitlines()[number + 1:number + 6]
        code_following = [strip_comments(line) for line in following]
        if any(("X11_WINDOW" in line or "XID" in line) for line in code_following):
            fail("naked '#else'-assumes-X11 found in the window platform-selection region")
        branch = "\n".join(region.splitlines()[number:number + 25])
        if "return false" not in branch:
            fail("final '#else' branch does not fail closed with 'return false'")
    if "SdlSubsystemLease::release" not in region:
        fail("initializeEmbedded rejection paths do not release the SDL video lease")

    # Gate 2: PlatformHost has no pure-virtual members. The check is limited
    # to 'virtual ... = 0;' declarations so default member initializers such
    # as 'uint32_t width = 0;' cannot trip it; comments are stripped so prose
    # about the removed specifier cannot trip it either.
    host_code = strip_comments(host_header)
    if re.search(r"virtual\b[^;{]*=\s*0\s*;", host_code):
        fail("PlatformHost declares pure-virtual ('= 0') members")

    # Gate 3: touch is single-path — the dead switch is gone from code
    # (comments may still name it to document the deletion).
    for name, source in (("mobile_input.cpp", touch_cpp),
                         ("mobile_input.hpp", touch_hpp),
                         ("window.cpp", window_source)):
        if "processSdlEvent" in strip_comments(source):
            fail(f"dead touch path 'processSdlEvent' still present in {name}")
    if "classifyTouchGesture" not in (touch_cpp + touch_hpp):
        fail("single live touch path 'classifyTouchGesture' missing from mobile_input")

    # Gate 4: Wayland is rejected with a diagnostic, not a crash or silence.
    # Judged on comment-stripped code: a leftover comment saying
    # "unsupported" must not pass without the real driver-query block.
    lowered = code.lower()
    if "wayland" not in lowered:
        fail("initializeEmbedded has no Wayland rejection path")
    elif "unsupported" not in lowered:
        fail("Wayland rejection path carries no 'unsupported' diagnostic")
    if "SDL_GetCurrentVideoDriver" not in code:
        fail("Wayland rejection lacks the SDL_GetCurrentVideoDriver query")

    if FAILURES:
        raise SystemExit(f"{len(FAILURES)} platform gate(s) violated")
    print("OK: platform gates green (x11-elif, fail-closed else, "
          "no pure-virtual host, single touch path, wayland rejection)")


if __name__ == "__main__":
    sys.exit(main())
