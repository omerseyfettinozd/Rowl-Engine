using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using RowlEngine.Editor.Tools.RowlEye.Engine;

namespace RowlEngine.Editor.Tools.RowlEye.Ai;

/// <summary>
/// 360-degree all-inclusive project and runtime comprehension state for AI models.
/// Enables any LLM to grasp the active scene, story progress, asset health, and telemetry in a single payload.
/// </summary>
public sealed record OmniProjectComprehensionDto(
    [property: JsonPropertyName("timestampUtc")] string TimestampUtc,
    [property: JsonPropertyName("narrativeSummary")] string NarrativeSummary,
    [property: JsonPropertyName("projectInfo")] ComprehensionProjectInfoDto ProjectInfo,
    [property: JsonPropertyName("liveScene")] ComprehensionSceneDto LiveScene,
    [property: JsonPropertyName("storyState")] ComprehensionStoryStateDto StoryState,
    [property: JsonPropertyName("diagnosticsAndHealth")] ComprehensionHealthDto DiagnosticsAndHealth,
    [property: JsonPropertyName("recentEvents")] IReadOnlyList<string> RecentEvents,
    [property: JsonPropertyName("availableAiTools")] IReadOnlyList<string> AvailableAiTools
);

public sealed record ComprehensionProjectInfoDto(
    [property: JsonPropertyName("projectName")] string ProjectName,
    [property: JsonPropertyName("projectRoot")] string ProjectRoot,
    [property: JsonPropertyName("totalNodes")] int TotalNodes,
    [property: JsonPropertyName("totalAssets")] int TotalAssets,
    [property: JsonPropertyName("unreferencedAssetCount")] int UnreferencedAssetCount
);

public sealed record ComprehensionSceneDto(
    [property: JsonPropertyName("currentNodeId")] uint CurrentNodeId,
    [property: JsonPropertyName("speaker")] string Speaker,
    [property: JsonPropertyName("dialogue")] string Dialogue,
    [property: JsonPropertyName("background")] string Background,
    [property: JsonPropertyName("character")] string Character,
    [property: JsonPropertyName("isPlaying")] bool IsPlaying,
    [property: JsonPropertyName("frameSha256")] string FrameSha256,
    [property: JsonPropertyName("resolution")] int[] Resolution
);

public sealed record ComprehensionStoryStateDto(
    [property: JsonPropertyName("visitedNodesCount")] int VisitedNodesCount,
    [property: JsonPropertyName("coveragePercent")] double CoveragePercent,
    [property: JsonPropertyName("isAtEnding")] bool IsAtEnding,
    [property: JsonPropertyName("totalEndings")] int TotalEndings,
    [property: JsonPropertyName("reachedEndingsCount")] int ReachedEndingsCount,
    [property: JsonPropertyName("availableChoices")] IReadOnlyList<string> AvailableChoices
);

public sealed record ComprehensionHealthDto(
    [property: JsonPropertyName("overallHealth")] string OverallHealth,
    [property: JsonPropertyName("fpsEstimate")] double FpsEstimate,
    [property: JsonPropertyName("hitchCount")] int HitchCount,
    [property: JsonPropertyName("isUiHung")] bool IsUiHung,
    [property: JsonPropertyName("linterPassed")] bool LinterPassed,
    [property: JsonPropertyName("linterIssueCount")] int LinterIssueCount,
    [property: JsonPropertyName("recentErrors")] IReadOnlyList<string> RecentErrors
);

/// <summary>
/// Service generating omni-comprehension snapshots and OpenAI/MCP function tools for AI models.
/// </summary>
public sealed class RowlEyeComprehensionService
{
    private static readonly Lazy<RowlEyeComprehensionService> InstanceLazy = new(() => new RowlEyeComprehensionService());
    public static RowlEyeComprehensionService Instance => InstanceLazy.Value;

