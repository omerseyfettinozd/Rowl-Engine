using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace RowlEngine.Editor.Tools.RowlEye.Engine;

/// <summary>
/// Combined quality and health report aggregating engine performance, memory, audio, VFS, and story coverage.
/// </summary>
public sealed record RowlEyeAggregateReportDto(
    [property: JsonPropertyName("timestampUtc")] string TimestampUtc,
    [property: JsonPropertyName("overallHealth")] string OverallHealth,
    [property: JsonPropertyName("healthSummary")] string HealthSummary,
    [property: JsonPropertyName("engineContext")] EngineContextDto EngineContext,
    [property: JsonPropertyName("memoryStats")] EngineMemoryStatsDto MemoryStats,
    [property: JsonPropertyName("audioStats")] JsonElement AudioStats,
    [property: JsonPropertyName("storyCoverage")] StoryCoverageDto StoryCoverage,
    [property: JsonPropertyName("nodeFrameHashes")] IReadOnlyDictionary<uint, string> NodeFrameHashes,
    [property: JsonPropertyName("vfsStatus")] JsonElement VfsStatus,
    [property: JsonPropertyName("recentErrors")] IReadOnlyList<string> RecentErrors
);

/// <summary>
/// Facade service aggregating multiple telemetry channels into a single unified report.
/// </summary>
public sealed class RowlEyeReportAggregator
{
    private static readonly Lazy<RowlEyeReportAggregator> InstanceLazy = new(() => new RowlEyeReportAggregator());
    public static RowlEyeReportAggregator Instance => InstanceLazy.Value;

    public RowlEyeAggregateReportDto GenerateReport(uint currentNodeId = 0)
    {
        var engineSvc = RowlEyeEngineService.Instance;
        var storySvc = RowlEyeStoryGraphService.Instance;

        engineSvc.EnsureInitialized();

        var engineCtx = engineSvc.GetContext();
        var memStats = engineSvc.GetMemoryStats();
        var coverage = storySvc.GetCoverage(currentNodeId);
        var frameHashes = engineSvc.NodeFrameHashes;

        JsonElement audioElem;
        try
        {
            string audioJson = engineSvc.GetAudioStatsJson();
            using var doc = JsonDocument.Parse(audioJson);
            audioElem = doc.RootElement.Clone();
        }
        catch
        {
            using var doc = JsonDocument.Parse("{\"available\":false}");
            audioElem = doc.RootElement.Clone();
        }

        JsonElement vfsElem;
        try
        {
            string vfsJson = engineSvc.QueryVfsStatusJson("Assets/json/full_story_graph.json");
            using var doc = JsonDocument.Parse(vfsJson);
            vfsElem = doc.RootElement.Clone();
        }
        catch
        {
            using var doc = JsonDocument.Parse("{\"mountCount\":0}");
            vfsElem = doc.RootElement.Clone();
        }

        var recentErrors = new List<string>();
        try
        {
            string logsJson = engineSvc.GetRecentLogsJson(minLevel: 2, maxCount: 20); // warnings & errors
            using var doc = JsonDocument.Parse(logsJson);
            if (doc.RootElement.ValueKind == JsonValueKind.Array)
            {
                foreach (var item in doc.RootElement.EnumerateArray())
                {
                    string msg = item.TryGetProperty("msg", out var m) ? m.GetString() ?? "" : "";
                    if (!string.IsNullOrEmpty(msg)) recentErrors.Add(msg);
                }
            }
        }
        catch { }

        // Evaluate overall health
        string overallHealth = "HEALTHY";
        var summaryParts = new List<string>();

        if (memStats.FrameStats.HitchCount > 10)
        {
            overallHealth = "DEGRADED";
            summaryParts.Add($"High hitch count: {memStats.FrameStats.HitchCount}");
        }

        if (audioElem.TryGetProperty("metrics", out var metProp) && metProp.TryGetProperty("clippingRisk", out var clipProp) && clipProp.GetBoolean())
        {
            overallHealth = "DEGRADED";
            summaryParts.Add("Audio clipping risk detected");
        }

        if (recentErrors.Count > 0)
        {
            summaryParts.Add($"{recentErrors.Count} warning(s)/error(s) in log");
        }

        if (summaryParts.Count == 0)
        {
            summaryParts.Add($"All systems normal. Coverage: {coverage.CoveragePercent}%, P95 frame: {memStats.FrameStats.P95Ms}ms");
        }

        return new RowlEyeAggregateReportDto(
            TimestampUtc: DateTime.UtcNow.ToString("o"),
            OverallHealth: overallHealth,
            HealthSummary: string.Join("; ", summaryParts),
            EngineContext: engineCtx,
            MemoryStats: memStats,
            AudioStats: audioElem,
            StoryCoverage: coverage,
            NodeFrameHashes: frameHashes,
            VfsStatus: vfsElem,
            RecentErrors: recentErrors
        );
    }
}
