#!/usr/bin/env python3
"""Faz 5 Dilim 5 converter tool tests: rowl_oggenc + rowl_webp2png.

Usage: test_converters.py <rowl_oggenc> <rowl_webp2png>

Stdlib-only (plus ffmpeg/gcc/pkg-config/vorbisfile system tools): runs on
any interpreter, including minimal build venvs without numpy/PIL.

Covers the MEDIA_CONVERTERS_CONTRACT matrix:
 - determinism: WAV->PCM->OGG twice -> SHA-256 equal (file + stdin input);
   WebP->PNG twice -> equal; vendor string + serial/version stability.
 - decode path: MP3/FLAC sources decode through the documented external
   commands (ffmpeg / flac, call-only, never linked).
 - round-trip: OGG output opens with vorbisfile (the same library behind
   OggStreamSource) and decodes near the source (lossy-tolerant correlation
   + duration probe, never bit-identical); PNG output opens with the repo's
   stb_image and matches the WebP pixels exactly.
 - hygiene: no ffmpeg CLI string in encoder code; PNG carries no
   tIME/tEXt/pHYs/iCCP chunks.
"""

import hashlib
import json
import math
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

OGGENC = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None
WEBP2PNG = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else None

VENDOR = "RowlEngine rowl_oggenc 1.0.0"
RATE = 44100
CHANNELS = 2
SECONDS = 2


def fail(message):
    raise SystemExit(f"[Converters] FAIL: {message}")


