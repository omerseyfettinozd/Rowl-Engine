using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Tools.RowlEye.Ai;
using SkiaSharp;

namespace RowlEngine.Editor.Tools.RowlEye.Engine;

/// <summary>
/// Detailed frame timing and latency distribution metrics over a ring buffer window.
/// </summary>
public sealed record FrameStatsDto(
    [property: JsonPropertyName("minMs")] double MinMs,
    [property: JsonPropertyName("avgMs")] double AvgMs,
    [property: JsonPropertyName("p50Ms")] double P50Ms,
    [property: JsonPropertyName("p95Ms")] double P95Ms,
    [property: JsonPropertyName("p99Ms")] double P99Ms,
    [property: JsonPropertyName("maxMs")] double MaxMs,
    [property: JsonPropertyName("sampleCount")] int SampleCount,
    [property: JsonPropertyName("hitchCount")] int HitchCount,
    [property: JsonPropertyName("spikeCount")] int SpikeCount
);

/// <summary>
/// Combined telemetry of host process memory, GC collections, and texture cache budget.
/// </summary>
public sealed record EngineMemoryStatsDto(
    [property: JsonPropertyName("managedHeapBytes")] long ManagedHeapBytes,
    [property: JsonPropertyName("processWorkingSetBytes")] long ProcessWorkingSetBytes,
    [property: JsonPropertyName("processPrivateBytes")] long ProcessPrivateBytes,
    [property: JsonPropertyName("gcGen0Collections")] int GcGen0Collections,
    [property: JsonPropertyName("gcGen1Collections")] int GcGen1Collections,
    [property: JsonPropertyName("gcGen2Collections")] int GcGen2Collections,
    [property: JsonPropertyName("textureCacheBudgetBytes")] ulong TextureCacheBudgetBytes,
    [property: JsonPropertyName("textureCacheEvictions")] ulong TextureCacheEvictions,
    [property: JsonPropertyName("frameStats")] FrameStatsDto FrameStats
);

/// <summary>
/// Diagnostic and performance telemetry payload for the native C++ engine core.
/// </summary>
public sealed record EngineContextDto(
    [property: JsonPropertyName("isInitialized")] bool IsInitialized,
    [property: JsonPropertyName("isPlaying")] bool IsPlaying,
    [property: JsonPropertyName("frameId")] ulong FrameId,
    [property: JsonPropertyName("resolution")] int[] Resolution,
    [property: JsonPropertyName("fpsEstimate")] double FpsEstimate,
    [property: JsonPropertyName("textureLoadMs")] double TextureLoadMs,
    [property: JsonPropertyName("nonTextureMs")] double NonTextureMs,
    [property: JsonPropertyName("textRasterMs")] double TextRasterMs,
    [property: JsonPropertyName("rendererFlushMs")] double RendererFlushMs,
    [property: JsonPropertyName("totalFrameMs")] double TotalFrameMs,
    [property: JsonPropertyName("textureCacheBytes")] ulong TextureCacheBytes,
    [property: JsonPropertyName("textureCacheEvictions")] ulong TextureCacheEvictions,
    [property: JsonPropertyName("frameStats")] FrameStatsDto? FrameStats = null,
    [property: JsonPropertyName("frameSha256")] string? FrameSha256 = null
);

/// <summary>
/// Result of evaluating a sandboxed Lua script or condition.
/// </summary>
public sealed record ScriptEvalResultDto(
    [property: JsonPropertyName("ok")] bool Ok,
    [property: JsonPropertyName("result")] string? Result,
    [property: JsonPropertyName("error")] string? Error,
    [property: JsonPropertyName("errorCode")] int ErrorCode,
    [property: JsonPropertyName("elapsedMs")] double ElapsedMs
);

