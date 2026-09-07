using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using Avalonia.Media.Imaging;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// High-performance, centralized bitmap caching service with negative caching support.
    /// Prevents repeated disk I/O, file locks, and redundant JPEG/PNG header decodes.
    /// </summary>
    public static class AssetBitmapCache
    {
        private static readonly ConcurrentDictionary<string, Bitmap?> _cache = new(StringComparer.OrdinalIgnoreCase);

        /// <summary>
        /// Retrieves a cached Bitmap or decodes it from the project assets directory.
        /// If the file does not exist, caches null (negative caching) to prevent repeated disk queries.
        /// </summary>
        public static Bitmap? GetOrLoad(string? filename)
        {
            if (string.IsNullOrWhiteSpace(filename)) return null;

            string key = filename.Trim();
            if (_cache.TryGetValue(key, out var cachedBitmap))
            {
                return cachedBitmap;
            }

            string assetsPath = MainWindowViewModel.AssetsPath;
            string projectRoot = MainWindowViewModel.ProjectRoot;
            string fn = Path.GetFileName(filename);

            string[] searchPaths = new[]
            {
                filename,
                Path.Combine(projectRoot, filename),
                Path.Combine(assetsPath, filename),
                Path.Combine(assetsPath, "images", filename),
                Path.Combine(assetsPath, "images", fn),
                Path.Combine(projectRoot, "Assets", "images", fn)
            };

            Bitmap? loaded = null;
            foreach (var p in searchPaths)
            {
                if (!string.IsNullOrWhiteSpace(p) && File.Exists(p))
                {
                    try
                    {
                        loaded = new Bitmap(p);
                        break;
                    }
                    catch
                    {
                        // Ignore decode errors and continue fallback
                    }
                }
            }

            // Cache either the valid Bitmap or null (negative cache)
            _cache[key] = loaded;
            return loaded;
        }

        /// <summary>
        /// Invalidates a specific cached image (e.g. when overwritten by user).
        /// </summary>
        public static void Invalidate(string? filename)
        {
            if (string.IsNullOrWhiteSpace(filename)) return;
            string key = filename.Trim();
            var removed = new HashSet<Bitmap>(ReferenceEqualityComparer.Instance);
            if (_cache.TryRemove(key, out var cached) && cached != null) removed.Add(cached);
            if (_cache.TryRemove(Path.GetFileName(filename), out cached) && cached != null) removed.Add(cached);
            foreach (var bitmap in removed) bitmap.Dispose();
        }

        /// <summary>
        /// Clears all cached bitmaps.
        /// </summary>
        public static void Clear()
        {
            var bitmaps = new HashSet<Bitmap>(ReferenceEqualityComparer.Instance);
            foreach (var bitmap in _cache.Values)
            {
                if (bitmap != null) bitmaps.Add(bitmap);
            }
            _cache.Clear();
            foreach (var bitmap in bitmaps) bitmap.Dispose();
        }
    }
}
