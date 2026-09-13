using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorHeadlessTestSuite
{
        internal static void Run(string? benchmarkPath = null)
        {
            Console.WriteLine("\n=======================================================");
            Console.WriteLine("🧪 ROWL ENGINE EDITOR HEADLESS TEST SUITE 🧪");
            Console.WriteLine("=======================================================");

            Program.BuildAvaloniaApp().SetupWithoutStarting();
            VerifyPlatformSpecificProjectRootResolution();
            string sourceAssets = MainWindowViewModel.AssetsPath;
            string testProjectRoot = Path.Combine(
                Path.GetTempPath(), $"RowlEditorTests_{Guid.NewGuid():N}");
            CopyDirectoryForTests(sourceAssets, Path.Combine(testProjectRoot, "Assets"));
            var mainVm = new MainWindowViewModel(testProjectRoot, connectEngine: false);

            try
            {

            // Test 1: NodeViewModel & Component Model & Trash Can Button Command
            EditorComponentHierarchyTests.Run(mainVm);

            // Test 2: Theme System (Light & Dark Mode)
            EditorWorkspaceThemeTests.Run(mainVm);

            // Test 3: ConnectionViewModel & Graph Topology
            EditorGraphValidationTests.Run(testProjectRoot);

            // Test 4: Story Graph File Serialization & Deserialization
            EditorGraphPersistenceTests.Run(mainVm, testProjectRoot);

            // Test 5: Asset Auto-Copy & Project Portability
            EditorAssetPortabilityTests.Run(mainVm);

            // Test 6: OBS Assist & Magnetic Snapping System
            EditorLayoutAssistTests.Run(mainVm);

            // Test 7/7b: Project save/build and runtime settings
            EditorProjectPersistenceTests.Run(mainVm, testProjectRoot);

            // Test 8: Performance Benchmark & Cache Optimization Verification
            EditorAssetCacheTests.Run();

            // ── Test 9: Variable & Condition Components + Native State Slots ───────────
            EditorRuntimeStateTests.Run(mainVm, testProjectRoot);

            // ── Test 9b: Player-local settings profile ───────────────────────
            Console.WriteLine("\n📌 [Test 9b]: Player settings profile persistence and bounds...");
            string profilePath = Path.Combine(testProjectRoot, "player-settings.json");
            var savedProfile = new PlayerSettingsProfile
            {
                MasterVolume = 0.8f,
                BgmVolume = 0.7f,
                VoiceVolume = 0.6f,
                SfxVolume = 0.5f,
                TextSpeedMultiplier = 1.5f,
                AutoAdvanceDelay = 3.25f
            };
            savedProfile.Save(profilePath);
            var restoredProfile = PlayerSettingsProfile.Load(profilePath);
            if (restoredProfile.MasterVolume != 0.8f || restoredProfile.BgmVolume != 0.7f ||
                restoredProfile.VoiceVolume != 0.6f || restoredProfile.SfxVolume != 0.5f ||
                restoredProfile.TextSpeedMultiplier != 1.5f || restoredProfile.AutoAdvanceDelay != 3.25f)
            {
                throw new Exception("Player settings profile did not round-trip");
            }
            File.WriteAllText(profilePath, "not-json");
            var fallbackProfile = PlayerSettingsProfile.Load(profilePath);
            if (fallbackProfile.MasterVolume != 1 || fallbackProfile.TextSpeedMultiplier != 1)
                throw new Exception("Invalid player settings profile did not fall back to defaults");
            Console.WriteLine("  ✅ [PASS] Player-local settings profile persistence and invalid-file fallback verified");

            // ── Test 10: StoryGraphLoaderService Decoupled Hydration & Error Isolation ──
            EditorStoryGraphLoaderTests.Run();

            // ── Test 11: Camera & Transition Components Lifecycle & Native P/Invoke ──
            EditorCameraTransitionTests.Run(mainVm);

            // ── Test 12: ProjectSaveAsCoordinator, PackageAssetsAsync & Pipeline Validation Isolation ──
            EditorSaveAsBuildTests.Run(mainVm, testProjectRoot);

            // ── Test 13: StoryGraphDocumentWriter, StoryGraphCanvasService & Layout Assist Presets ──
            EditorDocumentCanvasTests.Run(mainVm);

            // ── Test 14: EditorSceneSyncService & EditorPlayModeCoordinator Lifecycle & Diagnostics ──
            EditorSceneSyncTests.Run(mainVm, testProjectRoot);

            // ── Test 15: EditorSettingsSyncService & EditorComponentService Lifecycle ──
            EditorSettingsSyncTests.Run(mainVm, testProjectRoot);

            // ── Test 16: EditorAssetImportService & EditorBuildCoordinator Isolation ──
            EditorAssetImportBuildTests.Run(mainVm, testProjectRoot);

            // ── Test 17: StoryGraphLifecycleCoordinator & EditorWorkspaceLayoutService Isolation ──
            EditorStoryLifecycleTests.Run(mainVm);

            // ── Test 18: EditorProjectLifecycleCoordinator & EditorVisualAssetPickerService Isolation ──
            EditorProjectLifecycleTests.Run(mainVm, testProjectRoot);

            // ── Test 19: EditorAudioAssetPickerService & EditorModalDialogCoordinator Isolation ──
            EditorAudioPickerTests.Run(mainVm, testProjectRoot);

            // Test 20: EditorSelectionCoordinator & EditorBatchOperationService
            EditorSelectionBatchTests.Run(mainVm);

            // Test 21: EditorNotificationService & Diagnostics Integration
            EditorNotificationTests.Run(mainVm);

            // Test 22: Visual Transform Gizmo, Rotation, and Aspect-Ratio Scale Controls
            EditorTransformGizmoTests.Run(mainVm);

            // Test 23: Real-Time Audio DSP Telemetry, Live Stereo VU Meter & Audio Preview Controls
            EditorAudioDspTests.Run(mainVm);

            // Test 24: Multi-Layer Parallax Depth Background System & 2.5D Camera Interaction
            EditorParallaxCameraTests.Run(mainVm);

            // ── Test 25: Typewriter Character Voice Blips & Dialogue Audio Effects ──
            EditorTypewriterAudioTests.Run(mainVm);

            // Test 26: Cinematic Camera Shake Presets & Screen Visual FX Pipeline (Screen Flash, Fade, Color Tint / Vignette Post-Process)
            EditorCinematicFxTests.Run(mainVm);


            if (!string.IsNullOrWhiteSpace(benchmarkPath))
            {
                var benchmark = new EditorInteractionBenchmark();
                var benchmarkNode = mainVm.Nodes.First();
                const int iterations = 100;
                var stopwatch = Stopwatch.StartNew();
                for (int i = 0; i < iterations; i++)
                {
                    benchmarkNode.X += 1;
                    benchmarkNode.Y -= 1;
                }
                stopwatch.Stop();
                benchmark.Record("graph_drag_step_ms", stopwatch.Elapsed.TotalMilliseconds / iterations);

                var selectionNodeA = new NodeViewModel(9897, "Selection A", 0, 0, bare: true);
                selectionNodeA.AddComponent<DialogueComponentViewModel>().DialogueText = "Selection A";
                var selectionNodeB = new NodeViewModel(9898, "Selection B", 0, 0, bare: true);
                selectionNodeB.AddComponent<DialogueComponentViewModel>().DialogueText = "Selection B";
                mainVm.EngineHost.ResetToStartNode();
                mainVm.SelectNodeQuiet(selectionNodeA);
                stopwatch.Restart();
                for (int i = 0; i < iterations; i++)
                    mainVm.SelectNodeQuiet(i % 2 == 0 ? selectionNodeB : selectionNodeA);
                stopwatch.Stop();
                benchmark.Record("node_selection_with_preview_ms", stopwatch.Elapsed.TotalMilliseconds / iterations);

                stopwatch.Restart();
                for (int i = 0; i < iterations; i++)
                {
                    var component = benchmarkNode.AddComponent<VariableComponentViewModel>();
                    benchmarkNode.RemoveComponent(component);
                }
                stopwatch.Stop();
                benchmark.Record("component_change_ms", stopwatch.Elapsed.TotalMilliseconds / iterations);

                stopwatch.Restart();
                for (int i = 0; i < iterations; i++)
                    _ = StoryGraphSerializer.SerializePreviewComponents(benchmarkNode);
                stopwatch.Stop();
                benchmark.Record("preview_serialization_ms", stopwatch.Elapsed.TotalMilliseconds / iterations);

                mainVm.SelectNodeQuiet(benchmarkNode);
                mainVm.EngineHost.ResetToStartNode();
                mainVm.ScheduleEnginePreviewUpdate(benchmarkNode);
                stopwatch.Restart();
                bool delivered = mainVm.DeliverScheduledEnginePreview();
                stopwatch.Stop();
                if (!delivered) throw new Exception("Preview debounce did not deliver its selected node");
                benchmark.Record("preview_delivery_ms", stopwatch.Elapsed.TotalMilliseconds);
                benchmark.Record("preview_native_update_ms", mainVm.EngineHost.LastSceneUpdateMilliseconds);
                benchmark.Record("preview_step_ms", mainVm.EngineHost.LastPreviewStepMilliseconds);
                benchmark.Record("preview_pixel_copy_ms", mainVm.EngineHost.LastPixelBufferCopyMilliseconds);
                benchmark.Record("preview_texture_load_ms", mainVm.EngineHost.LastFrameTextureLoadMilliseconds);
                benchmark.Record("preview_non_texture_render_ms", mainVm.EngineHost.LastFrameNonTextureRenderMilliseconds);
                benchmark.Record("preview_text_rasterization_ms", mainVm.EngineHost.LastFrameTextRasterizationMilliseconds);
                benchmark.Record("preview_renderer_flush_ms", mainVm.EngineHost.LastFrameRendererFlushMilliseconds);

                var cacheNode = new NodeViewModel(9901, "Cache benchmark", 0, 0, bare: true);
                cacheNode.AddComponent<DialogueComponentViewModel>().DialogueText = "Unchanged preview";
                mainVm.SelectNodeQuiet(cacheNode);
                mainVm.EngineHost.ResetToStartNode();
                mainVm.ScheduleEnginePreviewUpdate(cacheNode);
                if (!mainVm.DeliverScheduledEnginePreview())
                    throw new Exception("Initial script-free preview did not deliver");
                mainVm.ScheduleEnginePreviewUpdate(cacheNode);
                stopwatch.Restart();
                bool duplicateDelivered = mainVm.DeliverScheduledEnginePreview();
                stopwatch.Stop();
                if (duplicateDelivered) throw new Exception("Unchanged script-free preview should not redraw");
                benchmark.Record("preview_duplicate_delivery_ms", stopwatch.Elapsed.TotalMilliseconds);

                string saveAsBenchmarkDir = Path.Combine(Path.GetTempPath(), "RowlEditorBenchmark_SaveAs_" + Guid.NewGuid().ToString("N"));
                stopwatch.Restart();
                mainVm.SaveProjectToDirectory(saveAsBenchmarkDir);
                stopwatch.Stop();
                if (!File.Exists(Path.Combine(saveAsBenchmarkDir, "project.rowlproj")))
                    throw new Exception("Save As benchmark did not publish its project manifest");
                benchmark.Record("save_as_ms", stopwatch.Elapsed.TotalMilliseconds);
                try { Directory.Delete(saveAsBenchmarkDir, true); } catch { }

                benchmark.Write(benchmarkPath);
                Console.WriteLine($"  ⚡ [BENCHMARK] Editor interaction report written: {benchmarkPath}");
            }

            // Test 27: Audio device status observer (edge-triggered toasts)
            EditorAudioDeviceTests.Run(mainVm);

            // Test 28: Faz 1.1 ViewModel thinning — extracted service behavior equivalence
            EditorViewModelThinningTests.Run(mainVm);

            // Test 29: End-to-end editor flow (Hub create → open → nodes → inspector → import → preview → save → build → package)
            EditorEndToEndFlowTests.Run(testProjectRoot);

            Console.WriteLine("\n=======================================================");
            Console.WriteLine("🎉 ALL EDITOR HEADLESS TESTS PASSED SUCCESSFULLY! 🎉");
            Console.WriteLine("=======================================================\n");
            }
            finally
            {
                mainVm.Dispose();
                try { Directory.Delete(testProjectRoot, true); } catch { }
            }
        }

        private static void CopyDirectoryForTests(string sourceDir, string targetDir)
        {
            Directory.CreateDirectory(targetDir);
            foreach (string file in Directory.GetFiles(sourceDir))
            {
                File.Copy(file, Path.Combine(targetDir, Path.GetFileName(file)), overwrite: true);
            }
            foreach (string subDirectory in Directory.GetDirectories(sourceDir))
            {
                CopyDirectoryForTests(
                    subDirectory,
                    Path.Combine(targetDir, Path.GetFileName(subDirectory)));
            }
        }

        private static void VerifyPlatformSpecificProjectRootResolution()
        {
            string fixtureRoot = Path.Combine(
                Path.GetTempPath(), $"RowlProjectRootFixture_{Guid.NewGuid():N}");
            string nestedOutput = Path.Combine(
                fixtureRoot, "editor", "Tests", "bin", "x64", "Debug", "net10.0");
            try
            {
                Directory.CreateDirectory(Path.Combine(fixtureRoot, "Assets"));
                Directory.CreateDirectory(nestedOutput);
                File.WriteAllText(Path.Combine(fixtureRoot, "CMakeLists.txt"), "# fixture");
                string resolved = MainWindowViewModel.ResolveProjectRootFrom(nestedOutput);
                if (!string.Equals(resolved, fixtureRoot, StringComparison.Ordinal))
                    throw new Exception($"Platform-specific output root resolved to '{resolved}'");
            }
            finally
            {
                try { Directory.Delete(fixtureRoot, true); } catch { }
            }
        }


}

public sealed class EditorHeadlessSuiteTests
{
    [Fact]
    public void HeadlessSuitePreservesEditorRuntimeContracts()
    {
        EditorHeadlessTestSuite.Run(
            Environment.GetEnvironmentVariable("ROWL_EDITOR_BENCHMARK_JSON"));
    }
}