/// <summary>
/// Result of scanning the project Assets folder for unreferenced / dead assets.
/// </summary>
public sealed record DeadAssetsResultDto(
    [property: JsonPropertyName("totalScannedFiles")] int TotalScannedFiles,
    [property: JsonPropertyName("unreferencedFiles")] IReadOnlyList<string> UnreferencedFiles,
    [property: JsonPropertyName("totalReclaimableBytes")] long TotalReclaimableBytes
);

/// <summary>
/// Service managing the native RowlEngineCore offscreen instance, providing frame capture,
/// performance metrics, and deterministic stepping/playback controls for AI models.
/// </summary>
public sealed class RowlEyeEngineService : IDisposable
{
    private static readonly Lazy<RowlEyeEngineService> InstanceLazy = new(() => new RowlEyeEngineService());
    public static RowlEyeEngineService Instance => InstanceLazy.Value;

    private static readonly NativeBridge.NativeLogCallback s_nativeLogCallback = OnNativeLogReceived;
    private static bool s_callbackHooked = false;

    private static void OnNativeLogReceived(int level, string levelStr, string message, string timestamp)
    {
        try
        {
            var payload = new NativeLogPayloadDto(level, levelStr ?? "", message ?? "", timestamp ?? "");
            AiTelemetryHub.Instance.Publish(AiTelemetryEventType.NativeLog, payload);

            // Anomaly detection: Alert AI agent on engine errors, shader/font/audio fallbacks (C1 / B2)
            string safeMsg = message ?? "";
            bool isFallbackOrError = level >= 3 ||
                safeMsg.Contains("fallback", StringComparison.OrdinalIgnoreCase) ||
                (safeMsg.Contains("shader", StringComparison.OrdinalIgnoreCase) && safeMsg.Contains("unavailable", StringComparison.OrdinalIgnoreCase)) ||
                (safeMsg.Contains("atlas", StringComparison.OrdinalIgnoreCase) && safeMsg.Contains("unavailable", StringComparison.OrdinalIgnoreCase));

            if (isFallbackOrError)
            {
                AiTelemetryHub.Instance.Publish(AiTelemetryEventType.AnomalyDetected, new
                {
                    category = level >= 3 ? "NativeError" : "RenderFallback",
                    code = level >= 3 ? "NATIVE_ERROR" : "FALLBACK_TRIGGERED",
                    level = levelStr ?? "WARN",
                    detail = safeMsg,
                    timestamp = timestamp ?? ""
                });
            }
        }
        catch
        {
            // Guarantee no exception leaks across P/Invoke boundary
        }
    }

    private readonly object _lock = new();
    private EngineHost? _engineHost;
    private ulong _frameCounter = 0;
    private byte[]? _cachedFrameBytes;
    private string _cachedFrameFormat = "jpeg";
    private ulong _cachedFrameId = 0;
    private uint _engineWidth = 1280;
    private uint _engineHeight = 720;
    private bool _isDisposed;

    // Frame ring buffer and statistics tracking (A1)
    private const int FrameRingCapacity = 600;
    private readonly double[] _frameRing = new double[FrameRingCapacity];
    private int _frameRingHead = 0;
    private int _frameRingCount = 0;
    private int _hitchCount = 0;
    private int _spikeCount = 0;
    private readonly object _frameRingLock = new();

    // Deterministic frame SHA-256 fingerprinting (A23)
    private readonly ConcurrentDictionary<uint, string> _nodeFrameHashes = new();
    private string _lastFrameSha256 = "";

    public bool IsInitialized => _engineHost?.IsInitialized == true;

    public void HookLogCallback()
    {
        lock (_lock)
        {
            if (s_callbackHooked) return;
            try
            {
                NativeBridge.RowlEngine_SetLogCallback(s_nativeLogCallback);
                s_callbackHooked = true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[RowlEyeEngineService] Failed to hook native log callback: {ex.Message}");
            }
        }
    }

