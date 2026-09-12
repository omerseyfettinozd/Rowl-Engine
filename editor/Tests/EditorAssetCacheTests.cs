using System;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor;

internal static class EditorAssetCacheTests
{
    public static void Run()
    {
        Console.WriteLine("\n📌 [Test 8]: Performance Benchmark & Cache Optimization Verification...");
        var stopwatch = Stopwatch.StartNew();

        // Benchmark 1: Negative caching for missing files (10,000 lookups)
        const int lookupIterations = 10000;
        for (int i = 0; i < lookupIterations; i++)
        {
            var bitmap = AssetBitmapCache.GetOrLoad("non_existent_placeholder_image.png");
            if (bitmap != null)
                throw new Exception("Expected null for non-existent image");
        }

        stopwatch.Stop();
        double negativeCacheMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
        double negativeCacheIops = lookupIterations / negativeCacheMilliseconds * 1000.0;
        Console.WriteLine(
            $"  ⚡ [BENCHMARK] AssetBitmapCache Negative Lookups: {lookupIterations:N0} queries in " +
            $"{negativeCacheMilliseconds:F2}ms ({negativeCacheIops:N0} queries/sec)");

        if (negativeCacheMilliseconds > 500)
            throw new Exception("Negative caching benchmark was too slow (>500ms)");

        // Different editor fields often describe the same image with a bare
        // filename or an images/ prefix. They must share one native Bitmap.
        AssetBitmapCache.Clear();
        var bareAsset = AssetBitmapCache.GetOrLoad("Woman.png");
        var prefixedAsset = AssetBitmapCache.GetOrLoad("images/Woman.png");
        var cacheStats = AssetBitmapCache.GetStats();
        if (bareAsset == null || prefixedAsset == null ||
            !ReferenceEquals(bareAsset, prefixedAsset) ||
            cacheStats.BitmapCount != 1 || cacheStats.EstimatedRgbaBytes <= 0)
        {
            throw new Exception("Asset cache did not canonicalize aliases into one bitmap");
        }

        Console.WriteLine(
            $"  ⚡ [BENCHMARK] AssetBitmapCache: {cacheStats.BitmapCount} unique bitmap, " +
            $"{cacheStats.EstimatedRgbaBytes:N0} estimated RGBA bytes");
        AssetBitmapCache.Clear();
        if (AssetBitmapCache.GetStats().BitmapCount != 0)
            throw new Exception("Asset cache statistics did not clear with the cache");

        // Exercise the Lazy cache factory under actual simultaneous alias
        // requests; every caller must receive the one shared Bitmap.
        object?[] concurrentAssets = new object?[16];
        Parallel.For(0, concurrentAssets.Length, i =>
        {
            concurrentAssets[i] = AssetBitmapCache.GetOrLoad(
                i % 2 == 0 ? "Woman.png" : "images/Woman.png");
        });
        if (concurrentAssets.Any(bitmap => bitmap == null) ||
            concurrentAssets.Any(bitmap => !ReferenceEquals(bitmap, concurrentAssets[0])) ||
            AssetBitmapCache.GetStats().BitmapCount != 1)
        {
            throw new Exception("Concurrent asset requests decoded duplicate bitmaps");
        }

        AssetBitmapCache.Clear();
        Console.WriteLine("  ✅ [PASS] AssetBitmapCache high-throughput negative caching & memory safety verified");
    }
}
