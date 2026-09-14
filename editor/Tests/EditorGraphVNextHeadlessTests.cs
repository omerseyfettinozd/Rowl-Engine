using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor;

internal static class EditorGraphVNextHeadlessTests
{
    public static void Run()
    {
        Console.WriteLine("\n📌 [Test 34]: Graph vNext sözleşmesi (v5 serileştirme + yapı doğrulama)...");

        // Step 34.1: v5 round-trip through the loader.
        Console.WriteLine("    [Step 34.1]: v5 serialize → load structure round-trip...");
        const string v5 = """
            {
              "format_version": 5, "start_node_id": 11,
              "nodes": [
                { "id": 11, "title": "Head", "chapter_id": "c1", "objects": [], "next_nodes": [ { "id": 12 } ] },
                { "id": 12, "title": "Tail", "chapter_id": "c1", "objects": [], "next_nodes": [] }
              ],
              "groups": [
                { "id": "g", "title": "Pair", "color": "#10B981", "x": 0, "y": 0, "width": 400, "height": 200, "node_ids": [11, 12] }
              ],
              "subgraphs": [
                { "id": "s", "title": "Pair flow", "entry_node_id": 11, "exit_node_ids": [12], "node_ids": [11, 12] }
              ],
              "chapters": [
                { "id": "c1", "title": "Only", "order": 0, "summary": "" }
              ]
            }
            """;
        StoryGraphLoadResult loaded;
        using (var doc = JsonDocument.Parse(v5))
        {
            loaded = StoryGraphLoaderService.Load(doc);
        }
        if (!loaded.Success)
            throw new Exception($"v5 graph failed to load: {loaded.ErrorMessage}");
        if (loaded.Structure.Groups.Count != 1 || loaded.Structure.Subgraphs.Count != 1 ||
            loaded.Structure.Chapters.Count != 1 || loaded.Nodes[0].ChapterId != "c1")
            throw new Exception("v5 structure did not survive the load round-trip.");

        string rewritten = StoryGraphSerializer.SerializeFullStoryGraph(
            loaded.Nodes, loaded.Connections, 11, loaded.Structure);
        using (var redoc = JsonDocument.Parse(rewritten))
        {
            if (redoc.RootElement.GetProperty("format_version").GetInt32() != 5)
                throw new Exception("v5 graph was not rewritten as format_version 5.");
            var roundTripped = StoryGraphLoaderService.Load(redoc);
            if (!roundTripped.Success || roundTripped.Structure.Subgraphs.Count != 1)
                throw new Exception("Rewritten v5 graph failed to reload.");
            var issues = ProjectValidationService.Validate(
                roundTripped.Nodes, roundTripped.Connections, Path.GetTempPath(), 11,
                roundTripped.Structure);
            if (issues.Any(issue => issue.IsError && issue.Message.Contains("subgraph")) ||
                issues.Any(issue => issue.IsError && issue.Message.Contains("chapter")))
                throw new Exception("Valid v5 structure produced contract errors.");
        }

        // Step 34.2: orphan chapter reference is a build-blocking error.
        Console.WriteLine("    [Step 34.2]: yetim chapter referansı hatası...");
        const string orphan = """
            {
              "format_version": 5, "start_node_id": 1,
              "nodes": [ { "id": 1, "title": "Solo", "chapter_id": "missing", "objects": [], "next_nodes": [] } ],
              "chapters": [ { "id": "c1", "title": "Only", "order": 0, "summary": "" } ]
            }
            """;
        using (var orphanDoc = JsonDocument.Parse(orphan))
        {
            var orphanLoaded = StoryGraphLoaderService.Load(orphanDoc);
            if (!orphanLoaded.Success)
                throw new Exception($"Orphan test graph failed to load: {orphanLoaded.ErrorMessage}");
            var orphanIssues = ProjectValidationService.Validate(
                orphanLoaded.Nodes, orphanLoaded.Connections, Path.GetTempPath(), 1,
                orphanLoaded.Structure);
            if (!orphanIssues.Any(issue => issue.IsError && issue.Message.Contains("unknown chapter")))
                throw new Exception("Orphan chapter reference was not reported as an error.");
        }

        Console.WriteLine("  ✅ [PASS] Graph vNext v5 round-trip and structure validation verified");
    }
}
