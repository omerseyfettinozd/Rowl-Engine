#!/usr/bin/env python3
import os
import sys
import struct
import zlib

try:
    import zstandard as zstd
    HAS_ZSTD = True
except ImportError:
    HAS_ZSTD = False

def pack_directory(input_dir, output_pkg):
    print(f"[Packer] Compressing assets from '{input_dir}' into '{output_pkg}'...")

    entries = []
    payload_bytes = bytearray()

    # Collect files
    file_list = []
    for root, _, files in os.walk(input_dir):
        for f in files:
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, input_dir).replace('\\', '/')
            file_list.append((full_path, rel_path))

    header_size = 4 + 2 + 4 + 8 # 18 bytes
    current_offset = header_size

    cctx = zstd.ZstdCompressor(level=3) if HAS_ZSTD else None

    for full_path, rel_path in file_list:
        with open(full_path, 'rb') as f:
            uncompressed_data = f.read()

        uncompressed_size = len(uncompressed_data)

        if HAS_ZSTD and uncompressed_size > 0:
            compressed_data = cctx.compress(uncompressed_data)
            flags = 1 # Zstd
        else:
            compressed_data = uncompressed_data
            flags = 0 # Raw

        compressed_size = len(compressed_data)
        path_bytes = rel_path.encode('utf-8')
        path_hash = zlib.crc32(path_bytes) & 0xffffffff

        entries.append({
            'path_hash': path_hash,
            'rel_path': rel_path,
            'path_bytes': path_bytes,
            'offset': current_offset,
            'compressed_size': compressed_size,
            'uncompressed_size': uncompressed_size,
            'flags': flags,
            'data': compressed_data
        })

        payload_bytes.extend(compressed_data)
        current_offset += compressed_size

    index_offset = current_offset
    index_bytes = bytearray()

    # Build index table
    for entry in entries:
        path_len = len(entry['path_bytes'])
        # struct fmt: uint64 pathHash, uint32 pathLength, uint64 offset, uint64 compressedSize, uint64 uncompressedSize, uint32 flags
        entry_header = struct.pack('<QIQQQI',
            entry['path_hash'],
            path_len,
            entry['offset'],
            entry['compressed_size'],
            entry['uncompressed_size'],
            entry['flags']
        )
        index_bytes.extend(entry_header)
        index_bytes.extend(entry['path_bytes'])

    # Build master header: "ROWL", version=1 (uint16), fileCount (uint32), indexOffset (uint64)
    file_count = len(entries)
    master_header = struct.pack('<4sHIQ', b'ROWL', 1, file_count, index_offset)

    with open(output_pkg, 'wb') as out_f:
        out_f.write(master_header)
        out_f.write(payload_bytes)
        out_f.write(index_bytes)

    print(f"[Packer] Package creation successful! Total files: {file_count}, Output size: {os.path.getsize(output_pkg)} bytes")

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 tools/package_assets.py <input_dir> <output_rowlpkg>")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_pkg = sys.argv[2]
    pack_directory(input_dir, output_pkg)
