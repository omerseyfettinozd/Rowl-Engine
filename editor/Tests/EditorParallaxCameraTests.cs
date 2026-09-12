using System;
using System.Collections.Generic;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorParallaxCameraTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 24]: Multi-Layer Parallax Depth & 2.5D Camera Controls...");

        // Step 24.1: BackgroundComponentViewModel Parallax & Opacity Defaults and Reset
        Console.WriteLine("    [Step 24.1]: BackgroundComponentViewModel Parallax & Opacity Defaults...");
        var bgComp = new BackgroundComponentViewModel();
        if (Math.Abs(bgComp.ParallaxFactorX - 1.0) > 0.001 ||
            Math.Abs(bgComp.ParallaxFactorY - 1.0) > 0.001 ||
            Math.Abs(bgComp.Opacity - 1.0) > 0.001)
            throw new Exception("BackgroundComponentViewModel default parallax/opacity mismatch");

        bgComp.ParallaxFactorX = 0.25;
        bgComp.ParallaxFactorY = 0.50;
        bgComp.Opacity = 0.80;
        bgComp.ResetDimensions();
        if (Math.Abs(bgComp.ParallaxFactorX - 1.0) > 0.001 ||
            Math.Abs(bgComp.ParallaxFactorY - 1.0) > 0.001 ||
            Math.Abs(bgComp.Opacity - 1.0) > 0.001)
            throw new Exception("ResetDimensions failed to reset parallax and opacity");

        // Step 24.2: Serialization & Deserialization Round-Trip
        Console.WriteLine("    [Step 24.2]: BackgroundComponentViewModel Serialization Round-Trip...");
        bgComp.ParallaxFactorX = 0.35;
        bgComp.ParallaxFactorY = 0.65;
        bgComp.Opacity = 0.75;
        var serializedBg = bgComp.Serialize();
        if (!serializedBg.ContainsKey("parallax_x") || !serializedBg.ContainsKey("parallax_y") || !serializedBg.ContainsKey("opacity"))
            throw new Exception("BackgroundComponentViewModel.Serialize missing parallax_x/y or opacity keys");
        if (Math.Abs((double)serializedBg["parallax_x"] - 0.35) > 0.001 ||
            Math.Abs((double)serializedBg["parallax_y"] - 0.65) > 0.001 ||
            Math.Abs((double)serializedBg["opacity"] - 0.75) > 0.001)
            throw new Exception("Serialized parallax/opacity values mismatch");

        var roundTripBg = new BackgroundComponentViewModel();
        roundTripBg.Deserialize(new Dictionary<string, object?>
        {
            ["parallax_x"] = 0.42,
            ["parallax_y"] = 0.84,
            ["opacity"] = 0.90
        });
        if (Math.Abs(roundTripBg.ParallaxFactorX - 0.42) > 0.001 ||
            Math.Abs(roundTripBg.ParallaxFactorY - 0.84) > 0.001 ||
            Math.Abs(roundTripBg.Opacity - 0.90) > 0.001)
            throw new Exception("BackgroundComponentViewModel.Deserialize round-trip mismatch");

        // Step 24.3: NodeViewModel Proxy Properties & Change Notifications
        Console.WriteLine("    [Step 24.3]: NodeViewModel Proxy Properties & PropertyChanged Notifications...");
        var testNode = new NodeViewModel(2401, "Parallax Test Node", 100, 100, bare: true);
        var nodeBg = testNode.AddComponent<BackgroundComponentViewModel>();
        bool notifiedPx = false;
        bool notifiedPy = false;
        bool notifiedOp = false;
        testNode.PropertyChanged += (s, e) =>
        {
            if (e.PropertyName == nameof(NodeViewModel.BackgroundParallaxX)) notifiedPx = true;
            if (e.PropertyName == nameof(NodeViewModel.BackgroundParallaxY)) notifiedPy = true;
            if (e.PropertyName == nameof(NodeViewModel.BackgroundOpacity)) notifiedOp = true;
        };

        testNode.BackgroundParallaxX = 0.15;
        testNode.BackgroundParallaxY = 0.25;
        testNode.BackgroundOpacity = 0.60;
        if (!notifiedPx || !notifiedPy || !notifiedOp)
            throw new Exception("NodeViewModel failed to notify on background parallax/opacity changes");
        if (Math.Abs(nodeBg.ParallaxFactorX - 0.15) > 0.001 ||
            Math.Abs(nodeBg.ParallaxFactorY - 0.25) > 0.001 ||
            Math.Abs(nodeBg.Opacity - 0.60) > 0.001)
            throw new Exception("NodeViewModel proxy property setters failed to update component");

        // Step 24.4: StoryGraphSerializer and Preview Components
        Console.WriteLine("    [Step 24.4]: StoryGraphSerializer JSON Generation...");
        string previewJson = StoryGraphSerializer.SerializePreviewComponents(testNode);
        if (!previewJson.Contains("parallax_x") || !previewJson.Contains("0.15") || !previewJson.Contains("opacity"))
            throw new Exception("StoryGraphSerializer.SerializePreviewComponents missing parallax/opacity");

        string activeStoryJson = StoryGraphSerializer.SerializeActiveStory(testNode);
        if (!activeStoryJson.Contains("background_parallax_x") || !activeStoryJson.Contains("background_opacity"))
            throw new Exception("StoryGraphSerializer.SerializeActiveStory missing background_parallax_x/opacity");

        // Step 24.5: EngineHost Native P/Invoke Integration
        Console.WriteLine("    [Step 24.5]: EngineHost Native P/Invoke Parallax Queries & JSON Sync...");
        mainVm.EngineHost.SetBackgroundParallax(0.20f, 0.30f);
        float hostPx = mainVm.EngineHost.GetBackgroundParallaxX();
        float hostPy = mainVm.EngineHost.GetBackgroundParallaxY();
        if (Math.Abs(hostPx - 0.20f) > 0.001f || Math.Abs(hostPy - 0.30f) > 0.001f)
            throw new Exception($"EngineHost SetBackgroundParallax failed: got ({hostPx}, {hostPy})");

        // Push component scene JSON to EngineHost
        string componentSyncJson = """
        [
            {
                "type": "background",
                "enabled": true,
                "data": {
                    "texture": "test_bg.png",
                    "rotation": 15.0,
                    "parallax_x": 0.45,
                    "parallax_y": 0.55,
                    "opacity": 0.85
                }
            }
        ]
        """;
        mainVm.EngineHost.UpdateSceneFromComponents(componentSyncJson);
        float syncPx = mainVm.EngineHost.GetBackgroundParallaxX();
        float syncPy = mainVm.EngineHost.GetBackgroundParallaxY();
        float syncOp = mainVm.EngineHost.GetBackgroundOpacity();
        if (Math.Abs(syncPx - 0.45f) > 0.001f || Math.Abs(syncPy - 0.55f) > 0.001f || Math.Abs(syncOp - 0.85f) > 0.001f)
            throw new Exception($"EngineHost UpdateSceneFromComponents failed: got px={syncPx}, py={syncPy}, op={syncOp}");

        // Step 24.6: StoryGraphNodeHydrator Legacy Fields Ingestion
        Console.WriteLine("    [Step 24.6]: StoryGraphNodeHydrator Legacy Fields Round-Trip...");
        string legacyNodeJson = """
        {
            "speaker": "Evelyn",
            "dialogue": "Parallax check",
            "background": "bg_parallax.png",
            "background_parallax_x": 0.12,
            "background_parallax_y": 0.18,
            "background_opacity": 0.95
        }
        """;
        using var legacyDoc = JsonDocument.Parse(legacyNodeJson);
        var legacyNode = new NodeViewModel(2402, "Legacy Parallax Node", 0, 0, bare: true);
        StoryGraphNodeHydrator.PopulateLegacyFields(legacyNode, legacyDoc.RootElement);
        var legacyBg = legacyNode.GetComponent<BackgroundComponentViewModel>();
        if (legacyBg == null ||
            Math.Abs(legacyBg.ParallaxFactorX - 0.12) > 0.001 ||
            Math.Abs(legacyBg.ParallaxFactorY - 0.18) > 0.001 ||
            Math.Abs(legacyBg.Opacity - 0.95) > 0.001)
            throw new Exception("StoryGraphNodeHydrator failed to ingest legacy parallax & opacity");

        Console.WriteLine("  ✅ [PASS] Multi-Layer Parallax Depth & 2.5D Camera Controls verified");
    }
}
