#!/usr/bin/env python3
"""Determinism and validation contract tests for tools/package_assets.py.

Job 21 gate: packing the same source tree twice (even with different file
mtimes) must yield byte-identical archives with equal SHA-256 digests, the
archive must carry a verifiable embedded manifest, and invalid inputs
(missing, dangling, zero-byte) must fail fast with a structured error and
publish no output file.
"""

import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"

sys.path.insert(0, str(ROOT / "tools"))
import launcher_contract as contract
import verify_release_package as verifier


HEADER = struct.Struct("<4sHIQ")
ENTRY = struct.Struct("<QIQQQI")


def pack(source, output):
    return subprocess.run([sys.executable, str(PACKAGER), str(source), str(output)],
                          capture_output=True, text=True, check=False)


def sha256_of(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write_fixture(root):
    (root / "json").mkdir(parents=True)
    (root / "images").mkdir(parents=True)
    (root / "json" / "story.json").write_text('{"start": 101}\n', encoding="utf-8")
    (root / "json" / "full_story_graph.json").write_text('{"nodes": []}\n', encoding="utf-8")
    (root / "images" / "hero.bin").write_bytes(bytes(range(256)))
    (root / "notes.txt").write_text("deterministic\n", encoding="utf-8")


def read_manifest(package_path):
    data = pathlib.Path(package_path).read_bytes()
    _, _, count, index_offset = HEADER.unpack(data[:HEADER.size])
    offset = index_offset
    manifest_payload = None
    for _ in range(count):
        _, path_length, entry_offset, compressed_size, _, flags = ENTRY.unpack(
            data[offset:offset + ENTRY.size])
        offset += ENTRY.size
        path = data[offset:offset + path_length].decode("utf-8")
        offset += path_length
        if path == "rowl/manifest.json":
            assert flags == 0, "manifest entry must be stored uncompressed"
            manifest_payload = data[entry_offset:entry_offset + compressed_size]
    assert manifest_payload is not None, "embedded manifest is missing"
    return json.loads(manifest_payload.decode("utf-8"))


def require_rejection(source, message, output_name="rejected.rowlpkg"):
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / output_name
        result = pack(source, output)
        if result.returncode == 0 or message not in result.stderr:
            raise SystemExit(f"expected packer rejection containing {message!r}: "
                             f"exit={result.returncode}, stderr={result.stderr!r}")
        if output.exists():
            raise SystemExit("rejected pack run must not publish an output file")


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    fixture = root / "fixture"
    write_fixture(fixture)

    first = root / "first.rowlpkg"
    second = root / "second.rowlpkg"
    for output in (first, second):
        result = pack(fixture, output)
        if result.returncode != 0:
            raise SystemExit(f"packer failed on a valid fixture: {result.stderr}")
    if sha256_of(first) != sha256_of(second):
        raise SystemExit("same source packed twice produced different digests")

    # mtimes and walk order must not leak into the bytes: retouch every file
    # to a different timestamp and pack the copy.
    retouched = root / "retouched"
    write_fixture(retouched)
    stamp = 946684800  # 2000-01-01, far from the original mtimes
    for current, _, files in os.walk(retouched):
        for name in files:
            os.utime(os.path.join(current, name), (stamp, stamp))
    third = root / "third.rowlpkg"
    result = pack(retouched, third)
    if result.returncode != 0:
        raise SystemExit(f"packer failed on the retouched fixture: {result.stderr}")
    if sha256_of(first) != sha256_of(third):
        raise SystemExit("file mtimes changed the package bytes")

    # The embedded manifest must describe exactly the packed files.
    manifest = read_manifest(first)
    if manifest.get("format") != 1:
        raise SystemExit("embedded manifest has an unsupported format")
    expected = {"images/hero.bin", "json/story.json", "json/full_story_graph.json", "notes.txt"}
    if [record["path"] for record in manifest["files"]] != sorted(expected):
        raise SystemExit("embedded manifest file list is not canonical")
    for record in manifest["files"]:
        source_bytes = (fixture / record["path"]).read_bytes()
        if record["size"] != len(source_bytes):
            raise SystemExit(f"manifest size mismatch for {record['path']}")
        if record["sha256"] != hashlib.sha256(source_bytes).hexdigest():
            raise SystemExit(f"manifest checksum mismatch for {record['path']}")

    # The release verifier must accept the deterministic package once it is
    # placed in a complete layout (notices file included).
    release = root / "release"
    (release / "Assets" / "packages").mkdir(parents=True)
    (release / "Assets" / "packages" / "game.rowlpkg").write_bytes(first.read_bytes())
    (release / "mods").mkdir()
    (release / "mods" / "README.md").write_text("# mods\n", encoding="utf-8")
    (release / "README.txt").write_text("release\n", encoding="utf-8")
    (release / "THIRD_PARTY_NOTICES.md").write_text("notices\n", encoding="utf-8")
    (release / "RowlGame").write_bytes(b"player")
    (release / "libRowlEngineCore.so").write_bytes(b"runtime")
    # Binary write, and the bytes are asserted below. pathlib.write_text()
    # defaults to newline=None, which maps every "\n" onto the host's
    # os.linesep -- so this fixture was LF on Linux and CRLF on Windows, and
    # the verifier rejected the CR on the Windows jobs only. That is what
    # turned rowl_package_determinism_tests red in the Windows matrix while
    # every other job stayed green.
    launcher_bytes = b"#!/bin/sh\nexec ./RowlGame \"$@\"\n"
    (release / "run_game.sh").write_bytes(launcher_bytes)
    if b"\r" in (release / "run_game.sh").read_bytes():
        raise SystemExit(f"run_game.sh fixture carries a CR: {launcher_bytes!r}")
    try:
        verifier.verify(str(release))
    except (OSError, ValueError) as error:
        raise SystemExit(f"verifier rejected the deterministic package: {error}")

    # The same reason from the other side: the fixture above must verify, and
    # the same launcher carrying the line endings a Windows text-mode write
    # would have produced must be rejected. Without this pair the whole suite
    # is byte-identical on both platforms and neither platform can notice the
    # difference -- which is exactly how the bug reached CI.
    crlf_release = root / "release-crlf"
    shutil.copytree(release, crlf_release)
    (crlf_release / "run_game.sh").write_bytes(
        contract.windows_text_mode_write("#!/bin/sh\nexec ./RowlGame \"$@\"\n")
        .encode("utf-8"))
    try:
        verifier.verify(str(crlf_release))
    except (OSError, ValueError) as error:
        if "has a CR" not in str(error):
            raise SystemExit(f"expected the CR rejection, got: {error}")
    else:
        raise SystemExit("verifier accepted a run_game.sh carrying a CR")

with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)

    empty_case = root / "empty-case"
    empty_case.mkdir()
    (empty_case / "ok.txt").write_text("ok\n", encoding="utf-8")
    (empty_case / "empty.txt").write_bytes(b"")
    require_rejection(empty_case, "[zero-byte]")

    dangling_case = root / "dangling-case"
    dangling_case.mkdir()
    (dangling_case / "ok.txt").write_text("ok\n", encoding="utf-8")
    try:
        (dangling_case / "ghost.txt").symlink_to(root / "does-not-exist.txt")
    except OSError as error:
        raise SystemExit("test host cannot create a symbolic link: " + str(error))
    require_rejection(dangling_case, "[dangling-symlink]")

    reserved_case = root / "reserved-case"
    (reserved_case / "rowl").mkdir(parents=True)
    (reserved_case / "rowl" / "manifest.json").write_text("{}\n", encoding="utf-8")
    require_rejection(reserved_case, "[reserved-path]")

    require_rejection(root / "missing-source-dir", "[missing-input-dir]")

print("[PackageDeterminism] repeated packs are byte-identical and validation rejects bad inputs.")
