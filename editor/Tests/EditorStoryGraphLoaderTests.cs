using System;
using System.Text.Json;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor;

internal static class EditorStoryGraphLoaderTests
{
    public static void Run()
    {
        Console.WriteLine("\n📌 [Test 10]: StoryGraphLoaderService Decoupled Hydration & Isolation...");
        string testGraphJson = """
        {
          "format_version": 4,
          "start_node_id": 301,
          "nodes": [
            {
              "id": 301,
              "title": "StartNode",
              "editor_x": 100,
              "editor_y": 120,
              "objects": [
                {
                  "id": "bg_obj",
                  "name": "Background",
                  "is_active": true,
                  "components": [
                    { "type": "background", "id": "bg1", "enabled": true, "data": { "texture": "Woman.png" } }
                  ]
                }
              ],
              "next_nodes": [ { "id": 302, "label": "Proceed" } ]
            },
            {
              "id": 302,
              "title": "SecondNode",
              "editor_x": 400,
              "editor_y": 120,
              "objects": [],
              "next_nodes": []
            }
          ]
        }
        """;

        using (var doc = JsonDocument.Parse(testGraphJson))
        {
            var loadResult = StoryGraphLoaderService.Load(doc);
            if (!loadResult.Success || loadResult.Nodes.Count != 2 || loadResult.Connections.Count != 1)
                throw new Exception($"StoryGraphLoaderService failed: expected 2 nodes & 1 connection, got {loadResult.Nodes.Count} nodes & {loadResult.Connections.Count} connections");

            if (loadResult.Nodes[0].Id != 301 || loadResult.Nodes[1].Id != 302)
                throw new Exception("StoryGraphLoaderService node ID ordering mismatch");

            if (loadResult.Connections[0].SourceNode?.Id != 301 || loadResult.Connections[0].TargetNode?.Id != 302)
                throw new Exception("StoryGraphLoaderService connection wiring mismatch");
        }

        // Test malformed graph handling
        using (var malformedDoc = JsonDocument.Parse("""{ "format_version": 4 }"""))
        {
            var failResult = StoryGraphLoaderService.Load(malformedDoc);
            if (failResult.Success)
                throw new Exception("StoryGraphLoaderService should fail gracefully on missing 'nodes' array");
        }

        Console.WriteLine("  ✅ [PASS] StoryGraphLoaderService decoupled hydration, wire connections, and error isolation verified");
    }
}