    public OmniProjectComprehensionDto GenerateSnapshot()
    {
        var engineSvc = RowlEyeEngineService.Instance;
        var storySvc = RowlEyeStoryGraphService.Instance;
        var watchdog = RowlEyeWatchdog.Instance;
        var hub = AiTelemetryHub.Instance;

        engineSvc.EnsureInitialized();

        // 1. Live scene query
        string worldJson = engineSvc.QueryWorldJson();
        string spk = "", dlg = "", bg = "", ch = "";
        uint currentNodeId = 0;
        bool isPlaying = false;

        try
        {
            using var doc = JsonDocument.Parse(worldJson);
            var root = doc.RootElement;
            isPlaying = root.TryGetProperty("isPlaying", out var pProp) && pProp.GetBoolean();
            if (root.TryGetProperty("nodeId", out var nidProp)) currentNodeId = nidProp.GetUInt32();

            if (root.TryGetProperty("story", out var st))
            {
                if (st.TryGetProperty("spk", out var sProp)) spk = sProp.GetString() ?? "";
                if (st.TryGetProperty("dlg", out var dProp)) dlg = dProp.GetString() ?? "";
                if (st.TryGetProperty("bg", out var bProp)) bg = bProp.GetString() ?? "";
                if (st.TryGetProperty("ch", out var cProp)) ch = cProp.GetString() ?? "";
            }
        }
        catch { }

        // 2. Story coverage
        var coverage = storySvc.GetCoverage(currentNodeId);

        // 3. Choices
        var choices = new List<string>();
        try
        {
            using var doc = JsonDocument.Parse(worldJson);
            if (doc.RootElement.TryGetProperty("choices", out var chProp) && chProp.ValueKind == JsonValueKind.Array)
            {
                foreach (var c in chProp.EnumerateArray())
                {
                    string label = c.TryGetProperty("label", out var l) ? l.GetString() ?? "" : "";
                    string optId = c.TryGetProperty("optionId", out var o) ? o.GetString() ?? "" : "";
                    choices.Add(string.IsNullOrEmpty(optId) ? label : $"{optId}: {label}");
                }
            }
        }
        catch { }

        // 4. Memory and performance
        var mem = engineSvc.GetMemoryStats();
        var ctx = engineSvc.GetContext();
        string frameSha = engineSvc.LastFrameSha256;
        if (string.IsNullOrEmpty(frameSha))
        {
            frameSha = engineSvc.ComputeFrameSha256(currentNodeId > 0 ? currentNodeId : null);
        }

        // 5. Dead assets and project info
        var deadAssets = engineSvc.GetDeadAssets();
        string projectRoot = Directory.GetCurrentDirectory();
        string projectName = Path.GetFileName(projectRoot) ?? "Rowl Engine Project";

        // 6. Recent logs & errors
        var recentErrors = new List<string>();
        try
        {
            string logsJson = engineSvc.GetRecentLogsJson(minLevel: 2, maxCount: 10);
            using var lDoc = JsonDocument.Parse(logsJson);
            if (lDoc.RootElement.ValueKind == JsonValueKind.Array)
            {
                foreach (var item in lDoc.RootElement.EnumerateArray())
                {
                    string msg = item.TryGetProperty("msg", out var m) ? m.GetString() ?? "" : "";
                    if (!string.IsNullOrEmpty(msg)) recentErrors.Add(msg);
                }
            }
        }
        catch { }

        // 7. Recent telemetry events
        var recentEvents = hub.GetHistory(0).TakeLast(5)
            .Select(e => $"[{e.Type}] {JsonSerializer.Serialize(e.Payload)}")
            .ToList();

        // 8. Natural Language Narrative Summary
        string statusText = isPlaying ? "çalışıyor (oyun modu)" : "duraklatıldı (inceleme modu)";
        string dialogueInfo = string.IsNullOrEmpty(spk) ? "Aktif diyalog yok." : $"{spk}: \"{dlg}\"";
        string narrative = $"Proje '{projectName}' {statusText}. Aktif Düğüm #{currentNodeId}. {dialogueInfo} " +
                           $"Hikaye kapsamı: %{coverage.CoveragePercent} ({coverage.VisitedCount}/{coverage.TotalNodes} düğüm). " +
                           $"FPS: {Math.Round(ctx.FpsEstimate, 1)}, Hitch: {mem.FrameStats.HitchCount}, UI Durumu: {(watchdog.IsHung ? "DONDU" : "Canlı")}.";

        // 9. Overall health verdict
        string overallHealth = "HEALTHY";
        if (watchdog.IsHung || mem.FrameStats.HitchCount > 10 || recentErrors.Count > 3)
        {
            overallHealth = "DEGRADED";
        }

        var availableTools = new[]
        {
            "step_simulation", "play_pause", "select_choice", "rewind_history",
            "mutate_story_graph", "inject_editor_input", "queue_dialog_answer",
            "eval_lua_script", "resize_engine", "set_time_scale", "set_rng_seed",
            "inspect_visual_tree", "get_dead_assets", "run_linter"
        };

        return new OmniProjectComprehensionDto(
            TimestampUtc: DateTime.UtcNow.ToString("o"),
            NarrativeSummary: narrative,
            ProjectInfo: new ComprehensionProjectInfoDto(
                ProjectName: projectName,
                ProjectRoot: projectRoot,
                TotalNodes: coverage.TotalNodes,
                TotalAssets: deadAssets.TotalScannedFiles,
                UnreferencedAssetCount: deadAssets.UnreferencedFiles.Count
            ),
            LiveScene: new ComprehensionSceneDto(
                CurrentNodeId: currentNodeId,
                Speaker: spk,
                Dialogue: dlg,
                Background: bg,
                Character: ch,
                IsPlaying: isPlaying,
                FrameSha256: frameSha,
                Resolution: ctx.Resolution
            ),
            StoryState: new ComprehensionStoryStateDto(
                VisitedNodesCount: coverage.VisitedCount,
                CoveragePercent: coverage.CoveragePercent,
                IsAtEnding: coverage.IsAtEnding,
                TotalEndings: coverage.TotalEndings,
                ReachedEndingsCount: coverage.ReachedEndings.Count,
                AvailableChoices: choices
            ),
            DiagnosticsAndHealth: new ComprehensionHealthDto(
                OverallHealth: overallHealth,
                FpsEstimate: Math.Round(ctx.FpsEstimate, 1),
                HitchCount: mem.FrameStats.HitchCount,
                IsUiHung: watchdog.IsHung,
                LinterPassed: recentErrors.Count == 0,
                LinterIssueCount: recentErrors.Count,
                RecentErrors: recentErrors
            ),
            RecentEvents: recentEvents,
            AvailableAiTools: availableTools
        );
    }

