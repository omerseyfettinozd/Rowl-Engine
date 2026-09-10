using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for persisting story graphs and active story files to disk atomically.
/// </summary>
public static class StoryGraphDocumentWriter
{
    public static bool SaveFullStoryGraph(
        string assetsPath,
        string assetsJsonPath,
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong startId = 101,
        Action<string>? log = null)
    {
        try
        {
            Directory.CreateDirectory(assetsPath);
            Directory.CreateDirectory(assetsJsonPath);
            string content = StoryGraphSerializer.SerializeFullStoryGraph(nodes, connections, startId);
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(assetsPath, "full_story_graph.json"), content);
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(assetsJsonPath, "full_story_graph.json"), content);
            return true;
        }
        catch (Exception ex)
        {
            log?.Invoke($"⚠️ Failed to save story graph: {ex.Message}");
            return false;
        }
    }

    public static bool SaveActiveStory(
        string assetsJsonPath,
        NodeViewModel? node,
        Action<string>? log = null)
    {
        if (node == null) return false;
        try
        {
            Directory.CreateDirectory(assetsJsonPath);
            string json = StoryGraphSerializer.SerializeActiveStory(node);
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(assetsJsonPath, "active_story.json"), json);
            return true;
        }
        catch (Exception ex)
        {
            log?.Invoke($"⚠️ Failed to save active_story.json: {ex.Message}");
            return false;
        }
    }
}
