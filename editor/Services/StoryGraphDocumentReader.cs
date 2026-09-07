using System;
using System.IO;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Resolves and parses the persisted story graph without mutating editor state.
/// The caller owns the returned document and can therefore apply its result as
/// a transaction only after parsing has succeeded.
/// </summary>
internal static class StoryGraphDocumentReader
{
    public static bool TryRead(
        string assetsPath,
        string assetsJsonPath,
        out JsonDocument? document,
        out string filePath,
        out string? error)
    {
        document = null;
        filePath = Path.Combine(assetsPath, "full_story_graph.json");
        error = null;

        if (!File.Exists(filePath))
        {
            filePath = Path.Combine(assetsJsonPath, "full_story_graph.json");
            if (!File.Exists(filePath))
                return false;
        }

        try
        {
            document = JsonDocument.Parse(File.ReadAllText(filePath));
            if (!document.RootElement.TryGetProperty("nodes", out var nodes) ||
                nodes.ValueKind != JsonValueKind.Array)
            {
                document.Dispose();
                document = null;
                error = "Story graph does not contain a nodes array.";
                return false;
            }

            return true;
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        {
            error = exception.Message;
            return false;
        }
    }
}
