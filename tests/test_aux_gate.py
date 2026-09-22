#!/usr/bin/env python3
"""D13 aux-gate structural locks (stdlib only).

test_story_gate.py deseninde; story -> aux yayilimini ve kilit-sirasini
kilitler (davranis testinin deterministik yakalayamadigi yapi):
  1. Aux kilit bolunmesi: g_prefetchMutex / g_characterMutex TU-local
     std::shared_mutex (<shared_mutex> include'lu, eski `std::mutex g_*`
     yok). Yazicilar unique_lock, okuyucular shared_lock; RowlEngine_*
     govdelerinde ciplak lock_guard(aux) yok.
  2. Hayalet-entry yasagi: okuyucu govdelerinde `g_*States[` (operator[])
     yok (find + yerel bos-runtime); yazicilarda serbest.
  3. Kilit-sirasi: aux/storyGate/VideoSerial DISTA, g_handleMutex ICTE.
     prefetch/character TU'larinda `g_handleMutex` adi gecmez; lifecycle'da
     aux kilit adlari gecmez. Destroy/Reclaim/Shutdown/Init govdesinde
     VideoSerialGuard ilk handle-kilit ediniminden ONCE gelir; Run'da
     serial YOK (by-design), Step'te claim-or-reject VAR + serial YOK.
  4. HOIST YOK: g_storyGate yalniz story TU'da; clear* ilanlari hpp'de,
     Destroy cagirir (D2 hatti korunur).
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PREFETCH_CPP = ROOT / "engine" / "src" / "c_api_prefetch_chapters.cpp"
CHARACTER_CPP = ROOT / "engine" / "src" / "c_api_character_layers.cpp"
LIFECYCLE_CPP = ROOT / "engine" / "src" / "c_api_lifecycle.cpp"
STORY_CPP = ROOT / "engine" / "src" / "c_api_story.cpp"
INTERNAL_HPP = ROOT / "engine" / "src" / "c_api_internal.hpp"

FAILURES = []

PREFETCH_WRITERS = {
    "RowlEngine_LoadChapterIndexJson",
    "RowlEngine_AppendChapterFileJson",
    "RowlEngine_LoadChapter",
    "RowlEngine_UnloadChapter",
    "RowlEngine_PrefetchChapterAssets",
    "RowlEngine_PumpPrefetch",
}
PREFETCH_READERS = {
    "RowlEngine_GetLoadedChaptersJson",
    "RowlEngine_IsChapterBoundaryNode",
    "RowlEngine_GetPrefetchProgressJson",
}
CHARACTER_WRITERS = {
    "RowlEngine_SetCharacterSlotAsset",
    "RowlEngine_SetCharacterSlotOpacity",
    "RowlEngine_SetCharacterSlotVisible",
    "RowlEngine_RegisterCharacterPreset",
    "RowlEngine_ApplyCharacterExpression",
    # Anlamsal okuyucu ama composeDrawList cagri-basi skip-cache yazar
    # (mutable) -> exclusive sinifta (D13 istisnasi).
    "RowlEngine_GetCharacterDrawListJson",
}
CHARACTER_READERS = {
    "RowlEngine_GetCharacterSlotAssetUtf8",
    "RowlEngine_GetCharacterSlotOpacity",
    "RowlEngine_IsCharacterSlotVisible",
    "RowlEngine_GetCharacterPresetListJson",
    "RowlEngine_GetLastCharacterErrorUtf8",
}

SERIAL_FIRST_ENTRIES = {
    "RowlEngine_Destroy",
    "RowlEngine_ReclaimHandle",
    "RowlEngine_Init",
    "RowlEngine_InitStandalone",
    "RowlEngine_Shutdown",
}
HANDLE_LOCK_TOKENS = (
    "g_handleMutex",
    "classifyHandle",
    "claimHandleThread",
    "claimHandleOrClassify",
    "takeLiveHandle",
    "toEngineChecked",
    "copyEngineAnyThread",
    "isLiveHandle",
    "unclaimHandleThread",
)


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


def entry_bodies(source):
    """Split top-level RowlEngine_* definitions into (name, body) pairs.

    Entry bodies close with a column-0 '}' (namespace closes carry a
    trailing comment, so they never match).
    """
    bodies = []
    current_name = None
    current_lines = []
    for line in source.splitlines():
        match = re.match(r"^(?:[\w:\*&<>\s]+? )?(\bRowlEngine_\w+)\s*\(", line)
        if match and current_name is None:
            current_name = match.group(1)
            current_lines = [line]
        elif current_name is not None:
            current_lines.append(line)
            if line == "}":
                bodies.append((current_name, "\n".join(current_lines)))
                current_name = None
                current_lines = []
    return bodies


def check_aux_split(path, mutex, writers, readers, map_name):
    source = read(path)
    if source is None:
        return
    code = strip_comments(source)
    label = path.name
    if f"std::shared_mutex {mutex}" not in code:
        fail(f"{label}: TU-local shared_mutex '{mutex}' missing")
    if re.search(rf"std::mutex\s+{mutex}\b", code):
        fail(f"{label}: legacy exclusive `std::mutex {mutex}` still present")
    if "#include <shared_mutex>" not in code and '#include "shared_mutex"' not in code:
        fail(f"{label}: <shared_mutex> include missing")
    bodies = dict(entry_bodies(code))
    if not bodies:
        fail(f"{label}: no RowlEngine_* entries parsed")
    for name, body in bodies.items():
        exclusive = f"unique_lock<std::shared_mutex> lock({mutex})" in body
        shared = f"shared_lock<std::shared_mutex> lock({mutex})" in body
        if name in writers:
            if not exclusive or shared:
                fail(f"{label}:{name} must take {mutex} exclusive "
                     f"(unique={exclusive}, shared={shared})")
        elif name in readers:
            if not shared or exclusive:
                fail(f"{label}:{name} must take {mutex} shared "
                     f"(unique={exclusive}, shared={shared})")
            if f"{map_name}[" in body:
                fail(f"{label}:{name} inserts via operator[] "
                     f"({map_name}[...] in a shared reader: ghost entry)")
        else:
            fail(f"{label}:{name} unclassified (not in D13 reader/writer sets)")
        if "lock_guard" in body:
            fail(f"{label}:{name} uses lock_guard on the aux mutex "
                 f"(read/write split bypassed)")
    for want in writers | readers:
        if want not in bodies:
            fail(f"{label}: expected entry '{want}' not found")


def check_lock_order():
    source = read(LIFECYCLE_CPP)
    if source is None:
        return
    code = strip_comments(source)
    for aux in ("g_prefetchMutex", "g_characterMutex", "g_storyGate"):
        if aux in code:
            fail(f"c_api_lifecycle.cpp names '{aux}' "
                 f"(handle>aux nesting risk; auxiliaries stay TU-local)")
    bodies = dict(entry_bodies(code))
    for name in SERIAL_FIRST_ENTRIES:
        body = bodies.get(name)
        if body is None:
            fail(f"c_api_lifecycle.cpp: expected entry '{name}' not found")
            continue
        serial_pos = body.find("VideoSerialGuard")
        if serial_pos < 0:
            fail(f"{name} takes no VideoSerialGuard (serial>handle order lost)")
            continue
        lock_positions = [p for t in HANDLE_LOCK_TOKENS
                          if (p := body.find(t)) >= 0]
        if lock_positions and not serial_pos < min(lock_positions):
            fail(f"{name} acquires a handle lock before VideoSerialGuard "
                 f"(inverted nesting)")
    run = bodies.get("RowlEngine_Run")
    if run is None:
        fail("c_api_lifecycle.cpp: RowlEngine_Run not found")
    else:
        if "VideoSerialGuard" in run:
            fail("RowlEngine_Run holds VideoSerialGuard "
                 "(blocking loop would deadlock Shutdown; by-design serial-free)")
        if "isLiveHandle" not in run:
            fail("RowlEngine_Run lost its liveness guard")
    step = bodies.get("RowlEngine_Step")
    if step is None:
        fail("c_api_lifecycle.cpp: RowlEngine_Step not found")
    else:
        if "VideoSerialGuard" in step:
            fail("RowlEngine_Step holds VideoSerialGuard (must stay serial-free)")
        if "claimHandleOrClassify" not in step:
            fail("RowlEngine_Step lost claim-or-reject")
    destroy = bodies.get("RowlEngine_Destroy")
    if destroy is not None:
        for clear in ("clearPrefetchStatesForHandle(handle)",
                      "clearCharacterStatesForHandle(handle)"):
            if clear not in destroy:
                fail(f"RowlEngine_Destroy no longer calls {clear} (D2 hygiene lost)")


def check_no_hoist():
    for path, mutex in ((PREFETCH_CPP, "g_prefetchMutex"),
                        (CHARACTER_CPP, "g_characterMutex")):
        source = read(path)
        if source is None:
            continue
        code = strip_comments(source)
        if "g_handleMutex" in code:
            fail(f"{path.name} names g_handleMutex "
                 f"(handle lock only via classify/toEngineChecked: aux>handle)")
        if "g_storyGate" in code:
            fail(f"{path.name} names g_storyGate (HOIST YOK: gate stays TU-local)")
    story = read(STORY_CPP)
    if story is not None:
        code = strip_comments(story)
        if "std::shared_mutex g_storyGate" not in code:
            fail("c_api_story.cpp lost TU-local g_storyGate (D13 must not move it)")
        for aux in ("g_prefetchMutex", "g_characterMutex"):
            if aux in code:
                fail(f"c_api_story.cpp names '{aux}' (aux gates stay TU-local)")
    hpp = read(INTERNAL_HPP)
    if hpp is not None:
        code = strip_comments(hpp)
        for decl in ("clearPrefetchStatesForHandle", "clearCharacterStatesForHandle"):
            if decl not in code:
                fail(f"c_api_internal.hpp lost clear ilan '{decl}'")


def main():
    check_aux_split(PREFETCH_CPP, "g_prefetchMutex",
                    PREFETCH_WRITERS, PREFETCH_READERS, "g_prefetchStates")
    check_aux_split(CHARACTER_CPP, "g_characterMutex",
                    CHARACTER_WRITERS, CHARACTER_READERS, "g_characterStates")
    check_lock_order()
    check_no_hoist()
    if FAILURES:
        print(f"{len(FAILURES)} aux gate(s) violated")
        return 1
    print("OK: aux gates green (read/write split, no ghost insert, "
          "serial>handle order, no hoist)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
