using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

public sealed class EditorGraphVNextTests
{
    private const string ValidV5Graph = """
        {
          "format_version": 5,
          "start_node_id": 101,
          "nodes": [
            {
              "id": 101, "title": "Prologue", "editor_x": 60, "editor_y": 80,
              "chapter_id": "ch1", "objects": [], "next_nodes": [ { "id": 102 } ]
            },
            {
              "id": 102, "title": "Trial", "editor_x": 340, "editor_y": 80,
              "chapter_id": "ch1", "objects": [], "next_nodes": [ { "id": 103 } ]
            },
            {
              "id": 103, "title": "Epilogue", "editor_x": 620, "editor_y": 80,
              "chapter_id": "ch2", "objects": [], "next_nodes": []
            }
          ],
          "groups": [
            {
              "id": "g1", "title": "Act framing", "color": "#3B82F6",
              "x": 40, "y": 40, "width": 700, "height": 200, "node_ids": [101, 102]
            }
          ],
          "subgraphs": [
            {
              "id": "sg1", "title": "Trial flow",
              "entry_node_id": 101, "exit_node_ids": [102], "node_ids": [101, 102]
            }
          ],
          "chapters": [
            { "id": "ch1", "title": "Arrivals", "order": 0, "summary": "Meet the town." },
            { "id": "ch2", "title": "Departures", "order": 1, "summary": "Leave at dawn.", "start_node_id": 103 }
          ]
        }
        """;

    [Fact]
    public void Serialize_WithoutStructure_WritesStableV4()
    {
        using var doc = JsonDocument.Parse(ValidV5Graph);
        var loaded = StoryGraphLoaderService.Load(doc);
        Assert.True(loaded.Success);

        var plain = loaded.Nodes.Select(node => { node.ChapterId = string.Empty; return node; }).ToList();
        string json = StoryGraphSerializer.SerializeFullStoryGraph(
            plain, loaded.Connections, 101, new GraphStructureDocument());

        using var rewritten = JsonDocument.Parse(json);
        Assert.Equal(4, rewritten.RootElement.GetProperty("format_version").GetInt32());
        Assert.False(rewritten.RootElement.TryGetProperty("groups", out _));
        Assert.False(rewritten.RootElement.TryGetProperty("subgraphs", out _));
        Assert.False(rewritten.RootElement.TryGetProperty("chapters", out _));
        Assert.False(rewritten.RootElement.GetProperty("nodes")[0].TryGetProperty("chapter_id", out _));
    }

    [Fact]
    public void Serialize_WithStructure_WritesV5_AndRoundTrips()
    {
        using var doc = JsonDocument.Parse(ValidV5Graph);
        var loaded = StoryGraphLoaderService.Load(doc);
        Assert.True(loaded.Success);

        string json = StoryGraphSerializer.SerializeFullStoryGraph(
            loaded.Nodes, loaded.Connections, 101, loaded.Structure);

        using var rewritten = JsonDocument.Parse(json);
        Assert.Equal(5, rewritten.RootElement.GetProperty("format_version").GetInt32());

        var reloaded = StoryGraphLoaderService.Load(rewritten);
        Assert.True(reloaded.Success);
        Assert.Equal(101UL, reloaded.Nodes[0].Id);
        Assert.Equal("ch1", reloaded.Nodes[0].ChapterId);
        Assert.Equal("ch2", reloaded.Nodes[2].ChapterId);
        Assert.Single(reloaded.Structure.Groups);
        Assert.Equal("g1", reloaded.Structure.Groups[0].Id);
        Assert.Equal(2, reloaded.Structure.Groups[0].NodeIds.Count);
        Assert.Single(reloaded.Structure.Subgraphs);
        Assert.Equal(101UL, reloaded.Structure.Subgraphs[0].EntryNodeId);
        Assert.Equal(2, reloaded.Structure.Chapters.Count);
        Assert.Equal(103UL, reloaded.Structure.Chapters[1].StartNodeId);

        var issues = ProjectValidationService.Validate(
            reloaded.Nodes, reloaded.Connections, Path.GetTempPath(), 101, reloaded.Structure);
        // Default-object asset references cannot resolve in an empty temp dir;
        // the vNext contract itself must report no errors.
        Assert.DoesNotContain(issues, issue => issue.IsError && IsStructureMessage(issue.Message));
    }

    [Fact]
    public void Load_V4GoldenSamples_DefaultToSingleChapter()
    {
        string repoRoot = FindRepoRoot();
        foreach (string relative in new[]
                 {
                     Path.Combine("samples", "first_light", "Assets", "json", "full_story_graph.json"),
                     Path.Combine("samples", "second_signal", "Assets", "json", "full_story_graph.json")
                 })
        {
            string path = Path.Combine(repoRoot, relative);
            Assert.True(File.Exists(path), $"Golden sample missing: {path}");
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var loaded = StoryGraphLoaderService.Load(doc);
            Assert.True(loaded.Success, $"Golden sample failed to load: {relative}");
            Assert.True(loaded.Structure.IsEmpty);
            Assert.All(loaded.Nodes, node => Assert.Equal(string.Empty, node.ChapterId));
        }
    }

    [Fact]
    public void Validate_OrphanChapterId_IsError()
    {
        var loaded = LoadStrict(ValidV5Graph.Replace("\"chapter_id\": \"ch2\"", "\"chapter_id\": \"ghost\""));
        var issues = ProjectValidationService.Validate(
            loaded.Nodes, loaded.Connections, Path.GetTempPath(), 101, loaded.Structure);
        Assert.Contains(issues, issue => issue.IsError && issue.Message.Contains("unknown chapter"));
    }

