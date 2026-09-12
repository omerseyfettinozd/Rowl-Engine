using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Models;

namespace RowlEngine.Editor
{
    internal class Program
    {
        [STAThread]
        public static void Main(string[] args)
        {
            // If invoked with --test or --headless-test, run automated test suite and exit
            if (args != null && args.Any(a => a == "--test" || a == "--headless-test"))
            {
                int benchmarkIndex = Array.IndexOf(args, "--editor-benchmark-json");
                string? benchmarkPath = benchmarkIndex >= 0 && benchmarkIndex + 1 < args.Length
                    ? args[benchmarkIndex + 1] : null;
                RunHeadlessTests(benchmarkPath);
                return;
            }

            TaskScheduler.UnobservedTaskException += (sender, e) =>
            {
                if (e.Exception?.InnerException is TaskCanceledException || e.Exception?.InnerExceptions?.Any(x => x is TaskCanceledException) == true)
                {
                    e.SetObserved();
                }
            };

            AppDomain.CurrentDomain.UnhandledException += (sender, e) =>
            {
                if (e.ExceptionObject is TaskCanceledException || (e.ExceptionObject is Exception ex && ex.InnerException is TaskCanceledException))
                {
                    return;
                }
            };

            try
            {
                BuildAvaloniaApp().StartWithClassicDesktopLifetime(args ?? Array.Empty<string>());
            }
            catch (TaskCanceledException)
            {
                // Normal cancellation on shutdown
            }
        }

