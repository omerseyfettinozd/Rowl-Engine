using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C04 — daha yeni bir Rowl sürümünün yazdığı hikâye grafiği (format_version &gt;
/// mevcut) kayıplı yüklenip v5 olarak ezilmez: açılış reddedilir, dosyaya
/// hiçbir kayıt yolu (elle kaydet, otomatik kayıt, kurtarma) dokunmaz.
/// Eski ve sürümsüz (legacy) dosyalar eskisi gibi yüklenir.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorStoryGraphNewerFormatTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "RowlNewerFmt_" + Guid.NewGuid().ToString("N"));
    private string AssetsPath => Path.Combine(_root, "Assets");
    private string JsonPath => Path.Combine(AssetsPath, "json");
    private string GraphFile => Path.Combine(JsonPath, "full_story_graph.json");

    public EditorStoryGraphNewerFormatTests() => Directory.CreateDirectory(JsonPath);

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch (Exception) { }
    }

    private static string Graph(string? version) => $$"""
        {
          {{(version is null ? "" : $"\"format_version\": {version},")}}
          "start_node_id": 1,
          "future_only_field": { "keep": "me" },
          "nodes": [
            { "id": 1, "title": "A", "editor_x": 0, "editor_y": 0,
              "objects": [ { "id": "o", "name": "O", "is_active": true,
                "components": [ { "type": "hologram_from_the_future", "id": "h", "enabled": true, "data": { "x": 1 } } ] } ],
              "next_nodes": [] }
          ]
        }
        """;

    private bool Load(string json, List<string> log, out ObservableCollection<NodeViewModel> nodes)
    {
        File.WriteAllText(GraphFile, json);
        nodes = new ObservableCollection<NodeViewModel>();
        return StoryGraphLifecycleCoordinator.LoadGraphWithRollback(
            AssetsPath, JsonPath, nodes, new ObservableCollection<ConnectionViewModel>(),
            (_, _) => { }, () => { }, _ => { }, log.Add, null, log.Add);
    }

    [Fact]
    public void NewerFormat_LoadIsRefused_WithUserMessage_AndFileUntouched()
    {
        string json = Graph("6");
        var messages = new List<string>();
        byte[] before;
        File.WriteAllText(GraphFile, json);
        before = File.ReadAllBytes(GraphFile);

        bool ok = Load(json, messages, out var nodes);

        Assert.False(ok);
        Assert.Empty(nodes);
        Assert.Contains(messages, m => m.Contains("daha yeni bir Rowl sürümüyle", StringComparison.Ordinal) && m.Contains("v6", StringComparison.Ordinal));
        Assert.Equal(before, File.ReadAllBytes(GraphFile));
    }

    [Fact]
    public void NewerFormat_NoSavePath_OverwritesTheFile()
    {
        File.WriteAllText(GraphFile, Graph("6"));
        byte[] before = File.ReadAllBytes(GraphFile);
        var log = new List<string>();
        var node = new NodeViewModel(1, "A", 0, 0, bare: true);
        var nodes = new[] { node };
        var conns = Array.Empty<ConnectionViewModel>();

        // Elle kaydet (eşzamanlı yol)
        Assert.False(StoryGraphLifecycleCoordinator.SaveFullGraph(AssetsPath, JsonPath, nodes, conns, 1, log.Add));
        Assert.Equal(before, File.ReadAllBytes(GraphFile));

        // Otomatik kayıt (arka plan anlık görüntüsü)
        var snapshot = StoryGraphSaveService.Capture(nodes, conns, 1, null);
        Assert.False(StoryGraphSaveService.TryWriteSnapshot(snapshot, JsonPath, 1, () => 1, log.Add));
        Assert.False(CrashRecoveryService.TryWriteSnapshotWithRecovery(snapshot, JsonPath, 1, () => 1, log.Add));
        Assert.Equal(before, File.ReadAllBytes(GraphFile));

        // Kurtarma geri yükleme
        var offer = new RestoreOffer("test", Graph("5"), 5, 1);
        Assert.False(CrashRecoveryService.RestoreOfferToCanonical(JsonPath, offer));
        Assert.Equal(before, File.ReadAllBytes(GraphFile));
        Assert.Contains(log, m => m.Contains("daha yeni bir Rowl sürümüyle", StringComparison.Ordinal));
    }

    [Fact]
    public void NewerLegacyCopy_IsNotDeletedBySave()
    {
        string legacy = Path.Combine(AssetsPath, "full_story_graph.json");
        File.WriteAllText(legacy, Graph("7"));
        byte[] before = File.ReadAllBytes(legacy);
        var nodes = new[] { new NodeViewModel(1, "A", 0, 0, bare: true) };

        Assert.False(StoryGraphLifecycleCoordinator.SaveFullGraph(
            AssetsPath, JsonPath, nodes, Array.Empty<ConnectionViewModel>(), 1));
        Assert.Equal(before, File.ReadAllBytes(legacy));
        Assert.False(File.Exists(GraphFile));
    }

    [Theory]
    [InlineData("5")]
    [InlineData("4")]
    [InlineData(null)]
    public void CurrentOlderAndLegacy_StillLoad_AndSave(string? version)
    {
        var messages = new List<string>();
        Assert.True(Load(Graph(version), messages, out var nodes));
        Assert.Single(nodes);
        Assert.True(StoryGraphLifecycleCoordinator.SaveFullGraph(
            AssetsPath, JsonPath, nodes, Array.Empty<ConnectionViewModel>(), 1));
    }

    [Fact]
    public void Loader_ReportsNewerFormatAsError()
    {
        using var doc = JsonDocument.Parse(Graph("6"));
        var result = StoryGraphLoaderService.Load(doc);
        Assert.False(result.Success);
        Assert.Equal(6, result.FormatVersion);
        Assert.Empty(result.Nodes);
        Assert.False(string.IsNullOrEmpty(result.ErrorMessage));
    }
}
