using System;
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
            // Fallback to legacy single-character API
            host.UpdateScene(
                node.Speaker ?? "",
                node.DialogueText ?? "",
                node.BackgroundTexture ?? "",
                (float)node.BackgroundX, (float)node.BackgroundY,
                (float)node.BackgroundWidth, (float)node.BackgroundHeight,
                node.CharacterSprite ?? "",
                (float)node.CharacterX, (float)node.CharacterY,
                (float)node.CharacterWidth, (float)node.CharacterHeight,
                (float)node.DialogueBoxX, (float)node.DialogueBoxY,
                (float)node.DialogueBoxWidth, (float)node.DialogueBoxHeight
            );
            return true;
        }
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
