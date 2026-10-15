using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using RowlEngine.Editor.Tools.RowlEye.Ai;
using RowlEngine.Editor.Tools.RowlEye.Engine;
using RowlEngine.Editor.Tools.RowlEye.Goldens;
using RowlEngine.Editor.Tools.RowlEye.Targets;

namespace RowlEngine.Editor.Tools.RowlEye.Serve;

/// <summary>
/// Lightweight, zero-dependency HTTP server providing live web inspector capabilities for Rowl.Eye.
/// </summary>
public sealed class RowlEyeWebServer : IAsyncDisposable, IDisposable
{
    private static readonly string[] AllowedTargets =
    [
        nameof(RowlEyeTarget.MainWindow),
        nameof(RowlEyeTarget.SettingsDialog),
        nameof(RowlEyeTarget.NodeGraph),
        nameof(RowlEyeTarget.Hierarchy),
        nameof(RowlEyeTarget.Inspector),
        nameof(RowlEyeTarget.AssetBrowser),
        nameof(RowlEyeTarget.LivePreview)
    ];

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    private readonly HttpListener _listener = new();
    private readonly RowlEyeServeOptions _opts;
    private readonly RowlEyeGoldenManager _goldenManager;
    private readonly SemaphoreSlim _renderThrottle = new(2, 2);
    private CancellationTokenSource? _cts;
    private Task? _listenerLoop;
    private string? _cachedHtml;
    private int _boundPort;

    public int BoundPort => _boundPort;
    public string BaseUrl => $"http://localhost:{_boundPort}/";

    public RowlEyeWebServer(RowlEyeServeOptions? opts = null)
    {
        _opts = opts ?? new RowlEyeServeOptions();
        _goldenManager = new RowlEyeGoldenManager(_opts.GoldensRoot);
        _boundPort = _opts.Port;
    }

    public Task StartAsync(CancellationToken ct = default)
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();

        if (_listener.IsListening) return Task.CompletedTask;

        if (_boundPort == 0)
        {
            _boundPort = GetFreePort();
        }

        _listener.Prefixes.Clear();
        _listener.Prefixes.Add($"http://localhost:{_boundPort}/");

        try
        {
            _listener.Start();
        }
        catch (HttpListenerException)
        {
            // Port might be in use, fallback to free port
            _boundPort = GetFreePort();
            _listener.Prefixes.Clear();
            _listener.Prefixes.Add($"http://localhost:{_boundPort}/");
            _listener.Start();
        }

        _cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        _listenerLoop = Task.Run(() => AcceptConnectionsAsync(_cts.Token), CancellationToken.None);

        try
        {
            RowlEyeEngineService.Instance.HookLogCallback();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[Rowl.Eye.WebServer] HookLogCallback warning: {ex.Message}");
        }

