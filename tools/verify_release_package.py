#!/usr/bin/env python3
"""Validate the portable Rowl desktop release layout and its v1 package index."""

import argparse
import os
import struct
import sys


HEADER = struct.Struct("<4sHIQ")
ENTRY = struct.Struct("<QIQQQI")
MAX_ENTRIES = 100_000


def fail(message):
    raise ValueError(message)


def read_package_entries(package_path):
    package_size = os.path.getsize(package_path)
    if package_size < HEADER.size:
        fail("package is shorter than its v1 header")

    with open(package_path, "rb") as package:
        magic, version, count, index_offset = HEADER.unpack(package.read(HEADER.size))
        if magic != b"ROWL" or version != 1:
            fail("package is not a ROWL v1 archive")
        if count == 0 or count > MAX_ENTRIES:
            fail("package entry count is outside the accepted range")
        if index_offset < HEADER.size or index_offset >= package_size:
            fail("package index offset is outside the archive")

        package.seek(index_offset)
        paths = set()
        for _ in range(count):
            raw = package.read(ENTRY.size)
            if len(raw) != ENTRY.size:
                fail("package index ends before all entries were read")
            _, path_length, offset, compressed_size, _, _ = ENTRY.unpack(raw)
            if path_length == 0 or path_length > 16 * 1024:
                fail("package contains an invalid path length")
            path_bytes = package.read(path_length)
            if len(path_bytes) != path_length:
                fail("package entry path is truncated")
            try:
                path = path_bytes.decode("utf-8")
            except UnicodeDecodeError as error:
                fail("package entry path is not UTF-8: " + str(error))
            normalized = path.replace("\\", "/")
            if (normalized.startswith("/") or normalized.startswith("../") or
                    "/../" in normalized or normalized in paths):
                fail("package contains an unsafe or duplicate entry path: " + path)
            if offset < HEADER.size or offset + compressed_size > index_offset:
                fail("package entry payload points outside the payload area: " + path)
            paths.add(normalized)
    return paths


def verify_mod_overrides(mods_root):
    """Reject links and special files before a release exposes mods to the VFS."""
    for current_root, directories, files in os.walk(mods_root, followlinks=False):
        for name in directories + files:
            candidate = os.path.join(current_root, name)
            if os.path.islink(candidate):
                fail("mods override contains a symbolic link: " + candidate)
            if not os.path.isdir(candidate) and not os.path.isfile(candidate):
                fail("mods override contains a non-regular entry: " + candidate)


def verify(release_root):
    root = os.path.abspath(release_root)
    if not os.path.isdir(root):
        fail("release root does not exist: " + root)

    package_path = os.path.join(root, "Assets", "packages", "game.rowlpkg")
    if not os.path.isfile(package_path):
        fail("missing canonical package: Assets/packages/game.rowlpkg")
    if not os.path.isdir(os.path.join(root, "mods")):
        fail("missing mods override directory")
    mods_root = os.path.join(root, "mods")
    if not os.path.isfile(os.path.join(mods_root, "README.md")):
        fail("missing mods override README")
    if not os.path.isfile(os.path.join(root, "README.txt")):
        fail("missing release README")
    if not (os.path.isfile(os.path.join(root, "RowlGame")) or
            os.path.isfile(os.path.join(root, "RowlGame.exe"))):
        fail("missing standalone player executable")
    if not any(name.startswith(("libRowlEngineCore", "RowlEngineCore"))
               for name in os.listdir(root)):
        fail("missing native RowlEngineCore runtime library")

    entries = read_package_entries(package_path)
    if "json/full_story_graph.json" not in entries:
        fail("canonical package does not contain json/full_story_graph.json")

    # The release must not accidentally ship a full loose Assets tree.
    assets_root = os.path.join(root, "Assets")
    allowed_assets = {"packages"}
    if any(entry.name not in allowed_assets for entry in os.scandir(assets_root)):
        fail("release Assets contains loose content; game.rowlpkg must be canonical")
    verify_mod_overrides(mods_root)

    print("[ReleaseVerifier] Valid release: game.rowlpkg contains "
          f"{len(entries)} entries including json/full_story_graph.json.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_root")
    args = parser.parse_args()
    try:
        verify(args.release_root)
    except (OSError, ValueError) as error:
        print("[ReleaseVerifier] ERROR: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
