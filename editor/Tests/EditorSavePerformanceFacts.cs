using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

// MS-2: 200-node synthetic graph. Verifies the snapshot save path round-trips,
// writes only the canonical location, never lets a stale sequence overwrite a
// newer one, and stays schema-identical to the live serializer.
public sealed class EditorSavePerformanceFacts
{
    private const int NodeCount = 200;

    private static (ObservableCollection<NodeViewModel> Nodes,
        ObservableCollection<ConnectionViewModel> Conns) BuildGraph()
    {
        var nodes = new ObservableCollection<NodeViewModel>();
        var conns = new ObservableCollection<ConnectionViewModel>();
        for (ulong i = 0; i < NodeCount; i++)
        {
            var node = new NodeViewModel(101 + i, $"Perf Node {101 + i}",
                (i % 20) * 120.0, (i / 20) * 140.0, bare: true);
            var holder = node.CreateObject("Dialogue");
            var dlg = new DialogueComponentViewModel { DialogueText = $"Line {101 + i}" };
            holder.AddComponent(dlg);
            nodes.Add(node);
        }
        // Linear chain plus a choice shortcut every 25 nodes.
        for (int i = 0; i < nodes.Count - 1; i++)
            conns.Add(new ConnectionViewModel(nodes[i], nodes[i + 1]));
        for (int i = 0; i < nodes.Count - 25; i += 25)
        {
            var holder = nodes[i].CreateObject("Choices");
            var choice = new ChoiceComponentViewModel();
            holder.AddComponent(choice);
            choice.Options[0].TargetNodeId = nodes[i + 25].Id;
            conns.Add(new ConnectionViewModel(nodes[i], nodes[i + 25], choice.Options[0].OptionId));
        }
        return (nodes, conns);
    }

    private static void JsonElementsEqual(JsonElement expected, JsonElement actual, string path)
    {
        Assert.Equal(expected.ValueKind, actual.ValueKind);
        switch (expected.ValueKind)
        {
            case JsonValueKind.Object:
                var eProps = expected.EnumerateObject().ToDictionary(p => p.Name);
                var aProps = actual.EnumerateObject().ToDictionary(p => p.Name);
                Assert.Equal(eProps.Keys.OrderBy(k => k), aProps.Keys.OrderBy(k => k));
                foreach (var key in eProps.Keys)
                    JsonElementsEqual(eProps[key].Value, aProps[key].Value, $"{path}.{key}");
                break;
            case JsonValueKind.Array:
                var eArr = expected.EnumerateArray().ToList();
                var aArr = actual.EnumerateArray().ToList();
                Assert.Equal(eArr.Count, aArr.Count);
                for (int i = 0; i < eArr.Count; i++)
                    JsonElementsEqual(eArr[i], aArr[i], $"{path}[{i}]");
                break;
            case JsonValueKind.Number:
                Assert.Equal(expected.GetRawText(), actual.GetRawText());
                break;
            case JsonValueKind.String:
                Assert.Equal(expected.GetString(), actual.GetString());
                break;
            case JsonValueKind.True:
            case JsonValueKind.False:
                Assert.Equal(expected.GetBoolean(), actual.GetBoolean());
                break;
        }
    }

