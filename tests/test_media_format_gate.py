#!/usr/bin/env python3
"""MVP media format gate tests for tools/package_assets.py.

Faz 1 Dilim 1 gate: PNG/JPEG/BMP/TGA, WAV/OGG and TTF/OTF pack normally,
while MP3, FLAC and WebP fail fast with a structured [converter-required]
error (until the Faz 5 converter lands) and other known-but-unsupported
media fails with [unsupported-media-format]. Rejected runs publish no
output file.

The Python sets must mirror the single C# capability table
(editor/Services/MediaFormatCatalog.cs); the CONTRACT blocks are parsed
from that file and compared set-for-set, so either side drifting breaks
this test.
"""

import pathlib
import re
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
CS_CATALOG = ROOT / "editor" / "Services" / "MediaFormatCatalog.cs"

sys.path.insert(0, str(ROOT / "tools"))
import package_assets as packager


# CONTRACT tag in MediaFormatCatalog.cs -> attribute on the packager module.
CONTRACT_MAP = {
    "accepted-image": "ACCEPTED_IMAGE_EXTS",
    "accepted-audio": "ACCEPTED_AUDIO_EXTS",
    "accepted-font": "ACCEPTED_FONT_EXTS",
    "converter-pending": "CONVERTER_PENDING_EXTS",
    "known-unsupported": "KNOWN_UNSUPPORTED_MEDIA_EXTS",
}


def parse_cs_block(source, tag):
    """Collect the quoted extensions of one // CONTRACT(tag) { ... }; block."""
    lines = source.splitlines()
    collecting = False
    found = set()
    for line in lines:
        if not collecting:
            if f"// CONTRACT({tag})" in line:
                collecting = True
            continue
        found.update(re.findall(r'"(\.[a-z0-9]+)"', line))
        if "};" in line:
            return found
    raise SystemExit(f"C# catalog is missing a parseable CONTRACT({tag}) block")


def pack(source, output):
    return subprocess.run([sys.executable, str(PACKAGER), str(source), str(output)],
                          capture_output=True, text=True, check=False)


def require_rejection(source, code, output_name="rejected.rowlpkg"):
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / output_name
        result = pack(source, output)
        if result.returncode != 2 or f"[{code}]" not in result.stderr:
            raise SystemExit(f"expected packer rejection [{code}]: "
                             f"exit={result.returncode}, stderr={result.stderr!r}")
        if output.exists():
            raise SystemExit("rejected pack run must not publish an output file")


# 1. The Python mirror must equal the single C# capability table, set for set.
cs_source = CS_CATALOG.read_text(encoding="utf-8")
for tag, attribute in CONTRACT_MAP.items():
    cs_set = parse_cs_block(cs_source, tag)
    py_set = set(getattr(packager, attribute))
    if cs_set != py_set:
        raise SystemExit(f"contract drift for {tag}: C#={sorted(cs_set)} python={sorted(py_set)}")
if not cs_set:
    raise SystemExit("contract parse produced no extensions")

# 2. Every accepted MVP format packs normally.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    fixture = root / "accepted"
    (fixture / "images").mkdir(parents=True)
    (fixture / "audio").mkdir(parents=True)
    (fixture / "fonts").mkdir(parents=True)
    (fixture / "json").mkdir(parents=True)
    accepted_files = [
        "images/hero.png", "images/photo.jpg", "images/photo2.jpeg",
        "images/old.bmp", "images/frame.tga",
        "audio/tone.wav", "audio/music.ogg",
        "fonts/body.ttf", "fonts/head.otf",
        "json/story.json", "notes.txt",
    ]
    for name in accepted_files:
        (fixture / name).write_bytes(b"payload:" + name.encode("utf-8"))
    output = root / "accepted.rowlpkg"
    result = pack(fixture, output)
    if result.returncode != 0:
        raise SystemExit(f"packer failed on accepted MVP formats: {result.stderr}")
    if not output.is_file() or output.stat().st_size == 0:
        raise SystemExit("packer published no output for accepted MVP formats")

# 3. Converter-pending formats are rejected with an explicit code (any case).
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    for name in ("audio/theme.mp3", "audio/voice.flac", "images/hero.webp",
                 "audio/LOUD.MP3", "images/PHOTO.WEBP"):
        case = root / "case"
        if case.exists():
            for child in sorted(case.rglob("*")):
                if child.is_file():
                    child.unlink()
        else:
            case.mkdir()
        target = case / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"not-yet-supported")
        require_rejection(case, "converter-required")

# 4. Other known-but-unsupported media is rejected with its own code.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    unsupported = root / "unsupported"
    (unsupported / "images").mkdir(parents=True)
    (unsupported / "audio").mkdir(parents=True)
    (unsupported / "images" / "fun.gif").write_bytes(b"gif")
    require_rejection(unsupported, "unsupported-media-format")

print("[MediaFormatGate] C#/Python contract mirrors match; MVP packs, MP3/FLAC/WebP/GIF reject.")