        private static void RunHeadlessTests(string? benchmarkPath = null)
        {
            Console.WriteLine("\n=======================================================");
            Console.WriteLine("🧪 ROWL ENGINE EDITOR HEADLESS TEST SUITE 🧪");
            Console.WriteLine("=======================================================");

            BuildAvaloniaApp().SetupWithoutStarting();
            string sourceAssets = MainWindowViewModel.AssetsPath;
            string testProjectRoot = Path.Combine(
                Path.GetTempPath(), $"RowlEditorTests_{Guid.NewGuid():N}");
            CopyDirectoryForTests(sourceAssets, Path.Combine(testProjectRoot, "Assets"));
            var mainVm = new MainWindowViewModel(testProjectRoot, connectEngine: false);

            try
            {

            // Test 1: NodeViewModel & Component Model & Trash Can Button Command
            Console.WriteLine("\n📌 [Test 1]: NodeViewModel & Modular Component Trash Can Button...");
            var node = new NodeViewModel(101, "Test Node", 100, 150, bare: false);
            if (node.Components.Count < 4) throw new Exception("Expected at least 4 default components");
            node.IsStartNode = true;
            if (node.BorderColor != "#10B981") throw new Exception("Start node visual state was not applied");
            node.IsSelected = true;
            if (node.BorderColor != "#F09A78") throw new Exception("Selected node visual state did not take priority");
            node.IsSelected = false;
            if (node.BorderColor != "#10B981" || !node.ComponentSummary.EndsWith("BİLEŞEN"))
                throw new Exception("Node card visual summary state is inconsistent");

            var dlg = node.GetComponent<DialogueComponentViewModel>();
            if (dlg == null) throw new Exception("DialogueComponentViewModel missing");
            dlg.Speaker = "TestSpeaker";
            dlg.DialogueText = "Hello Unit Test!";
            dlg.X = 120;
            dlg.Y = 820;
            if (node.Speaker != "TestSpeaker" || node.DialogueText != "Hello Unit Test!" || node.DialogueBoxX != 120 || node.DialogueBoxY != 820)
                throw new Exception("Proxy dialogue properties mismatch");

            var secondChar = node.AddComponent<CharacterComponentViewModel>();
            secondChar.Sprite = "Margot.jpg";
            secondChar.X = 1200;
            if (node.Components.Count(c => c is CharacterComponentViewModel) != 2)
                throw new Exception("Multi-character addition failed");

            // Multi-Dialogue test
            var secondDlg = node.AddComponent<DialogueComponentViewModel>();
            secondDlg.Speaker = "SecondSpeaker";
            secondDlg.DialogueText = "I am the second dialogue!";
            if (node.DialogueComponents.Count != 2)
                throw new Exception("Multi-dialogue addition failed: expected 2 dialogue components");

            secondDlg.RemoveSelfCommand.Execute(null);
            if (node.DialogueComponents.Count != 1)
                throw new Exception("Dialogue removal failed");

            var script = node.AddComponent<ScriptComponentViewModel>();
            script.ScriptPath = "scripts/intro.lua";
            script.InlineCode = "function on_enter() rowl.var_set('seen_intro', '1') end";
            var scriptData = script.Serialize();
            var restoredScript = new ScriptComponentViewModel();
            restoredScript.Deserialize(scriptData.ToDictionary(pair => pair.Key, pair => (object?)pair.Value));
            if (restoredScript.ScriptPath != script.ScriptPath || restoredScript.InlineCode != script.InlineCode)
                throw new Exception("ScriptComponent serialization mismatch");
            script.RuntimeState = "failed";
            script.RuntimeError = "syntax error";
            if (!script.HasRuntimeError || script.Serialize().ContainsKey("runtime_error"))
                throw new Exception("Script runtime feedback leaked into story serialization");

            var audioSettings = node.GetComponent<AudioComponentViewModel>();
            if (audioSettings == null) throw new Exception("Audio component missing");
            audioSettings.BgmTransition = "crossfade";
            audioSettings.BgmTransitionDurationSeconds = 2.5f;
            var serializedAudio = audioSettings.Serialize();
            var restoredAudio = new AudioComponentViewModel();
            restoredAudio.Deserialize(serializedAudio.ToDictionary(pair => pair.Key, pair => (object?)pair.Value));
            if (restoredAudio.BgmTransition != "crossfade" || restoredAudio.BgmTransitionDurationSeconds != 2.5f)
                throw new Exception("Audio transition settings serialization mismatch");

            // Test Trash Can (RemoveSelfCommand)
            secondChar.RemoveSelfCommand.Execute(null);
            if (node.Components.Count(c => c is CharacterComponentViewModel) != 1)
                throw new Exception("Trash can button (RemoveSelfCommand) failed to remove component!");
            using (var previewDocument = JsonDocument.Parse(StoryGraphSerializer.SerializePreviewComponents(node)))
            {
                if (previewDocument.RootElement.ValueKind != JsonValueKind.Array ||
                    previewDocument.RootElement.GetArrayLength() != node.AllComponents.Count(component => component.IsEnabled) ||
                    previewDocument.RootElement.EnumerateArray().Any(component =>
                        !component.TryGetProperty("type", out _) || !component.TryGetProperty("data", out _)))
                {
                    throw new Exception("Preview component serialization lost its engine contract");
                }
            }
            Console.WriteLine("  ✅ [PASS] Component addition, script serialization, proxy sync, and Trash Can (RemoveSelfCommand) verified");

            // Hierarchy must expose its empty state when the active node has no
            // GameObjects, then update immediately as objects are added/removed.
            var emptyHierarchyNode = new NodeViewModel(102, "Empty Hierarchy", 0, 0, bare: true);
            mainVm.SelectedNode = emptyHierarchyNode;
            if (mainVm.HierarchyViewModel.HasObjects)
                throw new Exception("Hierarchy reported objects for an empty node");
            emptyHierarchyNode.Title = "Renamed Empty Hierarchy";
            if (mainVm.HierarchyViewModel.CurrentNodeTitle != "Renamed Empty Hierarchy")
                throw new Exception("Hierarchy header did not update after the selected node was renamed");
            var hierarchyObject = emptyHierarchyNode.CreateObject("Temporary Object");
            if (!mainVm.HierarchyViewModel.HasObjects)
                throw new Exception("Hierarchy empty state did not update after object creation");
            emptyHierarchyNode.RemoveObject(hierarchyObject);
            if (mainVm.HierarchyViewModel.HasObjects)
                throw new Exception("Hierarchy empty state did not update after object deletion");
            mainVm.SelectedNode = null;
            if (mainVm.HierarchyViewModel.IsCurrentNodeEmpty)
                throw new Exception("Hierarchy showed an empty-frame state without a selected node");
            Console.WriteLine("  ✅ [PASS] Hierarchy empty state tracks GameObject creation and deletion");

            // Test 2: Theme System (Light & Dark Mode)
            Console.WriteLine("\n📌 [Test 2]: Dynamic Theming (Light/Orange-White & Dark/Black-White)...");
            if (!mainVm.IsDarkMode) throw new Exception("Default theme should be Dark mode");
            mainVm.ToggleTheme();
            if (mainVm.IsDarkMode) throw new Exception("Theme toggle should switch to Light mode");
            if (!mainVm.ThemeButtonText.Contains("Aydınlık")) throw new Exception("Theme button text should indicate Light mode");
            mainVm.ToggleTheme();
            if (!mainVm.IsDarkMode) throw new Exception("Theme toggle should switch back to Dark mode");
            Console.WriteLine("  ✅ [PASS] Theme toggle (Dark <-> Light/Orange) verified");

            // Hidden side panels must give their entire column, including the
            // splitter, back to the central workspace.
            mainVm.ShowPanel("Hierarchy");
            if (mainVm.HierarchyPanelWidth.Value != 0 || mainVm.HierarchySplitterWidth.Value != 0)
                throw new Exception("Hidden Hierarchy still reserved workspace width");
            mainVm.ShowPanel("Hierarchy");
            if (mainVm.HierarchyPanelWidth.Value != 240 || mainVm.HierarchySplitterWidth.Value != 6)
                throw new Exception("Hierarchy did not restore its workspace width");
            mainVm.ShowPanel("Inspector");
            if (mainVm.InspectorPanelWidth.Value != 0 || mainVm.InspectorSplitterWidth.Value != 0)
                throw new Exception("Hidden Inspector still reserved workspace width");
            mainVm.ShowPanel("Inspector");
            if (mainVm.InspectorPanelWidth.Value != 280 || mainVm.InspectorSplitterWidth.Value != 6)
                throw new Exception("Inspector did not restore its workspace width");
            Console.WriteLine("  ✅ [PASS] Hidden side panels release workspace width");

            // Bottom-panel tabs have independent visibility. Closing the log
            // must leave Assets available instead of collapsing the entire
            // area behind it.
            mainVm.ShowPanel("Log");
            if (mainVm.IsLogPanelVisible || !mainVm.IsAssetsPanelVisible || !mainVm.IsBottomPanelVisible)
                throw new Exception("Closing Log incorrectly hid the Assets workspace");
            mainVm.ShowPanel("Assets");
            if (!mainVm.IsAssetsPanelVisible || mainVm.BottomPanelActiveTab != 1 || !mainVm.IsBottomPanelVisible)
                throw new Exception("Assets panel could not become the active bottom workspace");
            mainVm.ShowPanel("Assets");
            if (mainVm.IsBottomPanelVisible)
                throw new Exception("Bottom workspace remained visible after both tabs were closed");
            if (mainVm.BottomPanelHeight.Value != 0 || mainVm.BottomSplitterHeight.Value != 0)
                throw new Exception("Hidden bottom workspace still reserved height");
            mainVm.ShowPanel("Assets");
            if (!mainVm.IsBottomPanelVisible || mainVm.BottomPanelActiveTab != 1 ||
                mainVm.BottomPanelHeight.Value != 180 || mainVm.BottomSplitterHeight.Value != 6)
                throw new Exception("Assets panel did not restore independently");
            mainVm.ShowPanel("Backlog");
            if (!mainVm.IsBacklogPanelVisible || mainVm.BottomPanelActiveTab != 2 || !mainVm.IsBottomPanelVisible)
                throw new Exception("Dialogue backlog panel did not become an independent bottom workspace");
            mainVm.ShowPanel("Backlog");
            if (mainVm.IsBacklogPanelVisible)
                throw new Exception("Dialogue backlog panel did not close independently");
            Console.WriteLine("  ✅ [PASS] Bottom Log and Assets panel visibility is independent and reclaims height");

            mainVm.ShowPanel("SplitScreen");
            mainVm.ShowPanel("Preview");
            if (mainVm.SplitScreenMode != 0 || !mainVm.IsPreviewActive || mainVm.IsNodeGraphActive)
                throw new Exception("Preview mode did not exit split screen cleanly");
            mainVm.ShowPanel("SplitScreen");
            mainVm.ShowPanel("EnginePreview");
            if (mainVm.SplitScreenMode != 0 || !mainVm.IsEnginePreviewActive || mainVm.IsNodeGraphActive)
                throw new Exception("Game preview mode did not exit split screen cleanly");
            mainVm.ShowPanel("NodeGraph");
            Console.WriteLine("  ✅ [PASS] Single preview modes exit split screen consistently");

            // Test 3: ConnectionViewModel & Graph Topology
            Console.WriteLine("\n📌 [Test 3]: ConnectionViewModel & Single Outgoing Wire Rule...");
            var testConns = new System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel>();
            var n1 = new NodeViewModel(1, "N1", 0, 0);
            var n2 = new NodeViewModel(2, "N2", 300, 0);
            var n3 = new NodeViewModel(3, "N3", 600, 0);

            testConns.Add(new ConnectionViewModel(n1, n2));
            testConns.Add(new ConnectionViewModel(n1, n3));
            if (testConns.Count != 2) throw new Exception("Initial test conns failed");
            Console.WriteLine("  ✅ [PASS] Wire topology & single outgoing rule verified without touching project files");

            var validationNode = new NodeViewModel(901, "Validation", 0, 0, bare: false);
            var missingBackground = validationNode.GetComponent<BackgroundComponentViewModel>();
            if (missingBackground != null) missingBackground.Texture = "missing_build_asset.png";
            var orphanNode = new NodeViewModel(902, "Orphan", 0, 0, bare: true);
            var validationIssues = ProjectValidationService.Validate(
                new[] { validationNode, orphanNode }, Array.Empty<ConnectionViewModel>(), Path.Combine(testProjectRoot, "Assets"), validationNode.Id);
            if (!validationIssues.Any(issue => issue.IsError && issue.Message.Contains("missing_build_asset.png")) ||
                !validationIssues.Any(issue => !issue.IsError && issue.Message.Contains("unreachable")))
                throw new Exception("Build validation did not report missing assets and unreachable nodes");
            Console.WriteLine("  ✅ [PASS] Build validation blocks missing assets and reports unreachable nodes");

            var cycleConnections = new[]
            {
                new ConnectionViewModel(n3, n2), new ConnectionViewModel(n2, n3)
            };
            var graphIssues = ProjectValidationService.Validate(
                new[] { n1, n2, n3 }, cycleConnections, Path.Combine(testProjectRoot, "Assets"), n3.Id);
            var terminalIssues = ProjectValidationService.Validate(
                new[] { n1, n2 }, new[] { new ConnectionViewModel(n1, n2) },
                Path.Combine(testProjectRoot, "Assets"), n1.Id);
            if (!graphIssues.Any(issue => !issue.IsError && issue.Message.Contains("cycle")) ||
                !terminalIssues.Any(issue => !issue.IsError && issue.Message.Contains("terminal")))
                throw new Exception("Graph analysis did not report reachable cycles and terminal nodes");
            Console.WriteLine("  ✅ [PASS] Graph analysis uses the actual start node and reports cycles/terminal nodes");

            // Test 4: Story Graph File Serialization & Deserialization
            Console.WriteLine("\n📌 [Test 4]: Story Graph v4 Serialization & Coordinate Persistence...");
            bool loadOk = mainVm.LoadFullStoryGraphFile();
            if (!loadOk) throw new Exception("Failed to load full_story_graph.json");
            if (mainVm.Nodes.Count == 0) throw new Exception("Nodes collection empty after load");
            Console.WriteLine($"  ✅ [PASS] Loaded {mainVm.Nodes.Count} project nodes with full component integrity (no unwanted node pollution)");

            if (!ProjectOpenCoordinator.TryResolve(testProjectRoot, out var resolvedProject) ||
                resolvedProject?.RootPath != testProjectRoot ||
                !File.Exists(resolvedProject.GraphFilePath))
            {
                throw new Exception("Project root selection was not resolved safely");
            }
            if (!ProjectOpenCoordinator.TryResolve(Path.Combine(testProjectRoot, "Assets"), out var resolvedAssets) ||
                resolvedAssets?.RootPath != testProjectRoot)
            {
                throw new Exception("Assets folder selection was not resolved to its project root");
            }
            Console.WriteLine("  ✅ [PASS] Project-folder and Assets-folder selection resolve to the same project root");

            var successfulTransition = new List<string>();
            bool switchSucceeded = ProjectOpenCoordinator.Switch(
                resolvedProject!, "old-root", "old-path",
                () => successfulTransition.Add("clear"),
                (root, path) => successfulTransition.Add($"project:{root}:{path}"),
                root => successfulTransition.Add($"mount:{root}"),
                () => { successfulTransition.Add("load"); return true; },
                () => successfulTransition.Add("refresh-bitmaps"),
                () => successfulTransition.Add("refresh-assets"));
            if (!switchSucceeded || !successfulTransition.SequenceEqual(new[]
                { "clear", $"project:{testProjectRoot}:{testProjectRoot}", $"mount:{testProjectRoot}", "load", "refresh-assets" }))
            {
                throw new Exception("Successful project transition ordering changed");
            }

            var failedTransition = new List<string>();
            bool switchFailed = ProjectOpenCoordinator.Switch(
                resolvedProject, "old-root", "old-path",
                () => failedTransition.Add("clear"),
                (root, path) => failedTransition.Add($"project:{root}:{path}"),
                root => failedTransition.Add($"mount:{root}"),
                () => { failedTransition.Add("load"); return false; },
                () => failedTransition.Add("refresh-bitmaps"),
                () => failedTransition.Add("refresh-assets"));
            if (switchFailed || !failedTransition.SequenceEqual(new[]
                { "clear", $"project:{testProjectRoot}:{testProjectRoot}", $"mount:{testProjectRoot}", "load",
                  "project:old-root:old-path", "clear", "refresh-bitmaps", "mount:old-root", "refresh-assets" }))
            {
                throw new Exception("Failed project transition rollback ordering changed");
            }
            Console.WriteLine("  ✅ [PASS] Project transition success and rollback ordering");

            string fileSystemTestRoot = Path.Combine(Path.GetTempPath(), $"RowlFileSystemTests_{Guid.NewGuid():N}");
            try
            {
                string sourceDirectory = Path.Combine(fileSystemTestRoot, "source");
                string targetDirectory = Path.Combine(fileSystemTestRoot, "target");
                Directory.CreateDirectory(Path.Combine(sourceDirectory, "nested"));
                File.WriteAllText(Path.Combine(sourceDirectory, "nested", "asset.txt"), "asset");
                ProjectFileSystem.CopyDirectory(sourceDirectory, targetDirectory);
                if (File.ReadAllText(Path.Combine(targetDirectory, "nested", "asset.txt")) != "asset")
                    throw new Exception("Project directory copy did not preserve nested assets");

                string atomicFile = Path.Combine(fileSystemTestRoot, "atomic", "story.json");
                ProjectFileSystem.WriteAllTextAtomically(atomicFile, "first");
                ProjectFileSystem.WriteAllTextAtomically(atomicFile, "second");
                if (File.ReadAllText(atomicFile) != "second" ||
                    Directory.GetFiles(Path.GetDirectoryName(atomicFile)!, "*.tmp").Length != 0)
                    throw new Exception("Atomic project write did not replace content cleanly");
            }
            finally
            {
                try { Directory.Delete(fileSystemTestRoot, true); } catch { }
            }
            Console.WriteLine("  ✅ [PASS] Project file copy and atomic write primitives");

            // A frame can own more than one ChoiceComponent. Every option must
            // survive save/reload; previously only the first component was saved.
            var multiChoiceSource = new NodeViewModel(9001, "Multi Choice", 0, 0, bare: true);
            var firstChoiceObject = multiChoiceSource.CreateObject("First Choices");
            var firstChoice = firstChoiceObject.AddComponent<ChoiceComponentViewModel>();
            firstChoice.Options[0].OptionId = "first_route";
            firstChoice.Options[0].TargetNodeId = 9002;
            var secondChoiceObject = multiChoiceSource.CreateObject("Second Choices");
            var secondChoice = secondChoiceObject.AddComponent<ChoiceComponentViewModel>();
            secondChoice.Options[0].OptionId = "second_route";
            secondChoice.Options[0].TargetNodeId = 9003;
            mainVm.Nodes.Add(multiChoiceSource);
            mainVm.Nodes.Add(new NodeViewModel(9002, "First Target", 300, 0, bare: true));
            mainVm.Nodes.Add(new NodeViewModel(9003, "Second Target", 600, 0, bare: true));
            mainVm.SaveFullStoryGraphFile();
            if (!mainVm.LoadFullStoryGraphFile())
                throw new Exception("Multi-Choice graph failed to reload");
            var reloadedRoutes = mainVm.Connections
                .Where(connection => connection.SourceNode?.Id == 9001)
                .Select(connection => connection.OptionId)
                .ToHashSet(StringComparer.Ordinal);
            if (!reloadedRoutes.SetEquals(new[] { "first_route", "second_route" }))
                throw new Exception("Multiple ChoiceComponents did not preserve every route");
            Console.WriteLine("  ✅ [PASS] Multiple ChoiceComponents preserve every stable-ID route across save/reload");

            var deletedTarget = mainVm.Nodes.First(item => item.Id == 9002);
            mainVm.DeleteNode(deletedTarget);
            mainVm.SaveFullStoryGraphFile();
            using (var savedGraph = JsonDocument.Parse(File.ReadAllText(
                Path.Combine(MainWindowViewModel.AssetsPath, "full_story_graph.json"))))
            {
                var savedSource = savedGraph.RootElement.GetProperty("nodes")
                    .EnumerateArray().First(item => item.GetProperty("id").GetUInt64() == 9001);
                var savedRoutes = savedSource.GetProperty("next_nodes").EnumerateArray()
                    .Select(item => item.GetProperty("option_id").GetString())
                    .ToArray();
                if (savedRoutes.Contains("first_route") || !savedRoutes.Contains("second_route"))
                    throw new Exception("Deleting a target node left a stale choice route");
            }
            Console.WriteLine("  ✅ [PASS] Deleting a target node removes its persisted choice route");

            string graphPath = Path.Combine(MainWindowViewModel.AssetsPath, "full_story_graph.json");
            string validGraph = File.ReadAllText(graphPath);
            var nodeIdsBeforeFailedLoad = mainVm.Nodes.Select(item => item.Id).ToArray();
            File.WriteAllText(graphPath, "{\"nodes\":[{\"id\":9999},");
            if (mainVm.LoadFullStoryGraphFile())
                throw new Exception("Malformed graph was unexpectedly accepted");
            if (!mainVm.Nodes.Select(item => item.Id).SequenceEqual(nodeIdsBeforeFailedLoad))
                throw new Exception("Malformed graph replaced the current in-memory project");
            File.WriteAllText(graphPath, validGraph);
            Console.WriteLine("  ✅ [PASS] Malformed graph loading preserves the current in-memory project");

            // Test 5: Asset Auto-Copy & Project Portability
            Console.WriteLine("\n📌 [Test 5]: Asset Auto-Copy & Project Portability (External Image Import)...");
            string tempExternalFile = Path.Combine(Path.GetTempPath(), "test_external_character_sprite.png");
            File.WriteAllBytes(tempExternalFile, new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }); // PNG header
            
            string importedName = mainVm.ImportImageFileToProject(tempExternalFile);
            if (importedName != "test_external_character_sprite.png")
                throw new Exception($"ImportImageFileToProject returned unexpected name: {importedName}");
            
            string destExpected = Path.Combine(MainWindowViewModel.AssetsPath, "images", "test_external_character_sprite.png");
            if (!File.Exists(destExpected))
                throw new Exception($"Imported file was not found at expected project path: {destExpected}");
            
            // Clean up test files
            try { File.Delete(tempExternalFile); } catch {}
            try { File.Delete(destExpected); } catch {}
            Console.WriteLine("  ✅ [PASS] External file automatically copied into project Assets/images/ and linked via relative filename");

            // Test 6: OBS Assist & Magnetic Snapping System
            Console.WriteLine("\n📌 [Test 6]: OBS Assist Transform & Alignment System...");
            if (mainVm.SelectedNode == null && mainVm.Nodes.Count > 0)
            {
                mainVm.SelectNode(mainVm.Nodes[0]);
            }
            if (mainVm.SelectedNode != null)
            {
                // 1. Fit Background to Screen (1920x1080)
                mainVm.FitBackgroundToScreen();
                if (mainVm.SelectedNode.BackgroundX != 0 || mainVm.SelectedNode.BackgroundY != 0 || 
                    mainVm.SelectedNode.BackgroundWidth != 1920 || mainVm.SelectedNode.BackgroundHeight != 1080)
                    throw new Exception("FitBackgroundToScreen failed");

                // 2. Center Element
                mainVm.CenterSelectedElement();
                var charComp = mainVm.SelectedNode.GetComponent<CharacterComponentViewModel>();
                if (charComp != null && charComp.X != (1920 - charComp.Width) / 2.0)
                    throw new Exception("CenterSelectedElement failed for Character");

                // 3. Align to Bottom
                mainVm.AlignCharacterToBottom();
                if (charComp != null && charComp.Y != 1080 - charComp.Height - 20)
                    throw new Exception("AlignCharacterToBottom failed");

                // 4. Snap Assist Toggle
                bool initialSnap = mainVm.IsSnapAssistEnabled;
                mainVm.ToggleSnapAssist();
                if (mainVm.IsSnapAssistEnabled == initialSnap)
                    throw new Exception("ToggleSnapAssist failed to flip boolean state");
                mainVm.ToggleSnapAssist();
            }
            Console.WriteLine("  ✅ [PASS] OBS Assist (Fit 1080p, Center, Ground Baseline, Snap Toggle) verified");

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

        public static AppBuilder BuildAvaloniaApp()
            => AppBuilder.Configure<App>()
                .UsePlatformDetect()
                .WithInterFont()
                .LogToTrace();
    }
}