    /// <summary>
    /// Returns the standard OpenAI/MCP compatible function calling tools schema for AI models.
    /// </summary>
    public object GetAiToolsSchema()
    {
        return new
        {
            version = "1.0",
            server = "Rowl.Eye AI Controller",
            tools = new object[]
            {
                new
                {
                    name = "step_simulation",
                    description = "Advances the native engine simulation by delta time in seconds.",
                    parameters = new
                    {
                        type = "object",
                        properties = new { deltaTime = new { type = "number", description = "Delta time (e.g. 0.01667 for 60 FPS)" } }
                    }
                },
                new
                {
                    name = "select_choice",
                    description = "Selects an active dialogue choice branch by its optionId or index.",
                    parameters = new
                    {
                        type = "object",
                        properties = new { optionId = new { type = "string", description = "Target choice option ID" } },
                        required = new[] { "optionId" }
                    }
                },
                new
                {
                    name = "rewind_history",
                    description = "Rewinds the engine state backwards by N steps.",
                    parameters = new
                    {
                        type = "object",
                        properties = new { steps = new { type = "integer", description = "Number of steps to rewind" } }
                    }
                },
                new
                {
                    name = "mutate_story_graph",
                    description = "Adds, deletes, moves, or edits dialogue text of story nodes in the active graph.",
                    parameters = new
                    {
                        type = "object",
                        properties = new
                        {
                            action = new { type = "string", @enum = new[] { "add_node", "remove_node", "move_node", "update_dialogue" } },
                            nodeId = new { type = "integer" },
                            title = new { type = "string" },
                            speaker = new { type = "string" },
                            text = new { type = "string" },
                            editorX = new { type = "number" },
                            editorY = new { type = "number" }
                        },
                        required = new[] { "action" }
                    }
                },
                new
                {
                    name = "queue_dialog_answer",
                    description = "Enqueues an autonomous response for modal dialogs (confirm, file pick, unsaved changes).",
                    parameters = new
                    {
                        type = "object",
                        properties = new
                        {
                            dialogType = new { type = "string", @enum = new[] { "confirm", "file_open", "file_save" } },
                            approve = new { type = "boolean" },
                            selectedPath = new { type = "string" }
                        },
                        required = new[] { "dialogType" }
                    }
                },
                new
                {
                    name = "inspect_visual_tree",
                    description = "Extracts the full hierarchical Avalonia control tree with ZIndex, Opacity, ClipToBounds, and Overlays.",
                    parameters = new
                    {
                        type = "object",
                        properties = new
                        {
                            target = new { type = "string", description = "Window target (e.g. MainWindow or Live)" },
                            includeHidden = new { type = "boolean" }
                        }
                    }
                }
            }
        };
    }
}
