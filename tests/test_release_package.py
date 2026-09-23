#!/usr/bin/env python3
"""Contract tests for portable release package verification."""

import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
VERIFIER = ROOT / "tools" / "verify_release_package.py"


def run_verifier(release_root):
    return subprocess.run([sys.executable, str(VERIFIER), str(release_root)],
                          capture_output=True, text=True, check=False)


def require_rejection(release_root, message):
    result = run_verifier(release_root)
    if result.returncode == 0 or message not in result.stderr:
        raise SystemExit(f"expected verifier rejection containing {message!r}: "
                         f"stdout={result.stdout!r}, stderr={result.stderr!r}")


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    release = root / "release"
    package = release / "Assets" / "packages" / "game.rowlpkg"
    package.parent.mkdir(parents=True)
    subprocess.run([sys.executable, str(PACKAGER), str(ROOT / "Assets"), str(package)], check=True)
    (release / "mods").mkdir()
    (release / "mods" / "README.md").write_text("# mods\n", encoding="utf-8")
    (release / "README.txt").write_text("release\n", encoding="utf-8")
    shutil.copy2(ROOT / "packaging" / "THIRD_PARTY_NOTICES.md",
                 release / "THIRD_PARTY_NOTICES.md")
    (release / "RowlGame").write_bytes(b"player")
    (release / "libRowlEngineCore.so").write_bytes(b"runtime")
    (release / "run_game.sh").write_text("#!/bin/sh\nexec ./RowlGame \"$@\"\n", encoding="utf-8")

    valid = run_verifier(release)
    if valid.returncode != 0 or "Valid release" not in valid.stdout:
        raise SystemExit("valid release package was rejected: " + valid.stderr)

    missing_launcher = root / "missing-launcher"
    shutil.copytree(release, missing_launcher)
    (missing_launcher / "run_game.sh").unlink()
    require_rejection(missing_launcher, "missing release launcher")

    missing_runtime = root / "missing-runtime"
    shutil.copytree(release, missing_runtime)
    (missing_runtime / "libRowlEngineCore.so").unlink()
    require_rejection(missing_runtime, "missing native RowlEngineCore runtime library")

    corrupt_package = root / "corrupt-package"
    shutil.copytree(release, corrupt_package)
    (corrupt_package / "Assets" / "packages" / "game.rowlpkg").write_bytes(b"ROWL")
    require_rejection(corrupt_package, "package is shorter than its v1 header")

    loose_assets = root / "loose-assets"
    shutil.copytree(release, loose_assets)
    (loose_assets / "Assets" / "images").mkdir()
    require_rejection(loose_assets, "release Assets contains loose content")

    unsafe_mod = root / "unsafe-mod"
    shutil.copytree(release, unsafe_mod)
    outside = root / "outside.txt"
    outside.write_text("outside", encoding="utf-8")
    try:
        (unsafe_mod / "mods" / "override.txt").symlink_to(outside)
    except OSError as error:
        raise SystemExit("test host cannot create a symbolic link: " + str(error))
    require_rejection(unsafe_mod, "mods override contains a symbolic link")

    # --- Faz 6 Dilim 2: packer `verify` + `.sha256` sidecar contract ---
    def run_packer(*args):
        return subprocess.run([sys.executable, str(PACKAGER), *[str(a) for a in args]],
                              capture_output=True, text=True, check=False)

    def require_verify(package_path, code, fragment=None, json_mode=False):
        args = ["verify", package_path] + (["--json"] if json_mode else [])
        result = run_packer(*args)
        combined = result.stdout + result.stderr
        if result.returncode != code:
            raise SystemExit(f"expected packer verify exit {code}, got {result.returncode}: "
                             f"stdout={result.stdout!r}, stderr={result.stderr!r}")
        if fragment is not None and fragment not in combined:
            raise SystemExit(f"expected packer verify output containing {fragment!r}: "
                             f"stdout={result.stdout!r}, stderr={result.stderr!r}")
        return result

    pack_src = root / "pack-src"
    (pack_src / "sub").mkdir(parents=True)
    (pack_src / "a.png").write_bytes(b"fake-png-bytes-0001")
    (pack_src / "sub" / "b.wav").write_bytes(b"fake-wav-bytes-0002")
    pkg_a = root / "a.rowlpkg"

    pack_result = run_packer(pack_src, pkg_a)
    if pack_result.returncode != 0:
        raise SystemExit("packer failed on small asset dir: " + pack_result.stderr)

    # .sha256 sidecar: canonical `<hash><two-spaces><basename>\n`, sha256sum-compatible.
    sidecar_a = pathlib.Path(str(pkg_a) + ".sha256")
    if not sidecar_a.is_file():
        raise SystemExit("packer did not write the .sha256 sidecar")
    package_digest = hashlib.sha256(pkg_a.read_bytes()).hexdigest()
    expected_sidecar = f"{package_digest}  {pkg_a.name}\n"
    if sidecar_a.read_text(encoding="utf-8") != expected_sidecar:
        raise SystemExit("sidecar is not in canonical '<hash>  <basename>\\n' form: "
                         f"{sidecar_a.read_text(encoding='utf-8')!r}")

    # pack -> verify OK (human-readable and --json).
    require_verify(pkg_a, 0, "OK")
    json_ok = require_verify(pkg_a, 0, '"ok": true', json_mode=True)
    payload = json.loads(json_ok.stdout)
    if payload.get("entries") != 3 or payload.get("sidecar") != "ok":
        raise SystemExit("verify --json payload is wrong: " + json_ok.stdout)

    # Deterministic .sha256: packing the same tree to the same path twice
    # yields identical sidecar bytes (and identical package bytes).
    sidecar_before = sidecar_a.read_bytes()
    package_before = pkg_a.read_bytes()
    pack_again = run_packer(pack_src, pkg_a)
    if pack_again.returncode != 0:
        raise SystemExit("second packer run failed: " + pack_again.stderr)
    if sidecar_a.read_bytes() != sidecar_before or pkg_a.read_bytes() != package_before:
        raise SystemExit("deterministic pack produced different bytes")

    # 1 flipped byte -> exit 1 (stale sidecar mismatch). The flip targets the
    # LAST byte (index-table tail: the manifest entry path), never the payload:
    # payload bytes of zstd-compressed entries are not re-hashed by verify
    # (see KI-11), so a payload flip is red in raw mode but green when the
    # `zstandard` module is present. The index tail is covered in both modes.
    pkg_corrupt = root / "corrupt.rowlpkg"
    shutil.copy2(pkg_a, pkg_corrupt)
    pathlib.Path(str(pkg_corrupt) + ".sha256").write_text(
        f"{package_digest}  {pkg_corrupt.name}\n", encoding="utf-8")
    raw = bytearray(pkg_corrupt.read_bytes())
    raw[-1] ^= 1
    pkg_corrupt.write_bytes(bytes(raw))
    require_verify(pkg_corrupt, 1, "does not match")

    # Missing sidecar -> still exit 1 (fail-closed distribution gate), even
    # though the package itself is internally consistent.
    pkg_nosidecar = root / "nosidecar.rowlpkg"
    shutil.copy2(pkg_a, pkg_nosidecar)
    require_verify(pkg_nosidecar, 1, "missing")

    # Manifest-broken but sidecar-fresh -> exit 1 via the deep manifest check.
    fresh_digest = hashlib.sha256(pkg_corrupt.read_bytes()).hexdigest()
    pathlib.Path(str(pkg_corrupt) + ".sha256").write_text(
        f"{fresh_digest}  {pkg_corrupt.name}\n", encoding="utf-8")
    require_verify(pkg_corrupt, 1, "manifest")

    # Missing package file -> exit 1.
    require_verify(root / "does-not-exist.rowlpkg", 1, "does not exist")

    # Malformed sidecar -> exit 1 (unreadable digest, not a hash compare).
    pkg_malformed = root / "malformed.rowlpkg"
    shutil.copy2(pkg_a, pkg_malformed)
    pathlib.Path(str(pkg_malformed) + ".sha256").write_text(
        "not-a-digest\n", encoding="utf-8")
    require_verify(pkg_malformed, 1, "malformed")

    # Usage error -> exit 2.
    usage = run_packer("verify")
    if usage.returncode != 2:
        raise SystemExit("expected packer verify usage error exit 2, got "
                         f"{usage.returncode}")

    print("[ReleasePackageTests] packer verify + .sha256 contract holds.")

    # --- W8-g: traversal curtain mirrors the engine reader ---
    # normalizePackagePath (engine/src/vfs/rowlpkg_reader.cpp:191-204)
    # fail-closes on NUL bytes and on a surviving ".." segment. The packer
    # can never emit these names (real files cannot be called ".." or
    # contain NUL), so the fixtures are crafted byte-by-byte below — fully
    # manifest-consistent, so only the traversal curtain can reject them.
    _HEADER = struct.Struct("<4sHIQ")
    _ENTRY = struct.Struct("<QIQQQI")

    def craft_package(package_path, names):
        blobs = [(name, b"w8g-payload:" + name.replace(b"\x00", b"_"))
                 for name in names]
        payload = bytearray(_HEADER.size)
        offsets = {}
        for name, blob in blobs:
            offsets[name] = len(payload)
            payload.extend(blob)
        records = [{
            "compressed_size": len(blob),
            "flags": 0,
            "path": name.decode("utf-8"),
            "sha256": hashlib.sha256(blob).hexdigest(),
            "size": len(blob),
        } for name, blob in blobs]
        records.sort(key=lambda record: record["path"])
        manifest_bytes = (json.dumps({"files": records, "format": 1},
                                     sort_keys=True, separators=(",", ":"))
                          + "\n").encode("utf-8")
        manifest_offset = len(payload)
        payload.extend(manifest_bytes)
        index_offset = len(payload)
        index = bytearray()
        manifest_name = b"rowl/manifest.json"
        for name, blob in blobs + [(manifest_name, manifest_bytes)]:
            offset = offsets.get(name, manifest_offset)
            index.extend(_ENTRY.pack(0, len(name), offset, len(blob),
                                     len(blob), 0))
            index.extend(name)
        _HEADER.pack_into(payload, 0, b"ROWL", 1, len(blobs) + 1, index_offset)
        payload.extend(index)
        pathlib.Path(package_path).write_bytes(bytes(payload))

    def stage_with_package(tag, package_path):
        staged = root / tag
        shutil.copytree(release, staged)
        shutil.copy2(package_path,
                     staged / "Assets" / "packages" / "game.rowlpkg")
        return staged

    graph_name = b"json/full_story_graph.json"
    for tag, evil in [("bare-dotdot", b".."),
                      ("trailing-dotdot", b"sub/.."),
                      ("inner-dotdot", b"a/../b"),
                      ("backslash-dotdot", b"..\\evil"),
                      ("embedded-nul", b"a\x00b")]:
        evil_pkg = root / f"w8g-{tag}.rowlpkg"
        craft_package(evil_pkg, [evil, graph_name])
        require_rejection(stage_with_package(f"w8g-{tag}", evil_pkg),
                          "unsafe or duplicate entry path")

    # False-positive control: dotty but harmless names stay green.
    control_pkg = root / "w8g-control.rowlpkg"
    craft_package(control_pkg, [b"a/..b", b"a/b..", b"...", graph_name])
    control = run_verifier(stage_with_package("w8g-control", control_pkg))
    if control.returncode != 0 or "Valid release" not in control.stdout:
        raise SystemExit("traversal curtain rejected harmless dotty names: "
                         + control.stderr)

    print("[ReleasePackageTests] traversal curtain mirrors the reader "
          "(dot-dot + NUL rejected, dotty names green).")