    [Fact]
    public void Validate_DuplicateSubgraphId_IsError()
    {
        var loaded = LoadStrict(ValidV5Graph);
        loaded.Structure.Subgraphs.Add(new SubgraphDefinition("sg1", "Clone", 103, new ulong[] { 103 }, new ulong[] { 103 }));
        var issues = ProjectValidationService.Validate(
            loaded.Nodes, loaded.Connections, Path.GetTempPath(), 101, loaded.Structure);
        Assert.Contains(issues, issue => issue.IsError && issue.Message.Contains("duplicate subgraph"));
    }

    [Fact]
    public void Validate_BoundaryEdgeThroughNonPort_IsError()
    {
        // Edge 102 -> 103 leaves sg1, but 102 is not an exit; make 101 the only exit.
        var loaded = LoadStrict(ValidV5Graph);
        loaded.Structure.Subgraphs.Clear();
        loaded.Structure.Subgraphs.Add(new SubgraphDefinition("sg1", "Trial flow", 101, new ulong[] { 101 }, new ulong[] { 101, 102 }));
        var issues = ProjectValidationService.Validate(
            loaded.Nodes, loaded.Connections, Path.GetTempPath(), 101, loaded.Structure);
        Assert.Contains(issues, issue => issue.IsError && issue.Message.Contains("non-exit node"));
    }

    [Fact]
    public void Validate_SubgraphCallCycle_IsError()
    {
        const string cyclic = """
            {
              "format_version": 5, "start_node_id": 1,
              "nodes": [
                { "id": 1, "title": "A", "objects": [], "next_nodes": [ { "id": 2 } ] },
                { "id": 2, "title": "B", "objects": [], "next_nodes": [ { "id": 3 } ] },
                { "id": 3, "title": "C", "objects": [], "next_nodes": [ { "id": 4 } ] },
                { "id": 4, "title": "D", "objects": [], "next_nodes": [ { "id": 1 } ] }
              ],
              "subgraphs": [
                { "id": "left", "title": "Left", "entry_node_id": 1, "exit_node_ids": [2], "node_ids": [1, 2] },
                { "id": "right", "title": "Right", "entry_node_id": 3, "exit_node_ids": [4], "node_ids": [3, 4] }
              ]
            }
            """;
        var loaded = LoadStrict(cyclic);
        var issues = ProjectValidationService.Validate(
            loaded.Nodes, loaded.Connections, Path.GetTempPath(), 1, loaded.Structure);
        Assert.Contains(issues, issue => issue.IsError && issue.Message.Contains("call cycle"));
    }

    [Fact]
    public void Validate_UnreachableExit_IsError()
    {
        // Subgraph exit 102 unreachable: edge 101 -> 102 removed below by construction.
        const string stranded = """
            {
              "format_version": 5, "start_node_id": 1,
              "nodes": [
                { "id": 1, "title": "A", "objects": [], "next_nodes": [] },
                { "id": 2, "title": "B", "objects": [], "next_nodes": [] }
              ],
              "subgraphs": [
                { "id": "sg", "title": "Stranded", "entry_node_id": 1, "exit_node_ids": [2], "node_ids": [1, 2] }
              ]
            }
            """;
        var loaded = LoadStrict(stranded);
        var issues = ProjectValidationService.Validate(
            loaded.Nodes, loaded.Connections, Path.GetTempPath(), 1, loaded.Structure);
        Assert.Contains(issues, issue => issue.IsError && issue.Message.Contains("unreachable from its entry"));
    }

    [Fact]
    public void Validate_EmptyGroup_IsWarningOnly()
    {
        var loaded = LoadStrict(ValidV5Graph);
        loaded.Structure.Groups.Add(new CanvasGroup("empty", "Empty", "#000000", 0, 0, 10, 10, Array.Empty<ulong>()));
        var issues = ProjectValidationService.Validate(
            loaded.Nodes, loaded.Connections, Path.GetTempPath(), 101, loaded.Structure);
        Assert.Contains(issues, issue => !issue.IsError && issue.Message.Contains("has no members"));
        Assert.DoesNotContain(issues, issue => issue.IsError && issue.Message.Contains("'empty'"));
    }

    [Fact]
    public void Load_MalformedStructure_Fails()
    {
        using var doc = JsonDocument.Parse("""{ "format_version": 5, "nodes": [], "groups": "nope" }""");
        var result = StoryGraphLoaderService.Load(doc);
        Assert.False(result.Success);
        Assert.NotNull(result.ErrorMessage);
    }

    private static bool IsStructureMessage(string message) =>
        message.Contains("subgraph") || message.Contains("chapter") || message.Contains("group");

    private static StoryGraphLoadResult LoadStrict(string json)
    {
        using var doc = JsonDocument.Parse(json);
        var loaded = StoryGraphLoaderService.Load(doc);
        if (!loaded.Success)
            throw new Exception($"Test graph failed to load: {loaded.ErrorMessage}");
        return loaded;
    }

    private static string FindRepoRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory, "Rowl Engine.sln")) ||
                Directory.Exists(Path.Combine(directory, "samples", "first_light")))
                return directory;
            directory = Directory.GetParent(directory)?.FullName;
        }
        throw new Exception("Repository root with samples/ could not be located.");
    }
}