        Console.WriteLine($"[Rowl.Eye.WebServer] Online at {BaseUrl}");
        return Task.CompletedTask;
    }

    private static int GetFreePort()
    {
        using var tcp = new TcpListener(IPAddress.Loopback, 0);
        tcp.Start();
        return ((IPEndPoint)tcp.LocalEndpoint).Port;
    }

    private async Task AcceptConnectionsAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            HttpListenerContext ctx;
            try
            {
                ctx = await _listener.GetContextAsync().WaitAsync(ct).ConfigureAwait(false);
            }
            catch (OperationCanceledException) { break; }
            catch (HttpListenerException) when (ct.IsCancellationRequested) { break; }
            catch (ObjectDisposedException) { break; }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"[Rowl.Eye.WebServer] Accept error: {ex.Message}");
                continue;
            }

            _ = Task.Run(() => HandleRequestSafelyAsync(ctx, ct), ct);
        }
    }

    private async Task HandleRequestSafelyAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        try
        {
            await ProcessRequestAsync(ctx, ct).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            try
            {
                await WriteErrorAsync(ctx, 500, ex.Message, "INTERNAL_ERROR", ct).ConfigureAwait(false);
            }
            catch
            {
                ctx.Response.Abort();
            }
        }
    }

    private async Task ProcessRequestAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var req = ctx.Request;
        var path = req.Url?.AbsolutePath.TrimEnd('/').ToLowerInvariant() ?? "";

        if (path is "" or "/index.html" or "/inspector.html")
        {
            await ServeInspectorHtmlAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/targets" && req.HttpMethod == "GET")
        {
            await WriteJsonAsync(ctx, AllowedTargets, ct).ConfigureAwait(false);
        }
        else if (path == "/api/snapshot" && req.HttpMethod == "GET")
        {
            await HandleSnapshotAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/viewmodel" && req.HttpMethod == "GET")
        {
            await HandleViewModelAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/visual-tree" && req.HttpMethod == "GET")
        {
            await HandleVisualTreeAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/golden-image" && req.HttpMethod == "GET")
        {
            await HandleGoldenImageAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/diff-image" && req.HttpMethod == "GET")
        {
            await HandleDiffImageAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/audit" && req.HttpMethod == "GET")
        {
            await HandleAuditAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/goldens/update" && (req.HttpMethod == "POST" || req.HttpMethod == "GET"))
        {
            await HandleGoldenUpdateAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/comprehension" && req.HttpMethod == "GET")
        {
            await HandleAiComprehensionAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/tools" && req.HttpMethod == "GET")
        {
            await HandleAiToolsAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/context" && req.HttpMethod == "GET")
        {
            await HandleAiContextAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/focus" && req.HttpMethod == "POST")
        {
            await HandleAiFocusAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/agent-stream" && req.HttpMethod == "GET")
        {
            await HandleAiAgentStreamAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/agent-state" && req.HttpMethod == "GET")
        {
            await HandleAiAgentStateAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/context" && req.HttpMethod == "GET")
        {
            await HandleEngineContextAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/memory_stats" && req.HttpMethod == "GET")
        {
            await HandleEngineMemoryStatsAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/framebuffer" && req.HttpMethod == "GET")
        {
            await HandleEngineFramebufferAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/playback" && req.HttpMethod == "POST")
        {
            await HandleEnginePlaybackAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/logs/recent" && req.HttpMethod == "GET")
        {
            await HandleAiLogsRecentAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/world/query" && req.HttpMethod == "GET")
        {
            await HandleAiWorldQueryAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/input/inject" && req.HttpMethod == "POST")
        {
            await HandleAiInputInjectAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/audio/stats" && req.HttpMethod == "GET")
        {
            await HandleAiAudioStatsAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/history/rewind" && req.HttpMethod == "POST")
        {
            await HandleAiHistoryRewindAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/story/graph" && req.HttpMethod == "GET")
        {
            await HandleAiStoryGraphAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/story/coverage" && req.HttpMethod == "GET")
        {
            await HandleAiStoryCoverageAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/story/drive" && req.HttpMethod == "POST")
        {
            await HandleAiStoryDriveAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/story/mutate" && req.HttpMethod == "POST")
        {
            await HandleAiStoryMutateAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/report/aggregate" && req.HttpMethod == "GET")
        {
            await HandleAiReportAggregateAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/editor/dialog_answer" && req.HttpMethod == "POST")
        {
            await HandleAiEditorDialogAnswerAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/editor/dialog_answers" && req.HttpMethod == "GET")
        {
            await HandleAiEditorDialogAnswersListAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/editor/undo_redo" && req.HttpMethod == "GET")
        {
            await HandleAiEditorUndoRedoAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/editor/inject" && req.HttpMethod == "POST")
        {
            await HandleAiEditorInjectAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/vfs/status" && req.HttpMethod == "GET")
        {
            await HandleAiVfsStatusAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/vfs/dead_assets" && req.HttpMethod == "GET")
        {
            await HandleAiVfsDeadAssetsAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/project/lint" && req.HttpMethod == "GET")
        {
            await HandleAiProjectLintAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/script/eval" && req.HttpMethod == "POST")
        {
            await HandleAiScriptEvalAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/resize" && req.HttpMethod == "POST")
        {
            await HandleEngineResizeAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/time_scale" && req.HttpMethod == "POST")
        {
            await HandleEngineTimeScaleAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/engine/rng_seed" && req.HttpMethod == "POST")
        {
            await HandleEngineRngSeedAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/api/v1/ai/watchdog/status" && req.HttpMethod == "GET")
        {
            await HandleWatchdogStatusAsync(ctx, ct).ConfigureAwait(false);
        }
        else if (path == "/favicon.ico")
        {
            ctx.Response.StatusCode = 204;
            ctx.Response.Close();
        }
        else
        {
            await WriteErrorAsync(ctx, 404, "Endpoint not found", "NOT_FOUND", ct).ConfigureAwait(false);
        }
    }

    private async Task HandleSnapshotAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "MainWindow");
        string theme = q.GetValueOrDefault("theme", "Dark");
        double scale = ParseScale(q.GetValueOrDefault("scale", "1.0"));

        if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
        {
            await WriteErrorAsync(ctx, 400, $"Unknown target '{targetName}'", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        byte[] png = await CaptureTargetPngAsync(target, theme, scale).ConfigureAwait(false);

        ctx.Response.ContentType = "image/png";
        ctx.Response.ContentLength64 = png.Length;
        ctx.Response.Headers.Add("Cache-Control", "no-cache, no-store, must-revalidate");
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(png, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private async Task HandleViewModelAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "MainWindow");
        string theme = q.GetValueOrDefault("theme", "Dark");

        if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
        {
            await WriteErrorAsync(ctx, 400, $"Unknown target '{targetName}'", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        object? vm = await Dispatcher.UIThread.InvokeAsync(() =>
        {
            var (_, viewModel, _) = RowlEyeFixtureFactory.Create(target, theme);
            return viewModel;
        });

        await WriteJsonAsync(ctx, vm ?? new { message = "No ViewModel associated with target" }, ct).ConfigureAwait(false);
    }

    private async Task HandleVisualTreeAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "MainWindow");
        string theme = q.GetValueOrDefault("theme", "Dark");

        bool includeHidden = !bool.TryParse(q.GetValueOrDefault("include_hidden", "true"), out var ih) || ih;
        bool live = bool.TryParse(q.GetValueOrDefault("live", "false"), out var lv) && lv;

        Inspection.VisualNodeDto? tree = await Dispatcher.UIThread.InvokeAsync(() =>
        {
            if (live || targetName.Equals("Live", StringComparison.OrdinalIgnoreCase))
            {
                if (Avalonia.Application.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime desktop &&
                    desktop.MainWindow != null)
                {
                    return Inspection.VisualTreeService.Extract(desktop.MainWindow, includeHidden);
                }
            }

            if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
            {
                return null;
            }

            var (window, _, _) = RowlEyeFixtureFactory.Create(target, theme);
            try
            {
                window.Show();
                Dispatcher.UIThread.RunJobs();
                return Inspection.VisualTreeService.Extract(window, includeHidden);
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        });

        if (tree == null)
        {
            await WriteErrorAsync(ctx, 400, $"Unable to inspect target '{targetName}' (live window not found or invalid target)", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        await WriteJsonAsync(ctx, tree, ct).ConfigureAwait(false);
    }

    private async Task HandleGoldenImageAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "MainWindow");
        string theme = q.GetValueOrDefault("theme", "Dark");
        double scale = ParseScale(q.GetValueOrDefault("scale", "1.0"));

        if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
        {
            await WriteErrorAsync(ctx, 400, $"Unknown target '{targetName}'", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        string goldenPath = _goldenManager.GetGoldenPath(target, theme, scale);
        if (!File.Exists(goldenPath))
        {
            await WriteErrorAsync(ctx, 404, $"Golden master not found for target '{target}'", "NEEDS_BASELINE", ct).ConfigureAwait(false);
            return;
        }

        byte[] bytes = await File.ReadAllBytesAsync(goldenPath, ct).ConfigureAwait(false);
        ctx.Response.ContentType = "image/png";
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private async Task HandleDiffImageAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "MainWindow");
        string theme = q.GetValueOrDefault("theme", "Dark");
        double scale = ParseScale(q.GetValueOrDefault("scale", "1.0"));

        if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
        {
            await WriteErrorAsync(ctx, 400, $"Unknown target '{targetName}'", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        string goldenPath = _goldenManager.GetGoldenPath(target, theme, scale);
        if (!File.Exists(goldenPath))
        {
            await WriteErrorAsync(ctx, 404, "No golden baseline exists to generate diff heatmap.", "NEEDS_BASELINE", ct).ConfigureAwait(false);
            return;
        }

        // Capture live to temp file
        string tempActual = Path.Combine(Path.GetTempPath(), $"rowl_diff_act_{Guid.NewGuid():N}.png");
        string tempDiff = Path.Combine(Path.GetTempPath(), $"rowl_diff_out_{Guid.NewGuid():N}.png");

        try
        {
            byte[] liveBytes = await CaptureTargetPngAsync(target, theme, scale).ConfigureAwait(false);
            await File.WriteAllBytesAsync(tempActual, liveBytes, ct).ConfigureAwait(false);

            var threshold = RowlEyeThreshold.ForTarget(target, false);
            var diffResult = RowlEyeDiffer.Compare(goldenPath, tempActual, tempDiff, threshold.Tolerance, threshold.MaxMismatchPercent, threshold.IgnoreRegions);

            if (File.Exists(tempDiff))
            {
                byte[] diffBytes = await File.ReadAllBytesAsync(tempDiff, ct).ConfigureAwait(false);
                ctx.Response.ContentType = "image/png";
                ctx.Response.ContentLength64 = diffBytes.Length;
                ctx.Response.StatusCode = 200;
                await ctx.Response.OutputStream.WriteAsync(diffBytes, ct).ConfigureAwait(false);
                ctx.Response.Close();
            }
            else
            {
                // Identical, return live
                ctx.Response.ContentType = "image/png";
                ctx.Response.ContentLength64 = liveBytes.Length;
                ctx.Response.StatusCode = 200;
                await ctx.Response.OutputStream.WriteAsync(liveBytes, ct).ConfigureAwait(false);
                ctx.Response.Close();
            }
        }
        finally
        {
            try { if (File.Exists(tempActual)) File.Delete(tempActual); } catch { }
            try { if (File.Exists(tempDiff)) File.Delete(tempDiff); } catch { }
        }
    }

    private async Task HandleAuditAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string theme = q.GetValueOrDefault("theme", "Dark");
        double scale = ParseScale(q.GetValueOrDefault("scale", "1.0"));

        var targets = new[]
        {
            RowlEyeTarget.MainWindow,
            RowlEyeTarget.SettingsDialog,
            RowlEyeTarget.NodeGraph,
            RowlEyeTarget.Hierarchy,
            RowlEyeTarget.Inspector,
            RowlEyeTarget.AssetBrowser,
            RowlEyeTarget.LivePreview
        };

        var auditResults = new List<object>();

        foreach (var target in targets)
        {
            string goldenPath = _goldenManager.GetGoldenPath(target, theme, scale);
            if (!File.Exists(goldenPath))
            {
                auditResults.Add(new
                {
                    target = target.ToString(),
                    theme,
                    scale,
                    status = "NeedsBaseline",
                    diffPercent = 0.0,
                    thresholdPercent = 0.5,
                    mismatchedPixels = 0,
                    message = "Altın referans dosyası bulunamadı."
                });
                continue;
            }

            string tempActual = Path.Combine(Path.GetTempPath(), $"rowl_audit_act_{Guid.NewGuid():N}.png");
            try
            {
                byte[] liveBytes = await CaptureTargetPngAsync(target, theme, scale).ConfigureAwait(false);
                await File.WriteAllBytesAsync(tempActual, liveBytes, ct).ConfigureAwait(false);

                var threshold = RowlEyeThreshold.ForTarget(target, false);
                var comp = RowlEyeDiffer.Compare(goldenPath, tempActual, null, threshold.Tolerance, threshold.MaxMismatchPercent, threshold.IgnoreRegions);

                auditResults.Add(new
                {
                    target = target.ToString(),
                    theme,
                    scale,
                    status = comp.Passed ? "Passed" : "Failed",
                    diffPercent = comp.MismatchPercentage,
                    thresholdPercent = threshold.MaxMismatchPercent,
                    mismatchedPixels = comp.MismatchedPixels,
                    message = comp.Message
                });
            }
            finally
            {
                try { if (File.Exists(tempActual)) File.Delete(tempActual); } catch { }
            }
        }

        await WriteJsonAsync(ctx, auditResults, ct).ConfigureAwait(false);
    }

    private async Task HandleGoldenUpdateAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "");
        string theme = q.GetValueOrDefault("theme", "Dark");
        double scale = ParseScale(q.GetValueOrDefault("scale", "1.0"));

        if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
        {
            await WriteErrorAsync(ctx, 400, "Geçerli bir 'target' parametresi belirtilmelidir.", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        string tempCaptured = Path.Combine(Path.GetTempPath(), $"rowl_promote_{Guid.NewGuid():N}.png");
        try
        {
            byte[] liveBytes = await CaptureTargetPngAsync(target, theme, scale).ConfigureAwait(false);
            await File.WriteAllBytesAsync(tempCaptured, liveBytes, ct).ConfigureAwait(false);

            _goldenManager.UpdateGolden(target, theme, scale, tempCaptured);
            string savedPath = _goldenManager.GetGoldenPath(target, theme, scale);
            await WriteJsonAsync(ctx, new { success = true, goldenPath = savedPath, target = target.ToString(), theme, scale }, ct).ConfigureAwait(false);
        }
        finally
        {
            try { if (File.Exists(tempCaptured)) File.Delete(tempCaptured); } catch { }
        }
    }

    private async Task<byte[]> CaptureTargetPngAsync(RowlEyeTarget target, string theme, double scale)
    {
        await _renderThrottle.WaitAsync().ConfigureAwait(false);
        try
        {
            return await Dispatcher.UIThread.InvokeAsync(() =>
            {
                var (window, _, _) = RowlEyeFixtureFactory.Create(target, theme);
                try
                {
                    window.Show();
                    Dispatcher.UIThread.RunJobs();
                    return RowlEyeRenderer.RenderWindowToBytes(window, scale);
                }
                finally
                {
                    window.Close();
                    Dispatcher.UIThread.RunJobs();
                }
            });
        }
        finally
        {
            _renderThrottle.Release();
        }
    }

    private async Task ServeInspectorHtmlAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        _cachedHtml ??= LoadInspectorHtml();
        byte[] bytes = Encoding.UTF8.GetBytes(_cachedHtml);

        ctx.Response.ContentType = "text/html; charset=utf-8";
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private string LoadInspectorHtml()
    {
        var candidatePaths = new[]
        {
            _opts.InspectorHtmlPath,
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Tools", "RowlEye", "Serve", "inspector.html"),
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "inspector.html"),
            Path.Combine(Directory.GetCurrentDirectory(), "editor", "Tools", "RowlEye", "Serve", "inspector.html")
        };

        foreach (var p in candidatePaths)
        {
            if (!string.IsNullOrWhiteSpace(p) && File.Exists(p))
            {
                return File.ReadAllText(p, Encoding.UTF8);
            }
        }

        // Try Embedded Resource
        var asm = Assembly.GetExecutingAssembly();
        var resourceName = asm.GetManifestResourceNames()
            .FirstOrDefault(n => n.EndsWith("inspector.html", StringComparison.OrdinalIgnoreCase));

        if (resourceName != null)
        {
            using var stream = asm.GetManifestResourceStream(resourceName);
            if (stream != null)
            {
                using var reader = new StreamReader(stream, Encoding.UTF8);
                return reader.ReadToEnd();
            }
        }

        throw new FileNotFoundException("inspector.html could not be located on disk or embedded resource.");
    }

    private static double ParseScale(string? raw)
    {
        if (double.TryParse(raw, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double s))
        {
            return Math.Clamp(s, 0.5, 3.0);
        }
        return 1.0;
    }

    private static Dictionary<string, string> ParseQuery(string? query)
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (string.IsNullOrEmpty(query)) return map;

        foreach (var pair in query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries))
        {
            var kv = pair.Split('=', 2);
            string k = Uri.UnescapeDataString(kv[0]);
            string v = kv.Length > 1 ? Uri.UnescapeDataString(kv[1]) : "";
            map[k] = v;
        }
        return map;
    }

    private static async Task WriteJsonAsync<T>(HttpListenerContext ctx, T payload, CancellationToken ct, int statusCode = 200)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(payload, JsonOpts);
        ctx.Response.ContentType = "application/json; charset=utf-8";
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.StatusCode = statusCode;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private static Task WriteErrorAsync(HttpListenerContext ctx, int statusCode, string message, string code, CancellationToken ct)
    {
        return WriteJsonAsync(ctx, new { error = message, code, status = statusCode }, ct, statusCode);
    }

    private static readonly HashSet<string> AllowedActions = new(StringComparer.OrdinalIgnoreCase)
    {
        "highlight", "focus", "inspect", "click", "click_test", "type_test", "finding", "done", "hover"
    };

    private async Task HandleAiContextAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string targetName = q.GetValueOrDefault("target", "MainWindow");
        string theme = q.GetValueOrDefault("theme", "Dark");
        double scale = ParseScale(q.GetValueOrDefault("scale", "1.0"));
        string sessionId = q.GetValueOrDefault("sessionId", "session_agent");

        if (!Enum.TryParse<RowlEyeTarget>(targetName, true, out var target))
        {
            await WriteErrorAsync(ctx, 400, $"Unknown target '{targetName}'", "BAD_TARGET", ct).ConfigureAwait(false);
            return;
        }

        AiContextDto context = await Dispatcher.UIThread.InvokeAsync(() =>
        {
            var (window, _, _) = RowlEyeFixtureFactory.Create(target, theme);
            try
            {
                window.Show();
                Dispatcher.UIThread.RunJobs();
                return AiContextBuilder.Build(window, target.ToString(), theme, scale, sessionId);
            }
            finally
            {
                window.Close();
                Dispatcher.UIThread.RunJobs();
            }
        });

        await WriteJsonAsync(ctx, context, ct).ConfigureAwait(false);
    }

    private async Task HandleAiFocusAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
        string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);

        AiFocusRequest? req;
        try
        {
            req = JsonSerializer.Deserialize<AiFocusRequest>(body, JsonOpts);
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid JSON payload: {ex.Message}", "BAD_JSON", ct).ConfigureAwait(false);
            return;
        }

        if (req == null || string.IsNullOrWhiteSpace(req.Target) || string.IsNullOrWhiteSpace(req.ControlPath))
        {
            await WriteErrorAsync(ctx, 400, "Target and ControlPath are required.", "INVALID_REQUEST", ct).ConfigureAwait(false);
            return;
        }

        if (!AllowedActions.Contains(req.Action))
        {
            await WriteErrorAsync(ctx, 400, $"Action '{req.Action}' is not supported. Allowed: {string.Join(", ", AllowedActions)}", "INVALID_ACTION", ct).ConfigureAwait(false);
            return;
        }

        var evt = AiTelemetryHub.Instance.RecordFocus(req);
        await WriteJsonAsync(ctx, new { status = "ok", seq = evt.Seq, timestamp = evt.Timestamp }, ct).ConfigureAwait(false);
    }

    private async Task HandleAiAgentStateAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        long since = 0;
        if (q.TryGetValue("since", out var sinceStr))
        {
            _ = long.TryParse(sinceStr, out since);
        }

        var state = AiTelemetryHub.Instance.CurrentState;
        var history = AiTelemetryHub.Instance.GetHistory(since);

        await WriteJsonAsync(ctx, new
        {
            state,
            history
        }, ct).ConfigureAwait(false);
    }

    private async Task HandleAiAgentStreamAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var res = ctx.Response;
        res.ContentType = "text/event-stream";
        res.Headers.Add("Cache-Control", "no-cache, no-transform");
        res.Headers.Add("Connection", "keep-alive");
        res.Headers.Add("Access-Control-Allow-Origin", "*");
        res.StatusCode = 200;

        var (subId, reader) = AiTelemetryHub.Instance.Subscribe();
        using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(ct);

        try
        {
            // 1. Initial snapshot of current state
            var cur = AiTelemetryHub.Instance.CurrentState;
            string initJson = JsonSerializer.Serialize(cur, JsonOpts);
            byte[] initBytes = Encoding.UTF8.GetBytes($"event: state\ndata: {initJson}\n\n");
            await res.OutputStream.WriteAsync(initBytes, linkedCts.Token).ConfigureAwait(false);
            await res.OutputStream.FlushAsync(linkedCts.Token).ConfigureAwait(false);

            // 2. Loop with heartbeat
            while (!linkedCts.Token.IsCancellationRequested)
            {
                var readTask = reader.WaitToReadAsync(linkedCts.Token).AsTask();
                var delayTask = Task.Delay(15000, linkedCts.Token);

                var completed = await Task.WhenAny(readTask, delayTask).ConfigureAwait(false);
                if (completed == readTask && await readTask.ConfigureAwait(false))
                {
                    while (reader.TryRead(out var evt))
                    {
                        string eventType = evt.Type.ToString();
                        string payloadJson = JsonSerializer.Serialize(evt.Payload, JsonOpts);
                        byte[] eventBytes = Encoding.UTF8.GetBytes($"id: {evt.Seq}\nevent: {eventType}\ndata: {payloadJson}\n\n");
                        await res.OutputStream.WriteAsync(eventBytes, linkedCts.Token).ConfigureAwait(false);
                    }
                    await res.OutputStream.FlushAsync(linkedCts.Token).ConfigureAwait(false);
                }
                else
                {
                    // Send heartbeat
                    byte[] pingBytes = Encoding.UTF8.GetBytes(": ping\n\n");
                    await res.OutputStream.WriteAsync(pingBytes, linkedCts.Token).ConfigureAwait(false);
                    await res.OutputStream.FlushAsync(linkedCts.Token).ConfigureAwait(false);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (HttpListenerException) { }
        catch (IOException) { }
        finally
        {
            AiTelemetryHub.Instance.Unsubscribe(subId);
            try { res.Close(); } catch { }
        }
    }

    private async Task HandleEngineContextAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var context = RowlEyeEngineService.Instance.GetContext();
        await WriteJsonAsync(ctx, context, ct).ConfigureAwait(false);
    }

    private async Task HandleEngineFramebufferAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        int width = 640;
        if (q.TryGetValue("width", out var wStr) && int.TryParse(wStr, out var parsedW))
        {
            width = Math.Clamp(parsedW, 160, 1920);
        }
        string format = q.GetValueOrDefault("format", "jpeg");
        int quality = 80;
        if (q.TryGetValue("quality", out var qStr) && int.TryParse(qStr, out var parsedQ))
        {
            quality = Math.Clamp(parsedQ, 20, 100);
        }

        byte[]? bytes = RowlEyeEngineService.Instance.CaptureFrame(width, format, quality);
        if (bytes == null || bytes.Length == 0)
        {
            await WriteErrorAsync(ctx, 503, "Native engine framebuffer is not available yet.", "FRAME_NOT_READY", ct).ConfigureAwait(false);
            return;
        }

        string contentType = string.Equals(format, "png", StringComparison.OrdinalIgnoreCase) ? "image/png" : "image/jpeg";
        ctx.Response.ContentType = contentType;
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private async Task HandleEnginePlaybackAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
        string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);

        using var doc = JsonDocument.Parse(body);
        var root = doc.RootElement;
        string cmd = root.TryGetProperty("cmd", out var cmdProp) ? cmdProp.GetString()?.ToLowerInvariant() ?? "" : "";

        if (cmd == "play")
        {
            RowlEyeEngineService.Instance.SetPlayState(true);
            AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "engine_play", detail = "Native motor oynatma moduna alındı." });
        }
        else if (cmd == "pause")
        {
            RowlEyeEngineService.Instance.SetPlayState(false);
            AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "engine_pause", detail = "Native motor duraklatıldı." });
        }
        else if (cmd == "step")
        {
            int frames = root.TryGetProperty("frames", out var fProp) && fProp.TryGetInt32(out var f) ? Math.Clamp(f, 1, 60) : 1;
            float dt = root.TryGetProperty("deltaTime", out var dtProp) && dtProp.TryGetSingle(out var parsedDt) ? parsedDt : 0.01667f;

            for (int i = 0; i < frames; i++)
            {
                RowlEyeEngineService.Instance.Step(dt);
            }
            AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "engine_step", detail = $"Native motor {frames} kare simüle edildi (dt={dt}s)." });
        }
        else
        {
            await WriteErrorAsync(ctx, 400, $"Unknown engine command '{cmd}'. Allowed: play, pause, step.", "INVALID_COMMAND", ct).ConfigureAwait(false);
            return;
        }

        var context = RowlEyeEngineService.Instance.GetContext();
        await WriteJsonAsync(ctx, new { status = "ok", cmd, context }, ct).ConfigureAwait(false);
    }

    private async Task HandleAiLogsRecentAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        int minLevel = int.TryParse(q.GetValueOrDefault("minLevel", "0"), out var ml) ? ml : 0;
        int limit = int.TryParse(q.GetValueOrDefault("limit", "100"), out var lim) ? lim : 100;

        string json = RowlEyeEngineService.Instance.GetRecentLogsJson(minLevel, limit);
        byte[] bytes = Encoding.UTF8.GetBytes(json);

        ctx.Response.ContentType = "application/json; charset=utf-8";
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.Headers.Add("Cache-Control", "no-cache, no-store, must-revalidate");
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private async Task HandleAiWorldQueryAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        bool compact = !string.Equals(q.GetValueOrDefault("compact", "true"), "false", StringComparison.OrdinalIgnoreCase);
        bool anomalies = !string.Equals(q.GetValueOrDefault("anomalies", "true"), "false", StringComparison.OrdinalIgnoreCase);

        var filterObj = new { compact, anomalies };
        string filterJson = JsonSerializer.Serialize(filterObj);

        string json = RowlEyeEngineService.Instance.QueryWorldJson(filterJson);
        try
        {
            using var doc = JsonDocument.Parse(json);
            if (doc.RootElement.TryGetProperty("nodeId", out var nidProp) && nidProp.TryGetUInt32(out var nid) && nid > 0)
            {
                RowlEyeStoryGraphService.Instance.RecordVisit(nid);
            }
        }
        catch { }

        byte[] bytes = Encoding.UTF8.GetBytes(json);

        ctx.Response.ContentType = "application/json; charset=utf-8";
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.Headers.Add("Cache-Control", "no-cache, no-store, must-revalidate");
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private async Task HandleAiInputInjectAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
        string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);

        if (string.IsNullOrWhiteSpace(body))
        {
            await WriteErrorAsync(ctx, 400, "Empty input JSON body", "BAD_REQUEST", ct).ConfigureAwait(false);
            return;
        }

        try
        {
            using var doc = JsonDocument.Parse(body);
            if (doc.RootElement.TryGetProperty("optionId", out var optProp))
            {
                string? opt = optProp.GetString();
                if (!string.IsNullOrEmpty(opt))
                {
                    RowlEyeStoryGraphService.Instance.RecordChoice(opt);
                }
            }
        }
        catch { }

        bool ok = RowlEyeEngineService.Instance.InjectInputJson(body);
        if (!ok)
        {
            await WriteErrorAsync(ctx, 500, "Failed to inject input into engine", "INJECT_FAILED", ct).ConfigureAwait(false);
            return;
        }

        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "input_inject", body });
        await WriteJsonAsync(ctx, new { status = "ok", message = "Input injected successfully" }, ct).ConfigureAwait(false);
    }

    private async Task HandleEngineMemoryStatsAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var stats = RowlEyeEngineService.Instance.GetMemoryStats();
        await WriteJsonAsync(ctx, stats, ct).ConfigureAwait(false);
    }

    private async Task HandleAiStoryCoverageAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        uint currentNodeId = 0;
        try
        {
            var q = ParseQuery(ctx.Request.Url?.Query);
            if (uint.TryParse(q.GetValueOrDefault("currentNodeId", "0"), out var nid))
            {
                currentNodeId = nid;
            }
        }
        catch { }

        var coverage = RowlEyeStoryGraphService.Instance.GetCoverage(currentNodeId);
        await WriteJsonAsync(ctx, coverage, ct).ConfigureAwait(false);
    }

    private async Task HandleAiStoryDriveAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string body = await ReadBodyAsStringAsync(ctx.Request, ct).ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(body))
        {
            await WriteErrorAsync(ctx, 400, "Scenario body cannot be empty", "BAD_REQUEST", ct).ConfigureAwait(false);
            return;
        }

        ScenarioDriveRequestDto? req;
        try
        {
            req = JsonSerializer.Deserialize<ScenarioDriveRequestDto>(body);
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid scenario JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        var result = RowlEyeStoryGraphService.Instance.ExecuteScenario(req ?? new ScenarioDriveRequestDto());
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "story_drive", scenario = result.ScenarioName, success = result.Success, steps = result.ExecutedSteps });
        await WriteJsonAsync(ctx, result, ct).ConfigureAwait(false);
    }

    private async Task HandleAiStoryMutateAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string body = await ReadBodyAsStringAsync(ctx.Request, ct).ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(body))
        {
            await WriteErrorAsync(ctx, 400, "Mutation body cannot be empty", "BAD_REQUEST", ct).ConfigureAwait(false);
            return;
        }

        StoryMutateRequestDto? req;
        try
        {
            req = JsonSerializer.Deserialize<StoryMutateRequestDto>(body);
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid mutation JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        if (req == null || string.IsNullOrWhiteSpace(req.Action))
        {
            await WriteErrorAsync(ctx, 400, "Mutation 'action' is required", "BAD_REQUEST", ct).ConfigureAwait(false);
            return;
        }

        var result = RowlEyeStoryGraphService.Instance.MutateStoryGraph(req);
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "story_mutate", mutation = req.Action, nodeId = result.NodeId, success = result.Success });
        await WriteJsonAsync(ctx, result, ct).ConfigureAwait(false);
    }

    private async Task HandleAiReportAggregateAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        uint currentNodeId = 0;
        try
        {
            var q = ParseQuery(ctx.Request.Url?.Query);
            if (uint.TryParse(q.GetValueOrDefault("currentNodeId", "0"), out var nid))
            {
                currentNodeId = nid;
            }
        }
        catch { }

        var report = RowlEyeReportAggregator.Instance.GenerateReport(currentNodeId);
        await WriteJsonAsync(ctx, report, ct).ConfigureAwait(false);
    }

    private async Task HandleAiEditorDialogAnswerAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string body = await ReadBodyAsStringAsync(ctx.Request, ct).ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(body))
        {
            await WriteErrorAsync(ctx, 400, "Dialog answer body cannot be empty", "BAD_REQUEST", ct).ConfigureAwait(false);
            return;
        }

        DialogAnswerDto? answer;
        try
        {
            answer = JsonSerializer.Deserialize<DialogAnswerDto>(body);
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid dialog answer JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        if (answer != null)
        {
            RowlEyeEditorBridge.Instance.QueueDialogAnswer(answer);
        }

        await WriteJsonAsync(ctx, new { status = "ok", message = "Dialog answer enqueued" }, ct).ConfigureAwait(false);
    }

    private async Task HandleAiEditorDialogAnswersListAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var answers = RowlEyeEditorBridge.Instance.GetPendingDialogAnswers();
        await WriteJsonAsync(ctx, new { status = "ok", count = answers.Count, answers }, ct).ConfigureAwait(false);
    }

    private async Task HandleAiAudioStatsAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string json = RowlEyeEngineService.Instance.GetAudioStatsJson();
        byte[] bytes = Encoding.UTF8.GetBytes(json);

        ctx.Response.ContentType = "application/json; charset=utf-8";
        ctx.Response.ContentLength64 = bytes.Length;
        ctx.Response.Headers.Add("Cache-Control", "no-cache, no-store, must-revalidate");
        ctx.Response.StatusCode = 200;
        await ctx.Response.OutputStream.WriteAsync(bytes, ct).ConfigureAwait(false);
        ctx.Response.Close();
    }

    private async Task HandleAiHistoryRewindAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
        string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);

        uint steps = 1;
        if (!string.IsNullOrWhiteSpace(body))
        {
            try
            {
                using var doc = JsonDocument.Parse(body);
                if (doc.RootElement.TryGetProperty("steps", out var sProp) && sProp.TryGetUInt32(out var s))
                {
                    steps = Math.Clamp(s, 1u, 50u);
                }
            }
            catch { }
        }

        bool success = RowlEyeEngineService.Instance.Rewind(steps);
        if (!success)
        {
            await WriteErrorAsync(ctx, 400, "Rewind failed (no history available or engine uninitialized)", "REWIND_FAILED", ct).ConfigureAwait(false);
            return;
        }

        string worldJson = RowlEyeEngineService.Instance.QueryWorldJson();
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "rewind", steps });

        using var worldDoc = JsonDocument.Parse(worldJson);
        await WriteJsonAsync(ctx, new { status = "ok", steps, world = worldDoc.RootElement }, ct).ConfigureAwait(false);
    }

    private async Task HandleAiStoryGraphAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        uint currentNodeId = 0;
        try
        {
            string worldJson = RowlEyeEngineService.Instance.QueryWorldJson();
            using var worldDoc = JsonDocument.Parse(worldJson);
            if (worldDoc.RootElement.TryGetProperty("nodeId", out var nidProp) && nidProp.TryGetUInt32(out var nid))
            {
                currentNodeId = nid;
            }
        }
        catch { }

        var topology = RowlEyeStoryGraphService.Instance.GetTopology(currentNodeId);
        await WriteJsonAsync(ctx, topology, ct).ConfigureAwait(false);
    }

    private async Task HandleAiEditorUndoRedoAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var status = await RowlEyeEditorBridge.Instance.GetUndoRedoStatusAsync().ConfigureAwait(false);
        await WriteJsonAsync(ctx, status, ct).ConfigureAwait(false);
    }

    private async Task HandleAiEditorInjectAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string body = await ReadBodyAsStringAsync(ctx.Request, ct).ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(body))
        {
            await WriteErrorAsync(ctx, 400, "Body must contain action or actions array", "EMPTY_BODY", ct).ConfigureAwait(false);
            return;
        }

        var actions = new List<EditorInputActionDto>();
        try
        {
            using var doc = JsonDocument.Parse(body);
            var root = doc.RootElement;
            if (root.ValueKind == JsonValueKind.Array)
            {
                var parsedList = JsonSerializer.Deserialize<List<EditorInputActionDto>>(body);
                if (parsedList != null) actions.AddRange(parsedList);
            }
            else if (root.ValueKind == JsonValueKind.Object)
            {
                if (root.TryGetProperty("actions", out var actsProp) && actsProp.ValueKind == JsonValueKind.Array)
                {
                    var parsedList = JsonSerializer.Deserialize<List<EditorInputActionDto>>(actsProp.GetRawText());
                    if (parsedList != null) actions.AddRange(parsedList);
                }
                else
                {
                    var single = JsonSerializer.Deserialize<EditorInputActionDto>(body);
                    if (single != null) actions.Add(single);
                }
            }
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        var results = await RowlEyeEditorBridge.Instance.InjectActionsAsync(actions).ConfigureAwait(false);
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "editor_inject", count = actions.Count });
        await WriteJsonAsync(ctx, new { status = "ok", count = actions.Count, results }, ct).ConfigureAwait(false);
    }

    private static async Task<string> ReadBodyAsStringAsync(HttpListenerRequest req, CancellationToken ct)
    {
        using var reader = new StreamReader(req.InputStream, Encoding.UTF8);
        return await reader.ReadToEndAsync(ct).ConfigureAwait(false);
    }

    private async Task HandleAiVfsStatusAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var q = ParseQuery(ctx.Request.Url?.Query);
        string? probePath = q.TryGetValue("path", out var pVal) ? pVal : null;
        string vfsJson = RowlEyeEngineService.Instance.QueryVfsStatusJson(probePath);

        using var doc = JsonDocument.Parse(vfsJson);
        await WriteJsonAsync(ctx, doc.RootElement, ct).ConfigureAwait(false);
    }

    private async Task HandleAiScriptEvalAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string body = await ReadBodyAsStringAsync(ctx.Request, ct).ConfigureAwait(false);
        if (string.IsNullOrWhiteSpace(body))
        {
            await WriteErrorAsync(ctx, 400, "Body cannot be empty", "EMPTY_BODY", ct).ConfigureAwait(false);
            return;
        }

        string code = "";
        bool isCondition = false;
        try
        {
            using var doc = JsonDocument.Parse(body);
            var root = doc.RootElement;
            if (root.TryGetProperty("code", out var cProp))
            {
                code = cProp.GetString() ?? "";
            }
            if (root.TryGetProperty("isCondition", out var isCondProp))
            {
                isCondition = isCondProp.GetBoolean();
            }
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        if (string.IsNullOrWhiteSpace(code))
        {
            await WriteErrorAsync(ctx, 400, "'code' field is required", "MISSING_CODE", ct).ConfigureAwait(false);
            return;
        }

        var result = RowlEyeEngineService.Instance.EvalScript(code, isCondition);
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "script_eval", isCondition, ok = result.Ok });
        await WriteJsonAsync(ctx, result, ct).ConfigureAwait(false);
    }

    private async Task HandleEngineResizeAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        uint width = 1280;
        uint height = 720;
        try
        {
            using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
            string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(body))
            {
                using var doc = JsonDocument.Parse(body);
                var root = doc.RootElement;
                if (root.TryGetProperty("width", out var wProp)) width = (uint)Math.Clamp(wProp.GetInt32(), 320, 3840);
                if (root.TryGetProperty("height", out var hProp)) height = (uint)Math.Clamp(hProp.GetInt32(), 240, 2160);
            }
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid resize JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        RowlEyeEngineService.Instance.Resize(width, height);
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "engine_resize", width, height });
        await WriteJsonAsync(ctx, new { success = true, width, height }, ct).ConfigureAwait(false);
    }

    private async Task HandleEngineTimeScaleAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        float scale = 1.0f;
        try
        {
            using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
            string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(body))
            {
                using var doc = JsonDocument.Parse(body);
                var root = doc.RootElement;
                if (root.TryGetProperty("scale", out var sProp)) scale = (float)sProp.GetDouble();
            }
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid time_scale JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        RowlEyeEngineService.Instance.TimeScale = scale;
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "engine_time_scale", scale = RowlEyeEngineService.Instance.TimeScale });
        await WriteJsonAsync(ctx, new { success = true, timeScale = RowlEyeEngineService.Instance.TimeScale }, ct).ConfigureAwait(false);
    }

    private async Task HandleEngineRngSeedAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        ulong seed = 0;
        try
        {
            using var reader = new StreamReader(ctx.Request.InputStream, Encoding.UTF8);
            string body = await reader.ReadToEndAsync(ct).ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(body))
            {
                using var doc = JsonDocument.Parse(body);
                var root = doc.RootElement;
                if (root.TryGetProperty("seed", out var sProp)) seed = sProp.GetUInt64();
            }
        }
        catch (Exception ex)
        {
            await WriteErrorAsync(ctx, 400, $"Invalid rng_seed JSON: {ex.Message}", "INVALID_JSON", ct).ConfigureAwait(false);
            return;
        }

        RowlEyeEngineService.Instance.SetRngSeed(seed);
        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.ActionExecuted, new { action = "engine_rng_seed", seed });
        await WriteJsonAsync(ctx, new { success = true, seed }, ct).ConfigureAwait(false);
    }

    private async Task HandleWatchdogStatusAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var watchdog = Engine.RowlEyeWatchdog.Instance;
        var status = new
        {
            isHung = watchdog.IsHung,
            hungCount = watchdog.HungCount,
            lastHeartbeatAgeMs = Math.Round(watchdog.LastHeartbeatAgeMs, 1),
            hangThresholdMs = watchdog.HangThresholdMs
        };
        await WriteJsonAsync(ctx, status, ct).ConfigureAwait(false);
    }

    private async Task HandleAiVfsDeadAssetsAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var result = RowlEyeEngineService.Instance.GetDeadAssets();
        await WriteJsonAsync(ctx, result, ct).ConfigureAwait(false);
    }

    private async Task HandleAiProjectLintAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        string? projectRoot = null;
        var dir = new DirectoryInfo(Directory.GetCurrentDirectory());
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "project.rowlproj")) ||
                Directory.Exists(Path.Combine(dir.FullName, "Assets")))
            {
                projectRoot = dir.FullName;
                break;
            }
            dir = dir.Parent;
        }

        var issues = new List<object>();
        if (!string.IsNullOrEmpty(projectRoot))
        {
            string assetsDir = Path.Combine(projectRoot, "Assets");
            string graphFile = Path.Combine(assetsDir, "full_story_graph.json");
            if (!File.Exists(graphFile)) graphFile = Path.Combine(assetsDir, "json", "full_story_graph.json");

            if (File.Exists(graphFile))
            {
                try
                {
                    string json = File.ReadAllText(graphFile);
                    using var doc = JsonDocument.Parse(json);
                    var root = doc.RootElement;
                    if (root.TryGetProperty("nodes", out var nodesProp))
                    {
                        foreach (var n in nodesProp.EnumerateArray())
                        {
                            uint id = n.TryGetProperty("id", out var idProp) ? idProp.GetUInt32() : 0;
                            if (n.TryGetProperty("objects", out var objs) && objs.ValueKind == JsonValueKind.Array)
                            {
                                foreach (var obj in objs.EnumerateArray())
                                {
                                    if (obj.TryGetProperty("components", out var comps) && comps.ValueKind == JsonValueKind.Array)
                                    {
                                        foreach (var comp in comps.EnumerateArray())
                                        {
                                            if (comp.TryGetProperty("data", out var data) && data.TryGetProperty("texture", out var texProp))
                                            {
                                                string? tex = texProp.GetString();
                                                if (!string.IsNullOrEmpty(tex))
                                                {
                                                    string texPath = Path.Combine(assetsDir, tex);
                                                    if (!File.Exists(texPath) && !File.Exists(Path.Combine(assetsDir, "textures", tex)))
                                                    {
                                                        issues.Add(new { severity = "WARNING", rule = "MISSING_TEXTURE", nodeId = id, detail = $"Texture '{tex}' referenced in node #{id} not found on disk" });
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    issues.Add(new { severity = "ERROR", rule = "PARSE_ERROR", detail = ex.Message });
                }
            }
        }

        var response = new
        {
            passed = issues.Count == 0,
            issueCount = issues.Count,
            issues
        };
        await WriteJsonAsync(ctx, response, ct).ConfigureAwait(false);
    }

    private async Task HandleAiComprehensionAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var snapshot = Ai.RowlEyeComprehensionService.Instance.GenerateSnapshot();
        await WriteJsonAsync(ctx, snapshot, ct).ConfigureAwait(false);
    }

    private async Task HandleAiToolsAsync(HttpListenerContext ctx, CancellationToken ct)
    {
        var tools = Ai.RowlEyeComprehensionService.Instance.GetAiToolsSchema();
        await WriteJsonAsync(ctx, tools, ct).ConfigureAwait(false);
    }

    public void Dispose()
    {
        try { _cts?.Cancel(); } catch { }
        try { _listener.Stop(); } catch { }
        try { _listener.Close(); } catch { }
        _cts?.Dispose();
        _renderThrottle.Dispose();
    }

    public async ValueTask DisposeAsync()
    {
        Dispose();
        if (_listenerLoop != null)
        {
            try { await _listenerLoop.ConfigureAwait(false); } catch { }
        }
    }
}
