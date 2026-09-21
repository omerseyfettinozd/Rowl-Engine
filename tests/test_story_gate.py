#!/usr/bin/env python3
"""#69 story-gate structural locks (stdlib only).

Asserts the TU-local claim-gate contract in engine/src/c_api_story.cpp
that a behavior test cannot deterministically pin (a data race only bites
on a losing interleave):
  1. No naked toEngineChecked(handle) claim remains outside the two gate
     helpers — every entry claims through toEngineClaimed (shared) or
     toEngineClaimedForWrite (exclusive), so the claim-to-copy span always
     holds g_storyGate.
  2. The gate exists: TU-local std::shared_mutex g_storyGate plus both
     claim helpers (helpers live before first use; <shared_mutex> included).
  3. Every RowlEngine_* entry body either holds a claim or delegates to
     another RowlEngine_* entry (the three WithLength wrappers) — nothing
     touches the engine claim-free.
  4. Delegators take no lock themselves (a second shared_lock on the same
     thread would self-deadlock); the inner plain getter holds it.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
STORY_CPP = ROOT / "engine" / "src" / "c_api_story.cpp"

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


def main():
    source = read(STORY_CPP)
    if source is None:
        print(f"{len(FAILURES)} story gate(s) violated")
        return 1
    code = strip_comments(source)

    # Gate 1: no naked claims outside the helpers (the two helper bodies
    # legitimately call toEngineChecked under the lock they just took).
    gate_region = code
    for helper_sig in (
        "StoryReadClaim toEngineClaimed(RowlEngineHandle handle) {",
        "StoryWriteClaim toEngineClaimedForWrite(RowlEngineHandle handle) {",
    ):
        start = gate_region.find(helper_sig)
        if start < 0:
            fail(f"claim helper definition missing: '{helper_sig}'")
            continue
        end = gate_region.find("\n}\n", start)
        if end < 0:
            fail(f"claim helper body never closes: '{helper_sig}'")
            continue
        gate_region = gate_region[:start] + gate_region[end + 1 :]
    naked = [
        line.strip()
        for line in gate_region.splitlines()
        if "toEngineChecked(handle)" in line
    ]
    if naked:
        fail(f"{len(naked)} naked toEngineChecked claim(s) bypass the story gate: {naked[0]}")

    # Gate 2: the gate machinery exists and precedes first use.
    if "std::shared_mutex g_storyGate" not in code:
        fail("TU-local g_storyGate (shared_mutex) missing from c_api_story.cpp")
    if "StoryReadClaim toEngineClaimed(" not in code:
        fail("toEngineClaimed (shared) helper missing from c_api_story.cpp")
    if "StoryWriteClaim toEngineClaimedForWrite(" not in code:
        fail("toEngineClaimedForWrite (exclusive) helper missing from c_api_story.cpp")
    if '#include "shared_mutex"' not in code:
        fail('<shared_mutex> include missing from c_api_story.cpp')
    first_helper = code.find("StoryReadClaim toEngineClaimed(")
    first_use = min(
        (code.find("toEngineClaimed(handle)"), code.find("toEngineClaimedForWrite(handle)"))
    )
    if first_helper >= 0 and first_use >= 0 and first_helper > first_use:
        fail("claim helpers are defined after their first use (undeclared at call sites)")

    # Gates 3+4: every entry claims or delegates; delegators take no lock.
    for name, body in entry_bodies(code):
        holds_claim = (
            "toEngineClaimed(handle)" in body or "toEngineClaimedForWrite(handle)" in body
        )
        # Delegation means CALLING another entry: skip the body's own
        # signature line, which trivially names a RowlEngine_* function.
        callee_body = "\n".join(body.splitlines()[1:])
        delegates = re.search(r"\bRowlEngine_\w+\s*\(", callee_body) is not None
        if not holds_claim and not delegates:
            fail(f"{name} touches no story claim and delegates to no entry (gate-free)")
        if delegates and not holds_claim:
            if "g_storyGate" in body or "shared_lock" in body or "unique_lock" in body:
                fail(f"{name} locks and delegates (double shared_lock self-deadlocks)")

    if FAILURES:
        print(f"{len(FAILURES)} story gate(s) violated")
        return 1
    print(
        "OK: story gates green (no naked claims, gate machinery first, "
        "every entry claims-or-delegates, delegators lock-free)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
