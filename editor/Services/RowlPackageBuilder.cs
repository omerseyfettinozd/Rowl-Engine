using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace RowlEngine.Editor.Services
{
    public static class RowlPackageBuilder
    {
        public static ulong ComputeFnv1a64(string text)
        {
            ulong hash = 14695981039346656037UL;
            byte[] bytes = Encoding.UTF8.GetBytes(text.Replace('\\', '/').TrimStart('/'));
            foreach (byte b in bytes)
            {
                hash ^= b;
                hash *= 1099511628211UL;
            }
            return hash;
        }

        private class EntryMeta
        {
            public ulong Hash;
            public string RelPath = "";
            public byte[] Data = Array.Empty<byte>();
            public uint UncompressedSize;
            public uint CompressedSize;
            public uint Flags; // 0 = uncompressed, 1 = zstd
            public ulong Offset;
        }

        public static bool BuildPackageFromDirectory(string inputDir, string outputPkgPath)
        {
            try
            {
                if (!Directory.Exists(inputDir)) return false;

                string? outDir = Path.GetDirectoryName(outputPkgPath);
                if (!string.IsNullOrEmpty(outDir) && !Directory.Exists(outDir))
                {
                    Directory.CreateDirectory(outDir);
                }

                var files = Directory.GetFiles(inputDir, "*", SearchOption.AllDirectories);
                var entries = new List<EntryMeta>();

                foreach (var f in files)
                {
                    string rel = Path.GetRelativePath(inputDir, f).Replace('\\', '/');
                    if (rel.EndsWith(".rowlpkg") || rel.EndsWith(".tmp") || rel.EndsWith(".gitkeep"))
                        continue;

                    byte[] data = File.ReadAllBytes(f);
                    ulong hash = ComputeFnv1a64(rel);

                    entries.Add(new EntryMeta
                    {
                        Hash = hash,
                        RelPath = rel,
                        Data = data,
                        UncompressedSize = (uint)data.Length,
                        CompressedSize = (uint)data.Length,
                        Flags = 0 // raw uncompressed
                    });
                }

                using var fs = new FileStream(outputPkgPath, FileMode.Create, FileAccess.Write, FileShare.None);
                using var bw = new BinaryWriter(fs);

                // 1. Write placeholder header (18 bytes)
                bw.Write(Encoding.ASCII.GetBytes("ROWL")); // 4 bytes Magic
                bw.Write((ushort)1);                       // 2 bytes Version
                bw.Write((uint)entries.Count);             // 4 bytes FileCount
                bw.Write((ulong)0);                        // 8 bytes IndexOffset placeholder

                // 2. Write file payloads
                foreach (var e in entries)
                {
                    e.Offset = (ulong)fs.Position;
                    bw.Write(e.Data);
                }

                // 3. Write index table
                ulong indexOffset = (ulong)fs.Position;

                foreach (var e in entries)
                {
                    byte[] pathBytes = Encoding.UTF8.GetBytes(e.RelPath);

                    // RowlPkgEntryRaw: uint64_t hash (8B), uint32_t pathLen (4B), uint64_t offset (8B), uint64_t compSize (8B), uint64_t uncompSize (8B), uint32_t flags (4B)
                    bw.Write((ulong)e.Hash);               // 8 bytes (64-bit uint64_t)
                    bw.Write((uint)pathBytes.Length);      // 4 bytes
                    bw.Write((ulong)e.Offset);             // 8 bytes
                    bw.Write((ulong)e.CompressedSize);     // 8 bytes
                    bw.Write((ulong)e.UncompressedSize);   // 8 bytes
                    bw.Write((uint)e.Flags);               // 4 bytes
                    bw.Write(pathBytes);                   // N bytes path
                }

                // 4. Update header IndexOffset
                fs.Seek(10, SeekOrigin.Begin);
                bw.Write((ulong)indexOffset);

                return true;
            }
            catch (Exception)
            {
                return false;
            }
        }
    }
}