def run(*args, input_bytes=None):
    proc = subprocess.run(list(args), input=input_bytes, capture_output=True)
    if proc.returncode != 0:
        fail(f"{args[0]} exit={proc.returncode}: {proc.stderr.decode(errors='replace')[:500]}")
    return proc


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write_rgba_png(path, width, height, rows):
    """Minimal deterministic PNG writer (filter 0, zlib 6): test fixture only."""
    raw = b"".join(b"\x00" + bytes(row) for row in rows)

    def chunk(ctype, data):
        return (struct.pack(">I", len(data)) + ctype + data +
                struct.pack(">I", zlib.crc32(ctype + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    pathlib.Path(path).write_bytes(png)


def ffmpeg_raw_rgba(path):
    proc = subprocess.run(
        ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
         "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
        capture_output=True)
    if proc.returncode != 0:
        fail(f"ffmpeg raw decode failed for {path}")
    return proc.stdout


def mean(values):
    return sum(values) / len(values)


def correlation(a, b):
    ma, mb = mean(a), mean(b)
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va == 0 or vb == 0:
        fail("correlation of silent signal")
    return cov / math.sqrt(va * vb)


if OGGENC is None or WEBP2PNG is None:
    fail("usage: test_converters.py <rowl_oggenc> <rowl_webp2png>")
for tool in (OGGENC, WEBP2PNG):
    if not tool.is_file():
        fail(f"tool not found: {tool}")
if shutil.which("ffmpeg") is None:
    fail("ffmpeg is required on PATH for fixture generation")

directory = pathlib.Path(tempfile.mkdtemp(prefix="rowl-converters-"))

# Tur-7: every section announces itself with flush=True so a TIMEOUT kill
# (unbuffered via ctest -u) leaves the exact hang point in the log instead
# of a zero-output kill. Prints change no assertion.
print("[Converters] fixture: sine WAV + PCM", flush=True)

# 1. Fixture: stereo sine WAV + raw PCM (the documented decode input shape).
frames = []
for i in range(RATE * SECONDS):
    s = int(0.5 * math.sin(2 * math.pi * 440 * i / RATE) * 32767)
    frames.append(struct.pack("<hh", s, s))
pcm_bytes = b"".join(frames)
wav_path = directory / "tone.wav"
with wave.open(str(wav_path), "wb") as wav:
    wav.setnchannels(CHANNELS)
    wav.setsampwidth(2)
    wav.setframerate(RATE)
    wav.writeframes(pcm_bytes)
pcm_path = directory / "tone.pcm"
run("ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav_path),
    "-ar", str(RATE), "-ac", str(CHANNELS), "-sample_fmt", "s16", "-f", "s16le", str(pcm_path))
if pcm_path.read_bytes() != pcm_bytes:
    fail("PCM fixture round-trip through ffmpeg changed bytes")

# 2. Documented MP3/FLAC decode path (external process, call-only).
#    Decodes through the exact contract command shape (stdout `-` form);
#    each source lands in its own file, and every call is -nostdin/-y so
#    no overwrite prompt can ever block the suite.
print("[Converters] MP3/FLAC decode path start", flush=True)
mp3_path = directory / "tone.mp3"
flac_path = directory / "tone.flac"
run("ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
    "-i", str(wav_path), str(mp3_path))
run("ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
    "-i", str(wav_path), str(flac_path))
for src in (mp3_path, flac_path):
    out = directory / (src.stem + "_" + src.suffix.lstrip(".") + ".decoded.pcm")
    proc = subprocess.run(
        ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
         "-i", str(src), "-ar", str(RATE), "-ac", str(CHANNELS),
         "-sample_fmt", "s16", "-f", "s16le", "-"],
        capture_output=True)
    if proc.returncode != 0:
        fail(f"documented decode failed for {src.suffix}")
    out.write_bytes(proc.stdout)
    if abs(len(out.read_bytes()) - len(pcm_bytes)) > len(pcm_bytes) // 20:
        fail(f"decoded {src.suffix} length drifts >5%")
print("[Converters] MP3/FLAC documented decode path OK")

# 3. OGG determinism: file input twice + stdin once -> identical bytes + sidecars.
print("[Converters] OGG encode start", flush=True)
ogg_a = directory / "a.ogg"
ogg_b = directory / "b.ogg"
ogg_c = directory / "c.ogg"
run(OGGENC, "--rate", str(RATE), "--channels", str(CHANNELS),
    "-o", str(ogg_a), "--sidecar", str(directory / "a.ogg.rowlconv.json"), str(pcm_path))
run(OGGENC, "--rate", str(RATE), "--channels", str(CHANNELS),
    "-o", str(ogg_b), "--sidecar", str(directory / "b.ogg.rowlconv.json"), str(pcm_path))
stdin_proc = subprocess.run(
    [str(OGGENC), "--rate", str(RATE), "--channels", str(CHANNELS), "-o", str(ogg_c), "-"],
    input=pcm_bytes, capture_output=True)
if stdin_proc.returncode != 0:
    fail("oggenc stdin input failed")
if not (sha(ogg_a) == sha(ogg_b) == sha(ogg_c)):
    fail("OGG determinism broken: repeated encodes differ")
side_a = json.loads((directory / "a.ogg.rowlconv.json").read_text())
side_b = json.loads((directory / "b.ogg.rowlconv.json").read_text())
if side_a != side_b:
    fail("OGG sidecars differ between identical runs")
if side_a["converter_name"] != "rowl_oggenc" or side_a["converter_version"] != "1.0.0":
    fail("OGG sidecar name/version mismatch")
if side_a["settings"] != {"quality_q": 4, "sample_rate_hz": RATE, "channels": CHANNELS,
                          "serial": side_a["settings"]["serial"]}:
    fail("OGG sidecar settings mismatch")
if side_a["source_sha256"] != hashlib.sha256(pcm_bytes).hexdigest():
    fail("OGG sidecar source_sha256 is not the PCM digest")
if side_a["output_sha256"] != sha(ogg_a):
    fail("OGG sidecar output_sha256 mismatch")
expected_serial = struct.unpack(">I", hashlib.sha256(pcm_bytes).digest()[:4])[0]
if side_a["settings"]["serial"] != expected_serial:
    fail("OGG serial is not big-endian SHA-256[0..3]")
if side_a["created_by"] != "rowl_oggenc 1.0.0":
    fail("OGG sidecar created_by mismatch")
strings = shutil.which("strings")
if strings:
    out = subprocess.run([strings, str(OGGENC)], capture_output=True, text=True)
    if VENDOR not in out.stdout:
        fail("vendor string missing from rowl_oggenc binary")
version = subprocess.run([str(OGGENC), "--version"], capture_output=True, text=True)
if version.stdout.strip() != VENDOR:
    fail("--version must print the fixed vendor string")
print(f"[Converters] OGG determinism OK ({sha(ogg_a)[:16]}…, serial={expected_serial})")

# 4. OGG round-trip: vorbisfile decode (same lib behind OggStreamSource) +
#    duration + lossy-tolerant correlation probe (never bit-identical).
print("[Converters] OGG round-trip checker compile start", flush=True)
checker_src = directory / "ogg_check.cpp"
checker_src.write_text(r"""
#include <vorbis/vorbisfile.h>
#include <cstdio>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 1;
    OggVorbis_File vf;
    if (ov_open_callbacks(f, &vf, nullptr, 0, OV_CALLBACKS_DEFAULT) < 0) return 1;
    vorbis_info* vi = ov_info(&vf, -1);
    long total = (long)ov_pcm_total(&vf, -1);
    printf("%d %d %ld\n", vi->rate, vi->channels, total);
    FILE* out = fopen(argv[2], "wb");
    char buf[8192];
    int section = 0;
    long got = 0;
    while (got < total) {
        long n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &section);
        if (n <= 0) break;
        fwrite(buf, 1, (size_t)n, out);
        got += n / (2 * vi->channels);
    }
    fclose(out);
    ov_clear(&vf);
    return got == total ? 0 : 1;
}
""")
checker = directory / ("ogg_check.exe" if sys.platform == "win32" else "ogg_check")
cflags = subprocess.run(["pkg-config", "--cflags", "vorbisfile"], capture_output=True, text=True)
libs = subprocess.run(["pkg-config", "--libs", "vorbisfile"], capture_output=True, text=True)
compile_proc = subprocess.run(
    ["g++", "-O1", "-o", str(checker), str(checker_src)] + cflags.stdout.split() + libs.stdout.split(),
    capture_output=True, text=True)
if compile_proc.returncode != 0:
    fail(f"vorbisfile round-trip checker did not compile: {compile_proc.stderr[:400]}")
decoded_pcm = directory / "roundtrip.pcm"
info = run(checker, str(ogg_a), str(decoded_pcm)).stdout.decode().split()
if int(info[0]) != RATE or int(info[1]) != CHANNELS:
    fail(f"vorbisfile reports {info}, expected {RATE} Hz / {CHANNELS} ch")
if abs(int(info[2]) - RATE * SECONDS) > RATE * SECONDS // 100:
    fail("vorbisfile duration drifts >1%")
ref = struct.unpack("<%dh" % (len(pcm_bytes) // 2), pcm_bytes)
got = struct.unpack("<%dh" % (len(decoded_pcm.read_bytes()) // 2), decoded_pcm.read_bytes())
count = min(len(ref), len(got))
margin = RATE * CHANNELS // 4
ref_mid = list(ref[margin:count - margin:7])
got_mid = list(got[margin:count - margin:7])
corr = correlation(ref_mid, got_mid)
rms = lambda v: math.sqrt(sum(x * x for x in v) / len(v))
rms_ratio = rms(got_mid) / rms(ref_mid)
if corr < 0.90 or not (0.5 < rms_ratio < 2.0):
    fail(f"OGG round-trip too far from source (corr={corr:.3f}, rms_ratio={rms_ratio:.3f})")
probe = subprocess.run(["ffprobe", "-hide_banner", "-loglevel", "error",
                        "-show_entries", "stream=codec_name", "-of", "csv=p=0", str(ogg_a)],
                       capture_output=True, text=True)
if probe.stdout.strip() != "vorbis":
    fail("ffprobe does not see a vorbis stream")
print(f"[Converters] OGG round-trip OK (vorbisfile decode, corr={corr:.4f})")

# 5. PNG determinism: gradient fixture -> lossless WebP -> convert twice -> identical.
print("[Converters] PNG webp2png start", flush=True)
width, height = 96, 64
rows = [[((x * 3) % 256, (y * 5) % 256, ((x + y) * 2) % 256, ((x * y) % 256))
         for x in range(width)] for y in range(height)]
flat_rows = [[c for px in row for c in px] for row in rows]
write_rgba_png(directory / "src.png", width, height, flat_rows)
webp_path = directory / "src.webp"
run("ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
    "-i", str(directory / "src.png"), "-c:v", "libwebp", "-lossless", "1", str(webp_path))
png_a = directory / "a.png"
png_b = directory / "b.png"
run(WEBP2PNG, "-o", str(png_a), "--sidecar", str(directory / "a.png.rowlconv.json"), str(webp_path))
run(WEBP2PNG, "-o", str(png_b), "--sidecar", str(directory / "b.png.rowlconv.json"), str(webp_path))
if sha(png_a) != sha(png_b):
    fail("PNG determinism broken: repeated converts differ")
png_side = json.loads((directory / "a.png.rowlconv.json").read_text())
if png_side["converter_name"] != "rowl_webp2png" or png_side["converter_version"] != "1.0.0":
    fail("PNG sidecar name/version mismatch")
if png_side["settings"] != {"png_writer": "libpng", "dpi": None}:
    fail("PNG sidecar settings mismatch (want libpng writer, no dpi)")
if png_side["source_sha256"] != sha(webp_path) or png_side["output_sha256"] != sha(png_a):
    fail("PNG sidecar digest mismatch")
if png_side["created_by"] != "rowl_webp2png 1.0.0":
    fail("PNG sidecar created_by mismatch")

# PNG chunk hygiene: only IHDR / IDAT* / IEND (no tIME, tEXt, pHYs, iCCP).
raw = png_a.read_bytes()
if raw[:8] != b"\x89PNG\r\n\x1a\n":
    fail("PNG magic broken")
pos, chunks = 8, []
while pos < len(raw):
    (length,) = struct.unpack(">I", raw[pos:pos + 4])
    chunks.append(raw[pos + 4:pos + 8].decode("latin1"))
    pos += 12 + length
if chunks[0] != "IHDR" or chunks[-1] != "IEND" or any(
        c not in ("IHDR", "IDAT", "IEND") for c in chunks):
    fail(f"PNG carries non-deterministic chunks: {chunks}")

# PNG pixel equality vs the WebP source (ffmpeg raw decode) + stb_image proof.
if ffmpeg_raw_rgba(webp_path) != ffmpeg_raw_rgba(png_a):
    fail("PNG pixels differ from WebP source decode")
stb_src = directory / "stb_check.c"
stb_src.write_text(r"""
#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#include <stdio.h>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(argv[1], &w, &h, &n, 4);
    if (!px) return 1;
    printf("%d %d %d\n", w, h, n);
    stbi_image_free(px);
    return 0;
}
""")
stb_bin = directory / "stb_check"
stb_proc = subprocess.run(
    ["gcc", "-O1", "-o", str(stb_bin), str(stb_src),
     "-I", str(ROOT / "engine" / "include"), "-lm"], capture_output=True, text=True)
if stb_proc.returncode != 0:
    fail(f"stb_image checker did not compile: {stb_proc.stderr[:400]}")
stb_out = run(stb_bin, str(png_a)).stdout.decode().split()
if int(stb_out[0]) != width or int(stb_out[1]) != height:
    fail(f"stb_image reports {stb_out[0]}x{stb_out[1]}, want {width}x{height}")
print(f"[Converters] PNG determinism + stb_image round-trip OK ({sha(png_a)[:16]}…)")

# 6. Hygiene: encoder sources must not shell out to ffmpeg (decode-only rule).
#    Comments may document the rule; code must not reference the binary.
import re


def strip_c_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


for source in (ROOT / "tools" / "rowl_oggenc.c", ROOT / "tools" / "rowl_webp2png.c"):
    code = strip_c_comments(source.read_text())
    if "ffmpeg" in code.lower():
        fail(f"{source.name} references ffmpeg in code (CLI encode path is banned)")
    if re.search(r"\b(system|popen|exec[lv]|fork)\s*\(", code):
        fail(f"{source.name} spawns subprocesses (tools must be self-contained)")
print("[Converters] no ffmpeg CLI in encoder sources")

shutil.rmtree(directory, ignore_errors=True)
print("[Converters] ALL CONVERTER TOOL TESTS PASSED")