    [Fact]
    public void SnapshotSave_RoundTrips200NodesToCanonicalPathOnly()
    {
        var (nodes, conns) = BuildGraph();
        string root = Path.Combine(Path.GetTempPath(), "RowlMs2_" + Guid.NewGuid().ToString("N"));
        string jsonDir = Path.Combine(root, "Assets", "json");
        try
        {
            var snapshot = StoryGraphSaveService.Capture(nodes, conns, 101, nodes[0]);
            Assert.Equal(NodeCount, snapshot.Nodes.Count);

            // A stale pre-MS-2 legacy copy must be removed by the save; only
            // the canonical address may survive it.
            Directory.CreateDirectory(Path.Combine(root, "Assets"));
            File.WriteAllText(Path.Combine(root, "Assets", "full_story_graph.json"), "{stale}");

            long seq = 1;
            Assert.True(StoryGraphSaveService.TryWriteSnapshot(
                snapshot, jsonDir, seq, () => seq));

            string canonical = Path.Combine(jsonDir, "full_story_graph.json");
            string legacy = Path.Combine(root, "Assets", "full_story_graph.json");
            Assert.True(File.Exists(canonical));
            Assert.True(File.Exists(Path.Combine(jsonDir, "active_story.json")));
            Assert.False(File.Exists(legacy));

            Assert.True(StoryGraphDocumentReader.TryRead(
                Path.Combine(root, "Assets"), jsonDir,
                out var doc, out var filePath, out var error));
            Assert.NotNull(doc);
            using (doc)
            {
                Assert.Equal(NodeCount, doc!.RootElement.GetProperty("nodes").GetArrayLength());
                Assert.Equal(canonical, filePath);
            }
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    [Fact]
    public void SnapshotSave_StaleSequenceNeverOverwritesNewer()
    {
        var (nodes, conns) = BuildGraph();
        string root = Path.Combine(Path.GetTempPath(), "RowlMs2Seq_" + Guid.NewGuid().ToString("N"));
        string jsonDir = Path.Combine(root, "Assets", "json");
        try
        {
            long latest = 2;
            var newer = StoryGraphSaveService.Capture(nodes, conns, 101, nodes[0]);
            Assert.True(StoryGraphSaveService.TryWriteSnapshot(newer, jsonDir, 2, () => latest));
            string afterNewer = File.ReadAllText(Path.Combine(jsonDir, "full_story_graph.json"));

            // An older scheduled save finishing late must abort without touching disk.
            nodes[0].Title = "MUTATED AFTER CAPTURE";
            var older = StoryGraphSaveService.Capture(nodes, conns, 101, nodes[0]);
            Assert.False(StoryGraphSaveService.TryWriteSnapshot(older, jsonDir, 1, () => latest));
            Assert.Equal(afterNewer, File.ReadAllText(Path.Combine(jsonDir, "full_story_graph.json")));
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    [Fact]
    public void SnapshotSave_MatchesLiveSerializerSchema()
    {
        var (nodes, conns) = BuildGraph();
        string liveFull = StoryGraphSerializer.SerializeFullStoryGraph(nodes, conns, 101);
        var snapshot = StoryGraphSaveService.Capture(nodes, conns, 101, nodes[5]);
        string snapFull = StoryGraphSaveService.SerializeFullGraph(snapshot);
        using (var liveDoc = JsonDocument.Parse(liveFull))
        using (var snapDoc = JsonDocument.Parse(snapFull))
        {
            JsonElementsEqual(liveDoc.RootElement, snapDoc.RootElement, "$");
        }

        string liveActive = StoryGraphSerializer.SerializeActiveStory(nodes[5]);
        string? snapActive = StoryGraphSaveService.SerializeActiveStory(snapshot);
        Assert.NotNull(snapActive);
        using (var liveDoc = JsonDocument.Parse(liveActive))
        using (var snapDoc = JsonDocument.Parse(snapActive!))
        {
            JsonElementsEqual(liveDoc.RootElement, snapDoc.RootElement, "$.active");
        }
    }

    [Fact]
    public async Task SnapshotSave_RunsOffCallingThread()
    {
        var (nodes, conns) = BuildGraph();
        string root = Path.Combine(Path.GetTempPath(), "RowlMs2Bg_" + Guid.NewGuid().ToString("N"));
        string jsonDir = Path.Combine(root, "Assets", "json");
        try
        {
            int callingThread = Environment.CurrentManagedThreadId;
            int workerThread = callingThread;
            var snapshot = StoryGraphSaveService.Capture(nodes, conns, 101, nodes[0]);
            long seq = 1;
            await Task.Run(() =>
            {
                workerThread = Environment.CurrentManagedThreadId;
                Assert.True(StoryGraphSaveService.TryWriteSnapshot(snapshot, jsonDir, seq, () => seq));
            });
            Assert.NotEqual(callingThread, workerThread);
            Assert.True(File.Exists(Path.Combine(jsonDir, "full_story_graph.json")));
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }
}
