using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for serializing active scene components and synchronizing them to the embedded native engine.
/// </summary>
public static class EditorSceneSyncService
{
    /// <summary>
    /// Sends the active node's scene data directly to the engine via P/Invoke.
    /// </summary>
    public static bool PushSceneToEngine(EngineHost host, NodeViewModel? node, Action<string>? log = null)
    {
        if (node == null || !host.IsInitialized) return false;

        try
        {
            string componentsJson = StoryGraphSerializer.SerializePreviewComponents(node);
            bool hasEnabledScripts = node.AllComponents.OfType<ScriptComponentViewModel>()
                .Any(component => component.IsEnabled);
            bool updated = host.UpdateSceneFromComponents(componentsJson, skipIfUnchanged: !hasEnabledScripts);
            if (updated)
                ApplyScriptRuntimeDiagnostics(host, node);
            return updated;
        }
        catch (Exception ex)
        {
            log?.Invoke($"⚠️ Component-based scene sync failed, falling back to legacy: {ex.Message}");
            // Fallback to legacy single-character API with rotation
            host.UpdateSceneEx(
                node.Speaker ?? "",
                node.DialogueText ?? "",
                node.BackgroundTexture ?? "",
                (float)node.BackgroundX, (float)node.BackgroundY,
                (float)node.BackgroundWidth, (float)node.BackgroundHeight,
                (float)node.BackgroundRotation,
                node.CharacterSprite ?? "",
                (float)node.CharacterX, (float)node.CharacterY,
                (float)node.CharacterWidth, (float)node.CharacterHeight,
                (float)node.CharacterRotation,
                (float)node.DialogueBoxX, (float)node.DialogueBoxY,
                (float)node.DialogueBoxWidth, (float)node.DialogueBoxHeight
            );
            return true;
        }
    }

    /// <summary>
    /// Routes one polled audio-telemetry sample to the live preview HUD and the
    /// selected node's audio component. The event sample carries master-bus
    /// levels (native channel 3); per-channel BGM (0) and SFX/Voice (2) levels
    /// are polled via delegates. Pure routing over delegates so the mapping is
    /// unit-testable without a native handle.
    /// </summary>
    public static void RouteAudioTelemetry(
        float masterPeakL,
        float masterPeakR,
        float masterRmsL,
        float masterRmsR,
        Func<int, int, float>? getPeak,
        Func<int, int, float>? getRms,
        Action<float, float, float, float>? updatePreview,
        AudioComponentViewModel? selectedAudio)
    {
        updatePreview?.Invoke(masterPeakL, masterPeakR, masterRmsL, masterRmsR);
        if (selectedAudio == null || getPeak == null || getRms == null) return;
        selectedAudio.UpdateAudioTelemetry(
            getPeak(0, 0), getPeak(0, 1), getRms(0, 0), getRms(0, 1), isSfx: false);
        selectedAudio.UpdateAudioTelemetry(
            getPeak(2, 0), getPeak(2, 1), getRms(2, 0), getRms(2, 1), isSfx: true);
    }

    /// <summary>
    /// Reloads the full story graph into the native engine when the file is
    /// present, so hot-reload picks up connection changes alongside the scene.
    /// </summary>
    public static void ReloadStoryGraphIntoEngine(EngineHost host, string assetsJsonPath)
    {
        string graphPath = Path.Combine(assetsJsonPath, "full_story_graph.json");
        if (File.Exists(graphPath))
            host.LoadStoryGraph(graphPath);
    }

    /// <summary>
    /// Synchronizes native script runtime diagnostic state (errors, execution state) back into ScriptComponentViewModels.
    /// </summary>
    public static void ApplyScriptRuntimeDiagnostics(EngineHost host, NodeViewModel? node)
    {
        if (node == null) return;
        var scripts = node.AllComponents.OfType<ScriptComponentViewModel>()
            .Where(component => component.IsEnabled).ToList();
        for (int index = 0; index < scripts.Count; index++)
        {
            var script = scripts[index];
            var diagnostic = host.ScriptRuntimeDiagnostics.FirstOrDefault(item =>
                item.module_id.EndsWith("#" + index, StringComparison.Ordinal) &&
                (string.IsNullOrEmpty(item.path) || item.path == script.ScriptPath));
            script.RuntimeState = diagnostic?.state ?? "Not run";
            script.RuntimeError = diagnostic?.error ?? string.Empty;
        }
    }
}
