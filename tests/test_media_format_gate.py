#!/usr/bin/env python3
"""MVP media format gate tests for tools/package_assets.py.

Faz 5 Dilim 5 gate: PNG/JPEG/BMP/TGA, WAV/OGG and TTF/OTF pack normally;
MP3, FLAC and WebP are accept-via-conversion (they pack without a
[converter-required] rejection — the import lane converts them to .ogg/.png
and converted outputs carry a converted_from manifest record from their
adjacent .rowlconv.json sidecar). Genuinely unsupported media (GIF et al.)
still fails with [unsupported-media-format]. Rejected runs publish no
output file.

The Python sets must mirror the single C# capability table
(editor/Services/MediaFormatCatalog.cs); the CONTRACT blocks are parsed
from that file and compared set-for-set, so either side drifting breaks
this test.
"""

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

try:
    import zstandard as zstd
    HAS_ZSTD = True
except ImportError:
    HAS_ZSTD = False


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


def find_tool(name):
    """Built converter tool for the gate fixture (both build trees)."""
    candidates = [ROOT / "build" / "bin" / name,
                  ROOT / "build-text-fallback" / "bin" / name,
                  ROOT / "build" / "bin" / (name + ".exe")]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit(f"converter tool missing for gate test: {name}")


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

# 3. Converter sources are accept-via-conversion: raw sources and converted
# outputs both pack (any case), with no [converter-required] rejection.
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
        target.write_bytes(b"not-yet-converted-source")
        output = root / "converted.rowlpkg"
        if output.exists():
            output.unlink()
        result = pack(case, output)
        if result.returncode != 0 or "[converter-required]" in result.stderr:
            raise SystemExit(f"converter sources must pack (accept-via-conversion): "
                             f"exit={result.returncode}, stderr={result.stderr!r}")
        if not output.is_file() or output.stat().st_size == 0:
            raise SystemExit("packer published no output for converter sources")


def read_manifest(package_path):
    """Extract and parse the embedded rowl/manifest.json from a .rowlpkg."""
    data = pathlib.Path(package_path).read_bytes()
    magic, version, file_count, index_offset = struct.unpack_from("<4sHIQ", data, 0)
    if magic != b"ROWL" or version != 1:
        raise SystemExit(f"unexpected package header: {magic!r} v{version}")
    offset = index_offset
    manifest_blob = None
    for _ in range(file_count):
        path_hash, path_len, entry_offset, comp_size, uncomp_size, flags = struct.unpack_from(
            "<QIQQQI", data, offset)
        offset += struct.calcsize("<QIQQQI")
        path = data[offset:offset + path_len].decode("utf-8")
        offset += path_len
        if path == "rowl/manifest.json":
            blob = data[entry_offset:entry_offset + comp_size]
            if flags == 1:
                if not HAS_ZSTD:
                    raise SystemExit("manifest is zstd-compressed but zstandard is missing")
                blob = zstd.ZstdDecompressor().decompress(blob)
            manifest_blob = blob
    if manifest_blob is None:
        raise SystemExit("package has no embedded manifest")
    return json.loads(manifest_blob.decode("utf-8"))


# 3b. Converted outputs with an adjacent sidecar carry converted_from
# (source path + hash); sidecar-less outputs pack as normal assets.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    fixture = root / "conv"
    (fixture / "audio").mkdir(parents=True)
    (fixture / "images").mkdir(parents=True)
    (fixture / "images" / "hero.png").write_bytes(b"converted-png-bytes")
    (fixture / "audio" / "plain.ogg").write_bytes(b"plain-ogg-no-sidecar")
    # Gerçek araç sidecar'ı (sentetik değil): derlenmiş rowl_oggenc koşulur.
    # Tool pratikte source_path yazmaz — C# hattı sonradan damgalar
    # (MediaConverterService.StampSourcePath); burada aynası uygulanır.
    oggenc = find_tool("rowl_oggenc")
    pcm = root / "silence.pcm"
    pcm.write_bytes(b"\x00" * (44100 * 2 * 2 // 10))
    music_ogg = fixture / "audio" / "music.ogg"
    music_sidecar = fixture / "audio" / "music.ogg.rowlconv.json"
    proc = subprocess.run([str(oggenc), "--rate", "44100", "--channels", "2",
                           "-o", str(music_ogg), "--sidecar", str(music_sidecar),
                           str(pcm)],
                          capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise SystemExit(f"rowl_oggenc failed on gate fixture: {proc.stderr}")
    real_sidecar = json.loads(music_sidecar.read_text(encoding="utf-8"))
    if "source_path" in real_sidecar:
        raise SystemExit("tool sidecar must not carry source_path (C# stamps it)")
    if real_sidecar.get("output_sha256") != hashlib.sha256(music_ogg.read_bytes()).hexdigest():
        raise SystemExit("tool sidecar output_sha256 must match the produced .ogg")
    real_sidecar["source_path"] = "sfx/theme.mp3"
    music_sidecar.write_text(json.dumps(real_sidecar), encoding="utf-8")
    output = root / "conv.rowlpkg"
    result = pack(fixture, output)
    if result.returncode != 0:
        raise SystemExit(f"packer failed on converted outputs: {result.stderr}")
    manifest = read_manifest(output)
    by_path = {entry["path"]: entry for entry in manifest["files"]}
    # The sidecar itself packs as a normal asset (provenance ships).
    if "audio/music.ogg.rowlconv.json" not in by_path:
        raise SystemExit("sidecar file itself must pack as a normal asset")
    converted = by_path.get("audio/music.ogg")
    if converted is None or "converted_from" not in converted:
        raise SystemExit("converted output must carry a converted_from manifest record")
    if (converted["converted_from"].get("path") != "sfx/theme.mp3"
            or converted["converted_from"].get("source_sha256") != real_sidecar["source_sha256"]):
        raise SystemExit(f"converted_from must hold source path + hash: {converted['converted_from']!r}")
    plain = by_path.get("audio/plain.ogg")
    if plain is None or "converted_from" in plain:
        raise SystemExit("sidecar-less output must pack as a normal asset (no converted_from)")
    # Corrupt sidecars never break packing (fail-open here, fail-closed in the linter).
    (fixture / "images" / "hero.png.rowlconv.json").write_text("{not json", encoding="utf-8")
    output2 = root / "conv2.rowlpkg"
    result = pack(fixture, output2)
    if result.returncode != 0:
        raise SystemExit(f"corrupt sidecar must not break packing: {result.stderr}")

# 4. Other known-but-unsupported media is rejected with its own code.
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    unsupported = root / "unsupported"
    (unsupported / "images").mkdir(parents=True)
    (unsupported / "audio").mkdir(parents=True)
    (unsupported / "images" / "fun.gif").write_bytes(b"gif")
    require_rejection(unsupported, "unsupported-media-format")

print("[MediaFormatGate] C#/Python contract mirrors match; MVP packs, MP3/FLAC/WebP convert-accept with converted_from, GIF rejects.")
