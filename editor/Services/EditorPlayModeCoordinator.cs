using System;
using System.IO;
using System.Threading.Tasks;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Coordinates transitioning between Edit Mode and live in-editor Play Mode.
/// </summary>
public static class EditorPlayModeCoordinator
{
    public static async Task<bool> StartPlayModeAsync(
        EngineHost host,
        Func<Task<bool>> ensureEngineConnected,
        Func<bool> saveStoryFiles,
        Action<NodeViewModel> selectNodeQuiet,
        Action<NodeViewModel> pushScene,
        Func<NodeViewModel?> getStartNode,
        Action<string> log,
        Action onSwitchedToGameTab,
        string assetsJsonPath)
    {
        saveStoryFiles();
        log("▶ Starting Offscreen Play Mode...");

        if (!host.IsInitialized)
        {
            bool connected = await ensureEngineConnected();
            if (!connected || !host.IsInitialized)
            {
                log("❌ Engine not initialized. Click 'Connect Engine' first.");
                return false;
            }
        }

        onSwitchedToGameTab();

        string graphPath = Path.Combine(assetsJsonPath, "full_story_graph.json");
        if (File.Exists(graphPath))
        {
            host.LoadStoryGraph(graphPath);
            log($"[Play] Story graph loaded from: {graphPath}");
        }

        // Activate engine play state
        host.SetPlayState(true);
        host.ResetToStartNode();

        var startNode = getStartNode();
        if (startNode != null)
        {
            selectNodeQuiet(startNode);
            pushScene(startNode);
        }

        log("✅ Engine play state activated (Started from first frame).");
        return true;
    }

    public static void StopPlayMode(
        EngineHost host,
        Func<NodeViewModel?> getStartNode,
        Action<NodeViewModel> pushScene,
        Action<NodeViewModel> selectNode,
        Action<string> log)
    {
        host.SetPlayState(false);
        host.ResetToStartNode();

        log("⏹ Play mode stopped (Engine reset to first frame).");

        var startNode = getStartNode();
        if (startNode != null)
        {
            pushScene(startNode);
            selectNode(startNode);
        }
    }
}