    private static string? ResolveProjectRoot()
    {
        var dir = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (dir != null)
        {
            if (Directory.Exists(Path.Combine(dir.FullName, "Assets")) &&
                (File.Exists(Path.Combine(dir.FullName, "Assets", "full_story_graph.json")) ||
                 File.Exists(Path.Combine(dir.FullName, "Assets", "json", "full_story_graph.json"))))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }

        dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (Directory.Exists(Path.Combine(dir.FullName, "Assets")) &&
                (File.Exists(Path.Combine(dir.FullName, "Assets", "full_story_graph.json")) ||
                 File.Exists(Path.Combine(dir.FullName, "Assets", "json", "full_story_graph.json"))))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }

        return null;
    }

    public void EnsureInitialized(uint width = 1280, uint height = 720)
    {
        HookLogCallback();
        lock (_lock)
        {
            if (_isDisposed) throw new ObjectDisposedException(nameof(RowlEyeEngineService));
            if (_engineHost?.IsInitialized == true) return;

            _engineWidth = width;
            _engineHeight = height;
            _engineHost = new EngineHost();
            bool success = _engineHost.Initialize(width, height);
            if (success)
            {
                string? projectRoot = ResolveProjectRoot();
                if (!string.IsNullOrEmpty(projectRoot))
                {
                    _engineHost.SetProjectDirectory(projectRoot);
                }

                // Run an initial step to produce the baseline frame
                _engineHost.Step(0.016f);
                _frameCounter++;
            }
        }
    }

    /// <summary>
    /// Returns the active performance and telemetry metrics of the native engine.
    /// </summary>
    public EngineContextDto GetContext()
    {
        lock (_lock)
        {
            if (_engineHost == null || !_engineHost.IsInitialized)
            {
                return new EngineContextDto(
                    IsInitialized: false,
                    IsPlaying: false,
                    FrameId: 0,
                    Resolution: new[] { 0, 0 },
                    FpsEstimate: 0,
                    TextureLoadMs: 0,
                    NonTextureMs: 0,
                    TextRasterMs: 0,
                    RendererFlushMs: 0,
                    TotalFrameMs: 0,
                    TextureCacheBytes: 0,
                    TextureCacheEvictions: 0
                );
            }

            double texLoad = _engineHost.LastFrameTextureLoadMilliseconds;
            double nonTex = _engineHost.LastFrameNonTextureRenderMilliseconds;
            double textRaster = _engineHost.LastFrameTextRasterizationMilliseconds;
            double flush = _engineHost.LastFrameRendererFlushMilliseconds;
            double totalMs = texLoad + nonTex + textRaster + flush;
            double fps = totalMs > 0.05 ? Math.Min(120.0, 1000.0 / totalMs) : 60.0;

            return new EngineContextDto(
                IsInitialized: true,
                IsPlaying: _engineHost.IsPlaying,
                FrameId: _frameCounter,
                Resolution: new[] { (int)_engineWidth, (int)_engineHeight },
                FpsEstimate: Math.Round(fps, 1),
                TextureLoadMs: Math.Round(texLoad, 2),
                NonTextureMs: Math.Round(nonTex, 2),
                TextRasterMs: Math.Round(textRaster, 2),
                RendererFlushMs: Math.Round(flush, 2),
                TotalFrameMs: Math.Round(totalMs, 2),
                TextureCacheBytes: _engineHost.TextureCacheBudgetBytes,
                TextureCacheEvictions: _engineHost.TextureCacheEvictionCount,
                FrameStats: GetFrameStats(),
                FrameSha256: _lastFrameSha256
            );
        }
    }

    /// <summary>
    /// Computes the SHA-256 fingerprint of the current framebuffer pixels and optionally associates it with a story node.
    /// </summary>
    public string ComputeFrameSha256(uint? associateWithNodeId = null)
    {
        EnsureInitialized();

        byte[]? raw = CaptureFrame(320, "png", 70);
        if (raw == null || raw.Length == 0) return _lastFrameSha256;

        byte[] hash = SHA256.HashData(raw);
        string hex = Convert.ToHexStringLower(hash);

        lock (_lock)
        {
            _lastFrameSha256 = hex;
        }

        if (associateWithNodeId.HasValue && associateWithNodeId.Value > 0)
        {
            _nodeFrameHashes[associateWithNodeId.Value] = hex;
        }

        return hex;
    }

    /// <summary>
    /// Gets the most recently computed frame SHA-256 fingerprint.
    /// </summary>
    public string LastFrameSha256 => _lastFrameSha256;

    /// <summary>
    /// Mapping of visited story node IDs to their rendered frame SHA-256 fingerprints.
    /// </summary>
    public IReadOnlyDictionary<uint, string> NodeFrameHashes => _nodeFrameHashes;

    /// <summary>
    /// Records a frame duration sample into the 600-frame ring buffer and fires performance anomalies if needed.
    /// </summary>
    public void RecordFrameTime(double durationMs)
    {
        lock (_frameRingLock)
        {
            _frameRing[_frameRingHead] = durationMs;
            _frameRingHead = (_frameRingHead + 1) % FrameRingCapacity;
            if (_frameRingCount < FrameRingCapacity) _frameRingCount++;

            if (durationMs > 33.33)
            {
                _hitchCount++;
                AiTelemetryHub.Instance.Publish(AiTelemetryEventType.AnomalyDetected, new
                {
                    category = "Performance",
                    code = "FRAME_HITCH",
                    durationMs = Math.Round(durationMs, 2),
                    detail = $"Frame duration {Math.Round(durationMs, 1)}ms exceeded 33.33ms budget (<30 FPS)"
                });
            }
            else if (durationMs > 25.0)
            {
                _spikeCount++;
            }
        }
    }

    /// <summary>
    /// Computes summary statistics (min, avg, p50, p95, p99, max, hitchCount, spikeCount) over recorded frame samples.
    /// </summary>
    public FrameStatsDto GetFrameStats()
    {
        double[] samples;
        int hitches, spikes;
        lock (_frameRingLock)
        {
            if (_frameRingCount == 0)
            {
                return new FrameStatsDto(0, 0, 0, 0, 0, 0, 0, 0, 0);
            }
            samples = new double[_frameRingCount];
            for (int i = 0; i < _frameRingCount; i++)
            {
                int idx = (_frameRingHead - _frameRingCount + i + FrameRingCapacity) % FrameRingCapacity;
                samples[i] = _frameRing[idx];
            }
            hitches = _hitchCount;
            spikes = _spikeCount;
        }

        Array.Sort(samples);
        double min = Math.Round(samples[0], 2);
        double max = Math.Round(samples[^1], 2);
        double avg = Math.Round(samples.Average(), 2);
        double p50 = Math.Round(samples[(int)(0.50 * (samples.Length - 1))], 2);
        double p95 = Math.Round(samples[(int)(0.95 * (samples.Length - 1))], 2);
        double p99 = Math.Round(samples[(int)(0.99 * (samples.Length - 1))], 2);

        return new FrameStatsDto(min, avg, p50, p95, p99, max, samples.Length, hitches, spikes);
    }

    /// <summary>
    /// Retrieves host process memory, GC generational statistics, texture cache budget, and frame stats.
    /// </summary>
    public EngineMemoryStatsDto GetMemoryStats()
    {
        var proc = Process.GetCurrentProcess();
        ulong budget = 0;
        ulong evictions = 0;
        lock (_lock)
        {
            if (_engineHost?.IsInitialized == true)
            {
                budget = _engineHost.TextureCacheBudgetBytes;
                evictions = _engineHost.TextureCacheEvictionCount;
            }
        }

        long managedBytes = GC.GetTotalMemory(false);
        if (managedBytes > 500L * 1024 * 1024)
        {
            AiTelemetryHub.Instance.Publish(AiTelemetryEventType.AnomalyDetected, new
            {
                category = "Memory",
                code = "HIGH_MANAGED_HEAP",
                managedHeapMb = Math.Round(managedBytes / (1024.0 * 1024.0), 1),
                detail = "Managed heap memory exceeded 500MB warning threshold"
            });
        }

        return new EngineMemoryStatsDto(
            ManagedHeapBytes: managedBytes,
            ProcessWorkingSetBytes: proc.WorkingSet64,
            ProcessPrivateBytes: proc.PrivateMemorySize64,
            GcGen0Collections: GC.CollectionCount(0),
            GcGen1Collections: GC.CollectionCount(1),
            GcGen2Collections: GC.CollectionCount(2),
            TextureCacheBudgetBytes: budget,
            TextureCacheEvictions: evictions,
            FrameStats: GetFrameStats()
        );
    }

    /// <summary>
    /// Captures the offscreen framebuffer and encodes it to a downscaled JPEG or PNG image.
    /// </summary>
    public byte[]? CaptureFrame(int targetWidth = 640, string format = "jpeg", int quality = 80)
    {
        EnsureInitialized();

        byte[]? rentedRawPixels = null;
        uint width = 0, height = 0;

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true) return _cachedFrameBytes;

            // Return cached frame if already captured for this step
            if (_cachedFrameBytes != null && _cachedFrameId == _frameCounter && string.Equals(_cachedFrameFormat, format, StringComparison.OrdinalIgnoreCase))
            {
                return _cachedFrameBytes;
            }

            var snapshot = _engineHost.Runtime.Invoke(handle =>
            {
                IntPtr pixelPtr = NativeBridge.RowlEngine_GetPixelBufferEx(handle, out uint w, out uint h, out uint pitch);
                if (pixelPtr == IntPtr.Zero || w == 0 || h == 0)
                    return (Pixels: (byte[]?)null, Width: 0u, Height: 0u);

                uint tightRowBytes = checked(w * 4);
                if (pitch < tightRowBytes) return (Pixels: (byte[]?)null, Width: w, Height: h);

                int rowLength = checked((int)tightRowBytes);
                byte[] buffer = ArrayPool<byte>.Shared.Rent(checked(rowLength * (int)h));
                for (uint y = 0; y < h; y++)
                {
                    IntPtr row = IntPtr.Add(pixelPtr, checked((int)(y * pitch)));
                    Marshal.Copy(row, buffer, checked((int)y * rowLength), rowLength);
                }

                return (Pixels: buffer, Width: w, Height: h);
            });

            rentedRawPixels = snapshot.Pixels;
            width = snapshot.Width;
            height = snapshot.Height;
        }

        if (rentedRawPixels == null || width == 0 || height == 0)
        {
            return _cachedFrameBytes;
        }

        try
        {
            // Encode outside the native lock using SkiaSharp
            using var rawBitmap = new SKBitmap();
            var info = new SKImageInfo((int)width, (int)height, SKColorType.Rgba8888, SKAlphaType.Premul);

            unsafe
            {
                fixed (byte* p = rentedRawPixels)
                {
                    rawBitmap.InstallPixels(info, (IntPtr)p, (int)width * 4);

                    int targetHeight = (int)Math.Round((double)height * targetWidth / width);
                    if (targetHeight < 1) targetHeight = 1;

                    using var resizedBitmap = rawBitmap.Resize(new SKImageInfo(targetWidth, targetHeight, SKColorType.Rgba8888, SKAlphaType.Premul), SKFilterQuality.Medium);
                    using var image = SKImage.FromBitmap(resizedBitmap ?? rawBitmap);

                    var skFormat = string.Equals(format, "png", StringComparison.OrdinalIgnoreCase)
                        ? SKEncodedImageFormat.Png
                        : SKEncodedImageFormat.Jpeg;

                    using var data = image.Encode(skFormat, Math.Clamp(quality, 30, 100));
                    var result = data.ToArray();

                    lock (_lock)
                    {
                        _cachedFrameBytes = result;
                        _cachedFrameId = _frameCounter;
                        _cachedFrameFormat = format;
                    }

                    return result;
                }
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rentedRawPixels);
        }
    }

    private float _timeScale = 1.0f;

    /// <summary>
    /// Gets or sets the simulation time multiplier (0.05x to 10.0x).
    /// </summary>
    public float TimeScale
    {
        get => _timeScale;
        set => _timeScale = Math.Clamp(value, 0.05f, 10.0f);
    }

    /// <summary>
    /// Dynamically resizes the engine viewport resolution (clamped between 320x240 and 3840x2160).
    /// </summary>
    public void Resize(uint width, uint height)
    {
        EnsureInitialized();
        lock (_lock)
        {
            if (_engineHost?.IsInitialized == true)
            {
                _engineWidth = Math.Clamp(width, 320u, 3840u);
                _engineHeight = Math.Clamp(height, 240u, 2160u);
                _engineHost.ResizeViewport(_engineWidth, _engineHeight);
                _frameCounter++;
            }
        }
    }

    /// <summary>
    /// Sets a deterministic random number generator seed in the engine runtime.
    /// </summary>
    public void SetRngSeed(ulong seed)
    {
        EnsureInitialized();
        lock (_lock)
        {
            if (_engineHost?.IsInitialized == true)
            {
                _engineHost.ExecuteScript($"math.randomseed({seed})");
            }
        }
    }

    /// <summary>
    /// Advances the engine simulation by the specified delta time (multiplied by TimeScale) and tracks frame performance metrics.
    /// </summary>
    public void Step(float deltaTime = 0.01667f)
    {
        EnsureInitialized();

        double frameMs = 0;
        lock (_lock)
        {
            if (_engineHost?.IsInitialized == true)
            {
                float effectiveDt = deltaTime * _timeScale;
                var sw = Stopwatch.StartNew();
                _engineHost.Step(effectiveDt);
                sw.Stop();
                _frameCounter++;

                double texLoad = _engineHost.LastFrameTextureLoadMilliseconds;
                double nonTex = _engineHost.LastFrameNonTextureRenderMilliseconds;
                double textRaster = _engineHost.LastFrameTextRasterizationMilliseconds;
                double flush = _engineHost.LastFrameRendererFlushMilliseconds;
                double engineTotal = texLoad + nonTex + textRaster + flush;
                frameMs = engineTotal > 0.05 ? engineTotal : sw.Elapsed.TotalMilliseconds;
            }
        }

        if (frameMs > 0.01)
        {
            RecordFrameTime(frameMs);
        }
    }

    /// <summary>
    /// Sets the playback mode of the native engine.
    /// </summary>
    public void SetPlayState(bool isPlaying)
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.IsInitialized == true)
            {
                _engineHost.SetPlayState(isPlaying);
            }
        }
    }

    /// <summary>
    /// Retrieves recent log messages from the native engine's ring buffer as JSON.
    /// </summary>
    public string GetRecentLogsJson(int minLevel = 0, int maxCount = 100)
    {
        int bufferCap = 64 * 1024; // 64KB initial buffer
        IntPtr buffer = Marshal.AllocHGlobal(bufferCap);
        try
        {
            int res = NativeBridge.RowlEngine_GetRecentLogsJson(buffer, bufferCap, minLevel, maxCount);
            if (res < 0)
            {
                // Buffer too small, reallocate (-res is required size)
                Marshal.FreeHGlobal(buffer);
                bufferCap = (-res) + 1024;
                buffer = Marshal.AllocHGlobal(bufferCap);
                res = NativeBridge.RowlEngine_GetRecentLogsJson(buffer, bufferCap, minLevel, maxCount);
            }

            if (res > 0 && buffer != IntPtr.Zero)
            {
                return Marshal.PtrToStringUTF8(buffer) ?? "[]";
            }
            return "[]";
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[RowlEyeEngineService] GetRecentLogsJson error: {ex.Message}");
            return "[]";
        }
        finally
        {
            if (buffer != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
    }

    /// <summary>
    /// Queries the active world state, story, character transform, and game state variables as JSON.
    /// </summary>
    public string QueryWorldJson(string? filterJson = null)
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true) return "{}";

            return _engineHost.Runtime.Invoke(handle =>
            {
                int bufferCap = 32 * 1024; // 32KB initial
                IntPtr buffer = Marshal.AllocHGlobal(bufferCap);
                try
                {
                    int res = NativeBridge.RowlEngine_QueryWorldJson(handle, filterJson, buffer, bufferCap);
                    if (res < 0)
                    {
                        // Buffer too small, reallocate
                        Marshal.FreeHGlobal(buffer);
                        bufferCap = (-res) + 512;
                        buffer = Marshal.AllocHGlobal(bufferCap);
                        res = NativeBridge.RowlEngine_QueryWorldJson(handle, filterJson, buffer, bufferCap);
                    }

                    if (res > 0 && buffer != IntPtr.Zero)
                    {
                        return Marshal.PtrToStringUTF8(buffer) ?? "{}";
                    }
                    return "{}";
                }
                finally
                {
                    if (buffer != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            });
        }
    }

    /// <summary>
    /// Injects synthetic/virtual input (taps, choices, advance) directly into the native engine.
    /// </summary>
    public bool InjectInputJson(string inputJson)
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true) return false;

            return _engineHost.Runtime.Invoke(handle =>
            {
                int res = NativeBridge.RowlEngine_InjectInputJson(handle, inputJson);
                return res == 1;
            });
        }
    }

    /// <summary>
    /// Retrieves audio telemetry and performance statistics (channel playback, volumes, RMS, clipping risk) as JSON.
    /// </summary>
    public string GetAudioStatsJson()
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true) return "{}";

            return _engineHost.Runtime.Invoke(handle =>
            {
                int bufferCap = 4 * 1024; // 4KB
                IntPtr buffer = Marshal.AllocHGlobal(bufferCap);
                try
                {
                    int res = NativeBridge.RowlEngine_GetAudioStatsJson(handle, buffer, bufferCap);
                    if (res < 0)
                    {
                        Marshal.FreeHGlobal(buffer);
                        bufferCap = (-res) + 256;
                        buffer = Marshal.AllocHGlobal(bufferCap);
                        res = NativeBridge.RowlEngine_GetAudioStatsJson(handle, buffer, bufferCap);
                    }

                    if (res > 0 && buffer != IntPtr.Zero)
                    {
                        return Marshal.PtrToStringUTF8(buffer) ?? "{}";
                    }
                    return "{}";
                }
                finally
                {
                    if (buffer != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            });
        }
    }

    /// <summary>
    /// Rewinds the simulation by the specified number of steps.
    /// </summary>
    public bool Rewind(uint steps = 1)
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true) return false;

            return _engineHost.Runtime.Invoke(handle =>
            {
                int res = NativeBridge.RowlEngine_Rewind(handle, steps);
                if (res == 1)
                {
                    _cachedFrameBytes = null;
                    return true;
                }
                return false;
            });
        }
    }

    /// <summary>
    /// Evaluates a Lua script string or condition on the engine worker thread.
    /// </summary>
    public ScriptEvalResultDto EvalScript(string code, bool isCondition = false)
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true)
            {
                return new ScriptEvalResultDto(false, null, "Engine runtime not available", -1, 0);
            }

            var sw = Stopwatch.StartNew();
            if (isCondition)
            {
                bool condRes = _engineHost.EvaluateCondition(code);
                sw.Stop();
                int errCode = (int)_engineHost.LastResultCode;
                string errMsg = _engineHost.LastResultMessage;
                bool ok = errCode == 0;
                return new ScriptEvalResultDto(
                    Ok: ok,
                    Result: condRes ? "true" : "false",
                    Error: ok ? null : errMsg,
                    ErrorCode: errCode,
                    ElapsedMs: Math.Round(sw.Elapsed.TotalMilliseconds, 2)
                );
            }
            else
            {
                bool execRes = _engineHost.ExecuteScript(code);
                sw.Stop();
                int errCode = (int)_engineHost.LastResultCode;
                string errMsg = _engineHost.LastResultMessage;
                bool ok = execRes && errCode == 0;
                return new ScriptEvalResultDto(
                    Ok: ok,
                    Result: ok ? "OK" : null,
                    Error: ok ? null : errMsg,
                    ErrorCode: errCode,
                    ElapsedMs: Math.Round(sw.Elapsed.TotalMilliseconds, 2)
                );
            }
        }
    }

    /// <summary>
    /// Queries VFS mount hierarchy, skipped package diagnostics, and optional probe path existence.
    /// </summary>
    public string QueryVfsStatusJson(string? probePath = null)
    {
        EnsureInitialized();

        lock (_lock)
        {
            if (_engineHost?.Runtime?.IsAvailable != true) return "{}";

            return _engineHost.Runtime.Invoke(handle =>
            {
                int bufferCap = 16 * 1024; // 16KB
                IntPtr buffer = Marshal.AllocHGlobal(bufferCap);
                try
                {
                    int res = NativeBridge.RowlEngine_QueryVfsStatusJson(handle, probePath, buffer, bufferCap);
                    if (res < 0)
                    {
                        Marshal.FreeHGlobal(buffer);
                        bufferCap = (-res) + 512;
                        buffer = Marshal.AllocHGlobal(bufferCap);
                        res = NativeBridge.RowlEngine_QueryVfsStatusJson(handle, probePath, buffer, bufferCap);
                    }

                    if (res > 0 && buffer != IntPtr.Zero)
                    {
                        return Marshal.PtrToStringUTF8(buffer) ?? "{}";
                    }
                    return "{}";
                }
                finally
                {
                    if (buffer != IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(buffer);
                    }
                }
            });
        }
    }

    /// <summary>
    /// Scans the project Assets directory and compares against graph node asset references to find unused files.
    /// </summary>
    public DeadAssetsResultDto GetDeadAssets()
    {
        string? projectRoot = ResolveProjectRoot();
        if (string.IsNullOrEmpty(projectRoot))
        {
            return new DeadAssetsResultDto(0, Array.Empty<string>(), 0);
        }

        string assetsDir = Path.Combine(projectRoot, "Assets");
        if (!Directory.Exists(assetsDir))
        {
            return new DeadAssetsResultDto(0, Array.Empty<string>(), 0);
        }

        var storyTexts = new List<string>();
        foreach (var jsonFile in Directory.GetFiles(assetsDir, "*.json", SearchOption.AllDirectories))
        {
            try { storyTexts.Add(File.ReadAllText(jsonFile)); } catch { }
        }
        string combinedGraphText = string.Join(" ", storyTexts);

        var unreferenced = new List<string>();
        long reclaimableBytes = 0;
        int totalScanned = 0;

        var assetFiles = Directory.GetFiles(assetsDir, "*.*", SearchOption.AllDirectories)
            .Where(f => !f.EndsWith(".json", StringComparison.OrdinalIgnoreCase));

        foreach (var file in assetFiles)
        {
            totalScanned++;
            string fileName = Path.GetFileName(file);
            if (!combinedGraphText.Contains(fileName, StringComparison.OrdinalIgnoreCase))
            {
                string relPath = Path.GetRelativePath(projectRoot, file).Replace('\\', '/');
                unreferenced.Add(relPath);
                try { reclaimableBytes += new FileInfo(file).Length; } catch { }
            }
        }

        return new DeadAssetsResultDto(totalScanned, unreferenced, reclaimableBytes);
    }

    public void Dispose()
    {
        lock (_lock)
        {
            if (_isDisposed) return;
            _isDisposed = true;
            _engineHost?.Dispose();
            _engineHost = null;
        }
    }
}
