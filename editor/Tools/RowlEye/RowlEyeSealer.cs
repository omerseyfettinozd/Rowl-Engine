using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Serialization;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tools.RowlEye;

/// <summary>
/// Manifest metadata schema for sealed Rowl.Eye snapshot runs.
/// </summary>
public sealed class RowlEyeManifest
{
    public string TargetWindow { get; set; } = string.Empty;
    public string TimestampUtc { get; set; } = string.Empty;
    public string GitCommitSha { get; set; } = string.Empty;
    public string AvaloniaVersion { get; set; } = "11.3.11";
    public int LogicalWidth { get; set; }
    public int LogicalHeight { get; set; }
    public double[] Scales { get; set; } = [];
    public string[] GeneratedFiles { get; set; } = [];
}

/// <summary>
/// Seals runtime state into deterministic JSON files alongside visual captures.
/// </summary>
public static class RowlEyeSealer
{
    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        ReferenceHandler = ReferenceHandler.IgnoreCycles
    };

    public static void SealViewModel(object? dataContext, string destinationJsonPath)
    {
        if (dataContext == null) return;
        try
        {
            string json = JsonSerializer.Serialize(dataContext, dataContext.GetType(), JsonOpts);
            File.WriteAllText(destinationJsonPath, json);
        }
        catch
        {
            // If the ViewModel has complex/unserializable cycles (like full MainWindowViewModel),
            // extract a summary viewmodel payload
            var summary = CreateSummaryPayload(dataContext);
            string json = JsonSerializer.Serialize(summary, JsonOpts);
            File.WriteAllText(destinationJsonPath, json);
        }
    }

    private static object CreateSummaryPayload(object dataContext)
    {
        if (dataContext is MainWindowViewModel mainVm)
        {
            return new
            {
                Type = "MainWindowViewModel",
                NodeCount = mainVm.Nodes.Count,
                SelectedNodeCount = mainVm.Nodes.Count(n => n.IsSelected),
                IsProjectDirty = mainVm.IsProjectDirty
            };
        }
        return new { Type = dataContext.GetType().Name, Status = "SealedFallback" };
    }

    public static void SealManifest(RowlEyeManifest manifest, string destinationJsonPath)
    {
        string json = JsonSerializer.Serialize(manifest, JsonOpts);
        File.WriteAllText(destinationJsonPath, json);
    }
}
