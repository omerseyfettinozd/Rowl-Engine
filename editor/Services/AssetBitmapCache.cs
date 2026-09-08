using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using Avalonia.Media.Imaging;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    public readonly record struct AssetBitmapCacheStats(
        int BitmapCount,
        long EstimatedRgbaBytes,
        int NegativeEntryCount);

    /// <summary>
    /// Centralized project-asset bitmap cache. Each decoded bitmap is keyed by
    /// its canonical asset path, so aliases share one native allocation.
    /// </summary>
    public static class AssetBitmapCache
    {
        private const string MissingPrefix = "!missing:";
        private static readonly ConcurrentDictionary<string, Lazy<Bitmap?>> _cache =
            new(StringComparer.OrdinalIgnoreCase);
        private static readonly ConcurrentDictionary<string, string> _requestToCacheKey =
            new(StringComparer.OrdinalIgnoreCase);

        public static Bitmap? GetOrLoad(string? filename)
        {
            if (string.IsNullOrWhiteSpace(filename)) return null;

            string requestKey = NormalizeRequest(filename);
            if (_requestToCacheKey.TryGetValue(requestKey, out var cachedKey) &&
                _cache.TryGetValue(cachedKey, out var cached))
                return cached.Value;

            string cacheKey = ResolveProjectAsset(filename) ?? (MissingPrefix + requestKey);
            var lazyBitmap = _cache.GetOrAdd(cacheKey, static key =>
                new Lazy<Bitmap?>(() => DecodeBitmap(key), LazyThreadSafetyMode.ExecutionAndPublication));
            _requestToCacheKey[requestKey] = cacheKey;
            return lazyBitmap.Value;
        }

        public static AssetBitmapCacheStats GetStats()
        {
            int bitmapCount = 0;
            int negativeEntryCount = 0;
            long estimatedRgbaBytes = 0;
            foreach (var pair in _cache)
            {
                if (pair.Key.StartsWith(MissingPrefix, StringComparison.Ordinal))
                {
                    negativeEntryCount++;
                    continue;
                }
                if (!pair.Value.IsValueCreated) continue;
                var bitmap = pair.Value.Value;
                if (bitmap == null)
                {
                    negativeEntryCount++;
                    continue;
                }
                bitmapCount++;
                estimatedRgbaBytes += (long)bitmap.PixelSize.Width * bitmap.PixelSize.Height * 4L;
            }
            return new AssetBitmapCacheStats(bitmapCount, estimatedRgbaBytes, negativeEntryCount);
        }

        public static void Invalidate(string? filename)
        {
            if (string.IsNullOrWhiteSpace(filename)) return;

            string requestKey = NormalizeRequest(filename);
            string basename = Path.GetFileName(requestKey);
            var cacheKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var pair in _requestToCacheKey)
            {
                if (string.Equals(pair.Key, requestKey, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(Path.GetFileName(pair.Key), basename, StringComparison.OrdinalIgnoreCase))
                {
                    if (_requestToCacheKey.TryRemove(pair.Key, out var removedKey))
                        cacheKeys.Add(removedKey);
                }
            }

            var resolved = ResolveProjectAsset(filename);
            if (resolved != null) cacheKeys.Add(resolved);
            cacheKeys.Add(MissingPrefix + requestKey);
            DisposeEntries(cacheKeys);
        }

        public static void Clear()
        {
            var cacheKeys = new List<string>(_cache.Keys);
            _requestToCacheKey.Clear();
            DisposeEntries(cacheKeys);
        }

        private static void DisposeEntries(IEnumerable<string> cacheKeys)
        {
            var disposed = new HashSet<Bitmap>(ReferenceEqualityComparer.Instance);
            foreach (var cacheKey in cacheKeys)
            {
                if (_cache.TryRemove(cacheKey, out var lazyBitmap) && lazyBitmap.IsValueCreated)
                {
                    var bitmap = lazyBitmap.Value;
                    if (bitmap != null && disposed.Add(bitmap)) bitmap.Dispose();
                }
            }
        }

        private static Bitmap? DecodeBitmap(string cacheKey)
        {
            if (cacheKey.StartsWith(MissingPrefix, StringComparison.Ordinal)) return null;
            try { return new Bitmap(cacheKey); }
            catch { return null; }
        }

        private static string NormalizeRequest(string filename) => filename.Trim().Replace('\\', '/');

        private static string? ResolveProjectAsset(string filename)
        {
            string assetsRoot = Path.GetFullPath(MainWindowViewModel.AssetsPath);
            string normalized = filename.Trim();
            string basename = Path.GetFileName(normalized);
            var candidates = new List<string>();
            if (Path.IsPathFullyQualified(normalized))
                candidates.Add(normalized);
            else
            {
                candidates.Add(Path.Combine(assetsRoot, normalized));
                candidates.Add(Path.Combine(assetsRoot, "images", normalized));
                candidates.Add(Path.Combine(assetsRoot, "images", basename));
            }

            foreach (var candidate in candidates)
            {
                string fullPath;
                try { fullPath = Path.GetFullPath(candidate); }
                catch { continue; }
                if (!IsInsideAssetsRoot(assetsRoot, fullPath) || !File.Exists(fullPath)) continue;
                return fullPath;
            }
            return null;
        }

        private static bool IsInsideAssetsRoot(string assetsRoot, string candidate)
        {
            string rootWithSeparator = assetsRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                + Path.DirectorySeparatorChar;
            return candidate.StartsWith(rootWithSeparator, StringComparison.OrdinalIgnoreCase);
        }
    }
}
