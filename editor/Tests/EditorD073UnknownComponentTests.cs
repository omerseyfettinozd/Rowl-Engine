using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D07-3 verification: a component type the editor has no view model for must
/// survive open + save. Before D07-3 the loader skipped it with a warning and
/// the saver then wrote the node without it (silent data loss; the samples carry
/// <c>unlock</c> components the editor did not know).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorD073UnknownComponentTests
{
    private static string RepoRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory, "samples", "kayip_tablo")))
                return directory;
            directory = Directory.GetParent(directory)?.FullName;
        }
        throw new Exception("Repository root with samples/ could not be located.");
    }

    /// <summary>A node whose object holds a dialogue plus a component type no editor knows.</summary>
    private const string FutureGraph = """
    {
      "format_version": 4,
      "start_node_id": 1,
      "nodes": [
        {
          "id": 1, "title": "N1", "editor_x": 0, "editor_y": 0,
          "objects": [
            { "id": "o1", "name": "Diyalog", "is_active": true, "components": [
              { "type": "dialogue", "id": "d1", "enabled": true, "data": { "speaker": "A", "text": "hi" } },
              { "type": "weather_fx", "id": "w1", "enabled": false,
                "data": { "mode": "rain", "intensity": 3, "layers": [1, 2, { "k": "v" }], "nested": { "a": [true, null] }, "ratio": 0.25 } }
            ] }
          ],
          "next_nodes": []
        }
      ]
    }
    """;

    private static List<JsonElement> ComponentsOfType(JsonDocument doc, string type)
    {
        var found = new List<JsonElement>();
        foreach (var node in doc.RootElement.GetProperty("nodes").EnumerateArray())
        foreach (var obj in node.GetProperty("objects").EnumerateArray())
        foreach (var comp in obj.GetProperty("components").EnumerateArray())
            if (comp.GetProperty("type").GetString() == type)
                found.Add(comp.Clone());
        return found;
    }

    [Fact]
    public void UnknownComponent_SurvivesLoadAndSerialize_Verbatim()
    {
        using var source = JsonDocument.Parse(FutureGraph);
        var loaded = StoryGraphLoaderService.Load(source);
        Assert.True(loaded.Success);

        string saved = StoryGraphSerializer.SerializeFullStoryGraph(loaded.Nodes, loaded.Connections, 1);
        using var after = JsonDocument.Parse(saved);

        var before = ComponentsOfType(source, "weather_fx").Single();
        var kept = Assert.Single(ComponentsOfType(after, "weather_fx"));
        Assert.Equal("w1", kept.GetProperty("id").GetString());
        Assert.False(kept.GetProperty("enabled").GetBoolean());
        Assert.Equal(before.GetProperty("data").GetRawText().Replace(" ", ""),
                     kept.GetProperty("data").GetRawText().Replace(" ", "").Replace("\n", "").Replace("\r", ""));
    }

    [Fact]
    public void UnknownComponent_SurvivesTheSnapshotSavePath()
    {
        using var source = JsonDocument.Parse(FutureGraph);
        var loaded = StoryGraphLoaderService.Load(source);
        var snapshot = StoryGraphSaveService.Capture(loaded.Nodes, loaded.Connections, 1, null);
        using var after = JsonDocument.Parse(StoryGraphSaveService.SerializeFullGraph(snapshot));
        var kept = Assert.Single(ComponentsOfType(after, "weather_fx"));
        Assert.Equal("rain", kept.GetProperty("data").GetProperty("mode").GetString());
        Assert.Equal(3, kept.GetProperty("data").GetProperty("intensity").GetInt32());
        Assert.Equal(JsonValueKind.Array, kept.GetProperty("data").GetProperty("layers").ValueKind);
    }

    [Fact]
    public void UnknownComponent_KeepsPositionNextToKnownOnes()
    {
        using var source = JsonDocument.Parse(FutureGraph);
        var loaded = StoryGraphLoaderService.Load(source);
        var objects = loaded.Nodes.Single().Objects.Single();
        Assert.Equal(new[] { "dialogue", "weather_fx" }, objects.Components.Select(c => c.TypeKey).ToArray());
    }

    [Fact]
    public void UnknownComponent_DuplicateAndBatchCloneKeepIt()
    {
        using var source = JsonDocument.Parse(FutureGraph);
        var node = StoryGraphLoaderService.Load(source).Nodes.Single();
        var copy = node.DuplicateObject(node.Objects.Single());
        var unknown = copy.Components.Single(c => c.TypeKey == "weather_fx");
        Assert.False(unknown.IsEnabled);
        var data = unknown.Serialize();
        Assert.Equal("rain", ((JsonElement)data["mode"]).GetString());
    }

    [Fact]
    public void SampleKayipTablo_UnlockComponentsSurviveEditorRoundTrip()
    {
        string graphPath = Path.Combine(RepoRoot(), "samples", "kayip_tablo", "Assets", "json", "full_story_graph.json");
        using var source = JsonDocument.Parse(File.ReadAllText(graphPath));
        var originals = ComponentsOfType(source, "unlock");
        Assert.Equal(4, originals.Count);

        var loaded = StoryGraphLoaderService.Load(source);
        Assert.True(loaded.Success);

        // Through the real writer into a throw-away copy (never the repo file).
        string temp = Path.Combine(Path.GetTempPath(), "d073_rt_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(temp, "Assets");
        string json = Path.Combine(assets, "json");
        try
        {
            Directory.CreateDirectory(json);
            Assert.True(StoryGraphDocumentWriter.SaveFullStoryGraph(assets, json, loaded.Nodes, loaded.Connections, 101));
            using var after = JsonDocument.Parse(File.ReadAllText(Path.Combine(json, "full_story_graph.json")));
            var kept = ComponentsOfType(after, "unlock");
            Assert.Equal(4, kept.Count);
            for (int i = 0; i < 4; i++)
            {
                Assert.Equal(originals[i].GetProperty("id").GetString(), kept[i].GetProperty("id").GetString());
                Assert.Equal(originals[i].GetProperty("data").GetProperty("kind").GetString(),
                             kept[i].GetProperty("data").GetProperty("kind").GetString());
                Assert.Equal(originals[i].GetProperty("data").GetProperty("id").GetString(),
                             kept[i].GetProperty("data").GetProperty("id").GetString());
            }
        }
        finally
        {
            try { Directory.Delete(temp, true); } catch { }
        }
    }
}
