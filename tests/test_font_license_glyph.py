#!/usr/bin/env python3
"""Faz 3 Dilim 5 — font license & glyph coverage gate.

Scans every project font (Assets/fonts, samples/*/Assets/fonts) and checks:

1. License integrity: the name table carries a license description
   (name ID 13) mentioning OFL/SIL, or a LICENSE*/OFL* sibling file
   exists next to the font. Otherwise a WARNING diagnostic is printed.
2. Glyph coverage: every character actually used in the shipped locale
   catalogs (samples/*/Assets/locales/*.json, Assets/locales/*.json)
   plus the locale alphabets required by the project manifests
   (project.rowlproj supported_locales: tr adds c C g G Turkish
   extras) must exist in each project font. Missing codepoints print a
   constructive WARNING naming the character, its codepoint and where it
   is used.

Exit status: 1 on ERROR (no fonts found, corrupt/unreadable font);
0 with WARNING lines otherwise. Missing fontTools degrades to SKIP
(exit 0) so developer machines without it stay green.
"""

import glob
import hashlib
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TURKISH_EXTRAS = "çÇğĞıIiİöÖşŞüÜâÂêÊîÎôÔûÛ"
TYPOGRAPHIC_PUNCT = "—–…\"\"''•§©®™°±×÷"
RTL_ADVISORY = "אבגשםلاکی"
CJK_ADVISORY = "日本語한국어中文"

LICENSE_KEYWORDS = ("open font license", "ofl", "sil")


def warn(message):
    print(f"WARNING: {message}")


def error(message):
    print(f"ERROR: {message}")


def find_fonts():
    patterns = [
        os.path.join(REPO, "Assets", "fonts", "*.ttf"),
        os.path.join(REPO, "Assets", "fonts", "*.otf"),
        os.path.join(REPO, "samples", "*", "Assets", "fonts", "*.ttf"),
        os.path.join(REPO, "samples", "*", "Assets", "fonts", "*.otf"),
    ]
    found = []
    for pattern in patterns:
        found.extend(sorted(glob.glob(pattern)))
    return found


def content_hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def required_locales():
    locales = set()
    for manifest in glob.glob(os.path.join(REPO, "project.rowlproj")) + \
            glob.glob(os.path.join(REPO, "samples", "*", "project.rowlproj")):
        try:
            with open(manifest, encoding="utf-8") as stream:
                data = json.load(stream)
        except (OSError, ValueError):
            continue
        for key in ("supported_locales", "supportedLocales"):
            value = data.get(key)
            if isinstance(value, list):
                for entry in value:
                    if isinstance(entry, str) and entry.strip():
                        locales.add(entry.strip().lower().split("-")[0].split("_")[0])
    return locales or {"en"}


def catalog_characters():
    """Map codepoint -> set of catalog files using it (speaker/text/alt)."""
    usage = {}
    for catalog in glob.glob(os.path.join(REPO, "samples", "*", "Assets", "locales", "*.json")) + \
            glob.glob(os.path.join(REPO, "Assets", "locales", "*.json")):
        try:
            with open(catalog, encoding="utf-8") as stream:
                data = json.load(stream)
        except (OSError, ValueError):
            continue
        entries = data.get("entries")
        if not isinstance(entries, dict):
            continue
        for entry in entries.values():
            if not isinstance(entry, dict):
                continue
            for field in ("speaker", "text", "alt_text"):
                text = entry.get(field)
                if not isinstance(text, str):
                    continue
                for char in text:
                    if char.isspace() or ord(char) < 0x20:
                        continue
                    usage.setdefault(ord(char), set()).add(os.path.basename(catalog))
    return usage


def check_license(font, name_table, directory):
    try:
        license_text = str(name_table.getDebugName(13) or "")
        license_url = str(name_table.getDebugName(14) or "")
    except Exception:
        license_text, license_url = "", ""
    blob = (license_text + " " + license_url).lower()
    if any(keyword in blob for keyword in LICENSE_KEYWORDS):
        return True
    for sibling in os.listdir(directory):
        lowered = sibling.lower()
        if lowered.startswith("license") or "ofl" in lowered:
            return True
    warn(f"{font}: no OFL/SIL license statement in the name table and no "
         f"LICENSE sibling; ship one to keep redistribution safe.")
    return False


def main():
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        print("SKIP: fontTools is not installed; font gate not evaluated.")
        return 0

    fonts = find_fonts()
    if not fonts:
        error("no project fonts found under Assets/fonts or samples/*/Assets/fonts.")
        return 1

    locales = required_locales()
    usage = catalog_characters()
    required = set(TYPOGRAPHIC_PUNCT)
    if "tr" in locales:
        required.update(TURKISH_EXTRAS)

    seen_hashes = {}
    failures = 0
    for path in fonts:
        rel = os.path.relpath(path, REPO)
        digest = content_hash(path)
        if digest in seen_hashes:
            print(f"INFO: {rel} is byte-identical to {seen_hashes[digest]}; checked once.")
            continue
        seen_hashes[digest] = rel
        try:
            font = TTFont(path, lazy=True)
            cmap = font.getBestCmap()
            family = font["name"].getDebugName(1)
            font.close()
        except Exception as exc:
            error(f"{rel} could not be parsed ({exc}).")
            failures += 1
            continue

        print(f"INFO: {rel} family='{family}' glyphs={len(cmap)}.")
        # Re-open cheaply for the name table (closed above releases lazy data).
        try:
            probe = TTFont(path, lazy=True)
            check_license(rel, probe["name"], os.path.dirname(path))
            probe.close()
        except Exception as exc:
            warn(f"{rel}: license record unreadable ({exc}).")

        missing_required = sorted(cp for cp in (ord(c) for c in required) if cp not in cmap)
        for codepoint in missing_required:
            warn(f"{rel}: U+{codepoint:04X} '{chr(codepoint)}' required by "
                 f"locales {sorted(locales)} has no glyph; "
                 f"translate around it or bundle an extended font.")
        missing_used = sorted(cp for cp in usage if cp not in cmap)
        for codepoint in missing_used:
            users = sorted(usage[codepoint])
            warn(f"{rel}: U+{codepoint:04X} '{chr(codepoint)}' used by "
                 f"{', '.join(users)} renders as .notdef; add a fallback glyph.")

        advisory = [c for c in RTL_ADVISORY + CJK_ADVISORY if ord(c) not in cmap]
        if advisory and locales & {"ar", "he", "fa", "ur", "ja", "zh", "ko"}:
            warn(f"{rel}: {len(advisory)} RTL/CJK codepoints required by "
                 f"locales {sorted(locales)} are missing.")

    if failures:
        return 1
    print("OK: font license/glyph gate evaluated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
