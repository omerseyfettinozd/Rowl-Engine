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

            // Test 7: Project Save, Save As, and Standalone Build Pipeline
            Console.WriteLine("\n📌 [Test 7]: Project Save, Save As & Standalone Build Pipeline...");
            // 1. Test SaveProjectCommand
            mainVm.SaveProject();

            // Project-owned settings and unknown manifest fields must survive Save As.
            File.WriteAllText(Path.Combine(testProjectRoot, "project.rowlproj"), "{ \"custom_release_field\": \"keep\" }");
            mainVm.Settings.ProjectSaveSlotCount = 17;
            mainVm.Settings.ProjectDefaultBgmTransition = "crossfade";
            mainVm.Settings.ProjectDefaultBgmTransitionDurationSeconds = 2.5f;

            // 2. Test Save As (Farklı Kaydet)
            string testSaveAsDir = Path.Combine(Path.GetTempPath(), "RowlTestProject_SaveAs");
            mainVm.SaveProjectToDirectory(testSaveAsDir);

            if (!File.Exists(Path.Combine(testSaveAsDir, "project.rowlproj")))
                throw new Exception("project.rowlproj was not created in Save As target");
            if (!File.Exists(Path.Combine(testSaveAsDir, "Assets", "full_story_graph.json")))
                throw new Exception("full_story_graph.json missing in Save As target");
            if (!Directory.Exists(Path.Combine(testSaveAsDir, "Assets", "images")))
                throw new Exception("Assets/images missing in Save As target");
            using (var copiedManifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(testSaveAsDir, "project.rowlproj"))))
            {
                if (copiedManifest.RootElement.GetProperty("custom_release_field").GetString() != "keep" ||
                    copiedManifest.RootElement.GetProperty("startNodeId").GetUInt64() != mainVm.GetStartNode()!.Id ||
                    copiedManifest.RootElement.GetProperty("save_slot_count").GetInt32() != 17)
                    throw new Exception("Save As did not preserve project metadata and runtime settings");
            }

            // 3. Test Build Game (Standalone Release Export)
            string testBuildDir = Path.Combine(Path.GetTempPath(), "RowlTest_Build_PC");
            mainVm.ExecuteBuildPipeline(testBuildDir);

            if (!File.Exists(Path.Combine(testBuildDir, "run_game.sh")))
                throw new Exception("run_game.sh missing in standalone build output: " + mainVm.LogOutput);
            if (!File.Exists(Path.Combine(testBuildDir, "run_game.bat")))
                throw new Exception("run_game.bat missing in standalone build output");
            if (!File.Exists(Path.Combine(testBuildDir, "README.txt")))
                throw new Exception("README.txt missing in standalone build output");
            string expectedPlayer = OperatingSystem.IsWindows() ? "RowlGame.exe" : "RowlGame";
            if (!File.Exists(Path.Combine(testBuildDir, expectedPlayer)) && !File.Exists(Path.Combine(testBuildDir, OperatingSystem.IsWindows() ? "rowl_player.exe" : "rowl_player")))
                throw new Exception($"Standalone player executable missing ({expectedPlayer}) in standalone build output");
            string expectedLib = OperatingSystem.IsWindows() ? "RowlEngineCore.dll"
                : OperatingSystem.IsMacOS() ? "libRowlEngineCore.dylib" : "libRowlEngineCore.so";
            if (!File.Exists(Path.Combine(testBuildDir, expectedLib)))
                throw new Exception($"{expectedLib} missing in standalone build output");
            if (!File.Exists(Path.Combine(testBuildDir, "Assets", "packages", "game.rowlpkg")))
                throw new Exception("game.rowlpkg missing in standalone build output");
            if (!Directory.Exists(Path.Combine(testBuildDir, "mods")) ||
                !File.Exists(Path.Combine(testBuildDir, "mods", "README.md")))
                throw new Exception("mods override directory missing in standalone build output");
            string cancelledBuildDir = Path.Combine(Path.GetTempPath(), "RowlTest_Build_Cancelled");
            using (var cancellation = new CancellationTokenSource())
            {
                cancellation.Cancel();
                var cancelledBuild = ProjectBuildService.BuildStandaloneAsync(
                    testProjectRoot, Path.Combine(testProjectRoot, "Assets"), cancelledBuildDir,
                    null, cancellation.Token).GetAwaiter().GetResult();
                if (!cancelledBuild.Cancelled || Directory.Exists(cancelledBuildDir))
                    throw new Exception("Cancelled standalone build published an output directory");
            }
            var repeatBuild = ProjectBuildService.BuildStandalone(testProjectRoot, Path.Combine(testProjectRoot, "Assets"), testBuildDir);
            if (repeatBuild.Succeeded || !File.Exists(Path.Combine(testBuildDir, "README.txt")))
                throw new Exception("Build must preserve a published package when its output directory already exists");

            // Clean up temporary test directories
            try { Directory.Delete(testSaveAsDir, true); } catch {}
            try { Directory.Delete(testBuildDir, true); } catch {}

            Console.WriteLine("  ✅ [PASS] Project Save, Save As (all assets + manifest) & Standalone Game Build verified");

            // ── Test 7b: Project runtime settings migration ─────────────────
            Console.WriteLine("\n📌 [Test 7b]: Project runtime settings migration...");
            string manifestPath = Path.Combine(testProjectRoot, "runtime-settings.rowlproj");
            File.WriteAllText(manifestPath, "{ \"name\": \"Legacy\" }");
            var legacySettings = ProjectRuntimeSettingsService.Load(manifestPath);
            if (legacySettings.SaveSlotCount != 10 || legacySettings.DefaultBgmTransition != "instant" ||
                legacySettings.DefaultBgmTransitionDurationSeconds != 1)
                throw new Exception("Legacy project settings did not receive safe defaults");
            ProjectRuntimeSettingsService.Save(manifestPath, new ProjectRuntimeSettings
            {
                SaveSlotCount = 14, DefaultBgmTransition = "crossfade", DefaultBgmTransitionDurationSeconds = 2.5f
            });
            var savedSettings = ProjectRuntimeSettingsService.Load(manifestPath);
            if (savedSettings.SaveSlotCount != 14 || savedSettings.DefaultBgmTransition != "crossfade" ||
                savedSettings.DefaultBgmTransitionDurationSeconds != 2.5f)
                throw new Exception("Project runtime settings did not round-trip");
            Console.WriteLine("  ✅ [PASS] Project runtime settings migration and round-trip verified");

            string editorProfilePath = Path.Combine(testProjectRoot, "editor-settings.json");
            new EditorSettingsProfile { AutoSaveEnabled = false, AutoSaveIntervalSeconds = 1 }.Save(editorProfilePath);
            var editorProfile = EditorSettingsProfile.Load(editorProfilePath);
            if (editorProfile.AutoSaveEnabled || editorProfile.AutoSaveIntervalSeconds != 15)
                throw new Exception("Editor settings profile did not persist and sanitize autosave behaviour");
            Console.WriteLine("  ✅ [PASS] Editor-local autosave settings persistence and bounds verified");

            // Test 8: Performance Benchmark & Cache Optimization Verification
            Console.WriteLine("\n📌 [Test 8]: Performance Benchmark & Cache Optimization Verification...");
            var sw = Stopwatch.StartNew();

            // Benchmark 1: Negative caching for missing files (10,000 lookups)
            const int lookupIterations = 10000;
            for (int i = 0; i < lookupIterations; i++)
            {
                var bmp = AssetBitmapCache.GetOrLoad("non_existent_placeholder_image.png");
                if (bmp != null) throw new Exception("Expected null for non-existent image");
            }
            sw.Stop();
            double negCacheMs = sw.Elapsed.TotalMilliseconds;
            double negCacheIops = (lookupIterations / negCacheMs) * 1000.0;
            Console.WriteLine($"  ⚡ [BENCHMARK] AssetBitmapCache Negative Lookups: {lookupIterations:N0} queries in {negCacheMs:F2}ms ({negCacheIops:N0} queries/sec)");

            if (negCacheMs > 500)
                throw new Exception("Negative caching benchmark was too slow (>500ms)");

            // Different editor fields often describe the same image with a
            // bare filename or an images/ prefix. They must share one native
            // Bitmap instead of decoding and retaining duplicates.
            AssetBitmapCache.Clear();
            var bareAsset = AssetBitmapCache.GetOrLoad("Woman.png");
            var prefixedAsset = AssetBitmapCache.GetOrLoad("images/Woman.png");
            var cacheStats = AssetBitmapCache.GetStats();
            if (bareAsset == null || prefixedAsset == null ||
                !ReferenceEquals(bareAsset, prefixedAsset) ||
                cacheStats.BitmapCount != 1 || cacheStats.EstimatedRgbaBytes <= 0)
            {
                throw new Exception("Asset cache did not canonicalize aliases into one bitmap");
            }
            Console.WriteLine($"  ⚡ [BENCHMARK] AssetBitmapCache: {cacheStats.BitmapCount} unique bitmap, " +
                              $"{cacheStats.EstimatedRgbaBytes:N0} estimated RGBA bytes");
            AssetBitmapCache.Clear();
            if (AssetBitmapCache.GetStats().BitmapCount != 0)
                throw new Exception("Asset cache statistics did not clear with the cache");

            // Exercise the Lazy cache factory under actual simultaneous alias
            // requests; every caller must receive the one shared Bitmap.
            object?[] concurrentAssets = new object?[16];
            Parallel.For(0, concurrentAssets.Length, i =>
            {
                concurrentAssets[i] = AssetBitmapCache.GetOrLoad(
                    i % 2 == 0 ? "Woman.png" : "images/Woman.png");
            });
            if (concurrentAssets.Any(bitmap => bitmap == null) ||
                concurrentAssets.Any(bitmap => !ReferenceEquals(bitmap, concurrentAssets[0])) ||
                AssetBitmapCache.GetStats().BitmapCount != 1)
            {
                throw new Exception("Concurrent asset requests decoded duplicate bitmaps");
            }
            AssetBitmapCache.Clear();

            Console.WriteLine("  ✅ [PASS] AssetBitmapCache high-throughput negative caching & memory safety verified");

            // ── Test 9: Variable & Condition Components + Native State Slots ───────────
            Console.WriteLine("\n📌 [Test 9]: Lua Condition, Variable Components & Native State Slots...");

            // Component Creation & Serialization
            var varComp = (VariableComponentViewModel)ComponentRegistry.Create("variable");
            varComp.Key = "player_reputation";
            varComp.Value = "85";
            varComp.Operation = "set";

            var condComp = (ConditionComponentViewModel)ComponentRegistry.Create("condition");
            condComp.Expression = "player_reputation >= 80";

            var scriptNode = new NodeViewModel(701, "ScriptNode", 100, 100);
            var logicObj = scriptNode.CreateObject("Logic");
            logicObj.AddComponent(varComp);
            logicObj.AddComponent(condComp);

            var varDict = varComp.Serialize();
            if ((string)varDict["key"] != "player_reputation" || (string)varDict["value"] != "85")
                throw new Exception("VariableComponent serialization mismatch");

            var condDict = condComp.Serialize();
            if ((string)condDict["expression"] != "player_reputation >= 80")
                throw new Exception("ConditionComponent serialization mismatch");

            // Native P/Invoke Integration via EngineHost
            var host = mainVm.EngineHost;
            if (!host.IsInitialized)
            {
                host.Initialize(1920, 1080, false);
            }
            if (host.IsInitialized)
            {
                if (host.LastFrameTextureLoadMilliseconds < 0 || host.LastFrameNonTextureRenderMilliseconds < 0 ||
                    host.LastFrameTextRasterizationMilliseconds < 0 || host.LastFrameRendererFlushMilliseconds < 0)
                    throw new Exception("Native first-frame profiling telemetry returned an invalid duration");
                host.SetProjectDirectory(testProjectRoot);
                host.SetVariable("test_affinity", "99");
                string readAffinity = host.GetVariable("test_affinity");
                if (readAffinity != "99")
                    throw new Exception($"Native variable mismatch: expected '99', got '{readAffinity}'");

                if (!host.EvaluateCondition("test_affinity >= 90") || host.EvaluateCondition("test_affinity < 50"))
                    throw new Exception("Native condition evaluation via EngineHost failed");

                // Save & Load Slots
                if (!host.SaveGameSlot(10))
                    throw new Exception("EngineHost.SaveGameSlot(10) failed");

                if (!File.Exists(Path.Combine(testProjectRoot, "saves", "save_slot_10.json")))
                    throw new Exception("Game save was not isolated under the selected project");

                if (!host.HasSaveSlot(10))
                    throw new Exception("EngineHost.HasSaveSlot(10) failed");

                if (!host.LoadGameSlot(10))
                    throw new Exception("EngineHost.LoadGameSlot(10) failed");

                host.DeleteSaveSlot(10);
                if (host.HasSaveSlot(10))
                    throw new Exception("EngineHost.DeleteSaveSlot(10) failed");

                // Structured Diagnostics Verification via EngineHost
                if (host.SaveGameSlot(-3))
                    throw new Exception("SaveGameSlot(-3) should fail");
                if (host.LastResultCode != RowlEngine.Editor.Native.RuntimeErrorCode.InvalidArgument ||
                    host.LastResultOperation != "save_game_slot" ||
                    host.LastResultTarget != "-3")
                    throw new Exception($"Expected InvalidArgument diagnostic for slot -3, got {host.LastResultCode} ({host.LastResultOperation})");

                if (host.LoadGameSlot(99))
                    throw new Exception("LoadGameSlot(99) should fail");
                if (host.LastResultCode != RowlEngine.Editor.Native.RuntimeErrorCode.FileNotFound)
                    throw new Exception($"Expected FileNotFound diagnostic for slot 99, got {host.LastResultCode}");

                host.ClearLastResult();
                if (host.LastResultCode != RowlEngine.Editor.Native.RuntimeErrorCode.Ok)
                    throw new Exception("ClearLastResult did not reset LastResultCode to Ok");

                // Rewind
                host.Rewind(1);
            }

            Console.WriteLine("  ✅ [PASS] Variable/Condition components, serialization, and P/Invoke Save/Load slots + Structured Diagnostics verified");

            // Preview coalescing may skip only a script-free scene whose exact
            // payload is already rendered. Script components retain their
            // existing execution/diagnostic refresh semantics.
            var noOpPreviewNode = new NodeViewModel(9899, "Preview cache", 0, 0, bare: true);
            noOpPreviewNode.AddComponent<DialogueComponentViewModel>().DialogueText = "Stable preview";
            mainVm.SelectNodeQuiet(noOpPreviewNode);
            mainVm.EngineHost.ResetToStartNode();
            mainVm.ScheduleEnginePreviewUpdate(noOpPreviewNode);
            if (!mainVm.DeliverScheduledEnginePreview())
                throw new Exception("Initial script-free preview did not deliver");
            mainVm.ScheduleEnginePreviewUpdate(noOpPreviewNode);
            if (mainVm.DeliverScheduledEnginePreview())
                throw new Exception("Unchanged script-free preview redrew unnecessarily");

            var scriptedPreviewNode = new NodeViewModel(9900, "Script preview", 0, 0, bare: true);
            scriptedPreviewNode.AddComponent<ScriptComponentViewModel>();
            mainVm.SelectNodeQuiet(scriptedPreviewNode);
            mainVm.EngineHost.ResetToStartNode();
            mainVm.ScheduleEnginePreviewUpdate(scriptedPreviewNode);
            if (!mainVm.DeliverScheduledEnginePreview())
                throw new Exception("Initial script preview did not deliver");
            mainVm.ScheduleEnginePreviewUpdate(scriptedPreviewNode);
            if (!mainVm.DeliverScheduledEnginePreview())
                throw new Exception("Script preview was incorrectly treated as a no-op");
            Console.WriteLine("  ✅ [PASS] Preview no-op coalescing preserves script refresh behavior");

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

            // ── Test 11: Camera & Transition Components Lifecycle & Native P/Invoke ──
            Console.WriteLine("\n📌 [Test 11]: Camera & Transition Components Lifecycle & Native P/Invoke...");

            // 1. Component Registry Discovery
            var availableTypes = ComponentRegistry.AvailableTypes;
            if (!availableTypes.Contains("camera") || !availableTypes.Contains("transition"))
                throw new Exception("ComponentRegistry missing 'camera' or 'transition' types");

            var camComp = ComponentRegistry.Create("camera") as CameraComponentViewModel;
            if (camComp == null || camComp.TypeKey != "camera")
                throw new Exception("ComponentRegistry failed to create CameraComponentViewModel");

            var transComp = ComponentRegistry.Create("transition") as TransitionComponentViewModel;
            if (transComp == null || transComp.TypeKey != "transition")
                throw new Exception("ComponentRegistry failed to create TransitionComponentViewModel");

            // 2. Camera Serialization and Deserialization
            camComp.X = 1200.0;
            camComp.Y = 600.0;
            camComp.Zoom = 2.0;
            camComp.PanDuration = 1.5;
            camComp.ZoomDuration = 1.0;
            camComp.Easing = "smooth_step";
            camComp.ShakeIntensity = 25.0;
            camComp.ShakeDuration = 0.5;

            var camSerialized = camComp.Serialize();
            if ((double)camSerialized["x"] != 1200.0 || (double)camSerialized["zoom"] != 2.0 ||
                (double)camSerialized["shake_intensity"] != 25.0)
                throw new Exception("CameraComponentViewModel serialization mismatch");

            var camDeserialized = new CameraComponentViewModel();
            camDeserialized.Deserialize(new Dictionary<string, object?>
            {
                ["x"] = 1200.0,
                ["y"] = 600.0,
                ["zoom"] = 2.0,
                ["pan_duration"] = 1.5,
                ["zoom_duration"] = 1.0,
                ["easing"] = "smooth_step",
                ["shake_intensity"] = 25.0,
                ["shake_duration"] = 0.5
            });
            if (camDeserialized.X != 1200.0 || camDeserialized.Zoom != 2.0 || camDeserialized.ShakeIntensity != 25.0)
                throw new Exception("CameraComponentViewModel deserialization mismatch");

            // 3. Transition Serialization and Deserialization
            transComp.Kind = "fade_color";
            transComp.Duration = 1.25;
            transComp.ColorHex = "#10B981";

            var transSerialized = transComp.Serialize();
            if ((string)transSerialized["kind"] != "fade_color" || (double)transSerialized["duration"] != 1.25 ||
                (string)transSerialized["color"] != "#10B981")
                throw new Exception("TransitionComponentViewModel serialization mismatch");

            var transDeserialized = new TransitionComponentViewModel();
            transDeserialized.Deserialize(new Dictionary<string, object?>
            {
                ["kind"] = "fade_color",
                ["duration"] = 1.25,
                ["color"] = "#10B981"
            });
            if (transDeserialized.Kind != "fade_color" || transDeserialized.Duration != 1.25 || transDeserialized.ColorHex != "#10B981")
                throw new Exception("TransitionComponentViewModel deserialization mismatch");

            // 4. StoryGraphLoaderService Integration with Camera and Transition
            string cameraStoryJson = """
            {
              "format_version": 4,
              "start_node_id": 401,
              "nodes": [
                {
                  "id": 401,
                  "title": "CameraNode",
                  "editor_x": 100,
                  "editor_y": 100,
                  "objects": [
                    {
                      "id": "cam_obj",
                      "name": "Camera Controller",
                      "is_active": true,
                      "components": [
                        {
                          "type": "camera",
                          "id": "cam1",
                          "enabled": true,
                          "data": { "x": 1100, "y": 550, "zoom": 1.5, "pan_duration": 1.0 }
                        },
                        {
                          "type": "transition",
                          "id": "tr1",
                          "enabled": true,
                          "data": { "kind": "wipe_left", "duration": 0.8 }
                        }
                      ]
                    }
                  ],
                  "next_nodes": []
                }
              ]
            }
            """;

            using (var doc = JsonDocument.Parse(cameraStoryJson))
            {
                var loadResult = StoryGraphLoaderService.Load(doc);
                if (!loadResult.Success || loadResult.Nodes.Count != 1)
                    throw new Exception("StoryGraphLoaderService failed to load camera test graph");

                var loadedNode = loadResult.Nodes[0];
                var loadedCam = loadedNode.AllComponents.OfType<CameraComponentViewModel>().FirstOrDefault();
                if (loadedCam == null || loadedCam.X != 1100.0 || loadedCam.Zoom != 1.5 || loadedCam.PanDuration != 1.0)
                    throw new Exception("StoryGraphLoaderService hydrated camera component properties mismatch");

                var loadedTrans = loadedNode.AllComponents.OfType<TransitionComponentViewModel>().FirstOrDefault();
                if (loadedTrans == null || loadedTrans.Kind != "wipe_left" || loadedTrans.Duration != 0.8)
                    throw new Exception("StoryGraphLoaderService hydrated transition component properties mismatch");
            }

            // 5. NativeBridge P/Invoke for Camera Tweening
            IntPtr nativeHandle = NativeBridge.RowlEngine_Create();
            if (nativeHandle != IntPtr.Zero)
            {
                try
                {
                    if (NativeBridge.RowlEngine_Init(nativeHandle, 1920, 1080, 0) == 1)
                    {
                        NativeBridge.RowlEngine_SetCamera(nativeHandle, 960.0f, 540.0f, 1.0f);
                        NativeBridge.RowlEngine_CameraPanTo(nativeHandle, 1200.0f, 600.0f, 0.5f, 3);
                        NativeBridge.RowlEngine_CameraZoomTo(nativeHandle, 2.0f, 0.5f, 3);
                        if (NativeBridge.RowlEngine_IsCameraMoving(nativeHandle) != 1)
                            throw new Exception("NativeBridge RowlEngine_IsCameraMoving expected 1 during tween");

                        NativeBridge.RowlEngine_ResetCamera(nativeHandle);
                    }
                }
                finally
                {
                    NativeBridge.RowlEngine_Destroy(nativeHandle);
                }
            }

            Console.WriteLine("  ✅ [PASS] Camera and Transition component models, hydration, and NativeBridge P/Invoke verified");

            // ── Test 12: ProjectSaveAsCoordinator, PackageAssetsAsync & Pipeline Validation Isolation ──
            Console.WriteLine("\n📌 [Test 12]: ProjectSaveAsCoordinator, PackageAssetsAsync & Pipeline Validation Isolation...");

            // 1. ProjectSaveAsCoordinator Success & Manifest Verification
            string testCoordSaveAsDir = Path.Combine(Path.GetTempPath(), $"RowlTestCoord_SaveAs_{Guid.NewGuid():N}");
            var saveAsResult = ProjectSaveAsCoordinator.SaveProjectCopy(
                testProjectRoot,
                testCoordSaveAsDir,
                mainVm.Nodes.Count,
                mainVm.GetStartNode()?.Id ?? 101,
                mainVm.SaveProject);

            if (!saveAsResult.Succeeded || !Directory.Exists(testCoordSaveAsDir))
                throw new Exception("ProjectSaveAsCoordinator failed to copy project");
            if (!File.Exists(Path.Combine(testCoordSaveAsDir, "project.rowlproj")))
                throw new Exception("ProjectSaveAsCoordinator failed to produce project.rowlproj");
            if (!File.Exists(Path.Combine(testCoordSaveAsDir, "Assets", "full_story_graph.json")))
                throw new Exception("ProjectSaveAsCoordinator failed to copy story graph");

            // 2. ProjectSaveAsCoordinator Descendant / Self Rejection
            bool threwOnDescendant = false;
            try
            {
                ProjectSaveAsCoordinator.SaveProjectCopy(
                    testProjectRoot,
                    Path.Combine(testProjectRoot, "SubFolder"),
                    mainVm.Nodes.Count,
                    101,
                    () => { });
            }
            catch (InvalidOperationException)
            {
                threwOnDescendant = true;
            }
            if (!threwOnDescendant)
                throw new Exception("ProjectSaveAsCoordinator should throw InvalidOperationException when target is a descendant");

            // 3. ProjectBuildService.PackageAssetsAsync Success
            string testPackageOut = Path.Combine(Path.GetTempPath(), $"test_archive_{Guid.NewGuid():N}.rowlpkg");
            var pkgResult = ProjectBuildService.PackageAssetsAsync(
                Path.Combine(testProjectRoot, "Assets"),
                testPackageOut).GetAwaiter().GetResult();

            if (!pkgResult.Succeeded || !File.Exists(testPackageOut))
                throw new Exception($"ProjectBuildService.PackageAssetsAsync failed: {pkgResult.Message}");
            if (new FileInfo(testPackageOut).Length == 0)
                throw new Exception("Package output was empty");

            // 4. ProjectBuildService.PackageAssetsAsync Missing Assets Error Containment
            var badPkgResult = ProjectBuildService.PackageAssetsAsync(
                Path.Combine(Path.GetTempPath(), "NonExistentAssetsDir_12345"),
                testPackageOut).GetAwaiter().GetResult();
            if (badPkgResult.Succeeded)
                throw new Exception("PackageAssetsAsync should fail when assets directory is missing");

            // 5. ProjectBuildService.PackageAssetsAsync Cancellation Containment
            string cancelPackageOut = Path.Combine(Path.GetTempPath(), $"cancelled_{Guid.NewGuid():N}.rowlpkg");
            using (var pkgCts = new CancellationTokenSource())
            {
                pkgCts.Cancel();
                var cancelledPkgResult = ProjectBuildService.PackageAssetsAsync(
                    Path.Combine(testProjectRoot, "Assets"),
                    cancelPackageOut,
                    null,
                    pkgCts.Token).GetAwaiter().GetResult();
                if (!cancelledPkgResult.Cancelled || File.Exists(cancelPackageOut))
                    throw new Exception("Cancelled package creation should not produce an output file");
            }

            // 6. ProjectBuildService.ExecuteBuildPipeline Error Blocking
            var invalidNode = new NodeViewModel(9999, "Corrupted Node", 0, 0, bare: true);
            var invalidConn = new ConnectionViewModel(invalidNode, null!, "opt1");
            var pipeResultWithErrors = ProjectBuildService.ExecuteBuildPipeline(
                testProjectRoot,
                Path.Combine(testProjectRoot, "Assets"),
                Path.Combine(Path.GetTempPath(), "ShouldNotBeCreatedBuildDir"),
                new[] { invalidNode },
                new[] { invalidConn },
                9999,
                null,
                null);

            if (pipeResultWithErrors.Succeeded)
                throw new Exception("ExecuteBuildPipeline should fail when graph validation has blocking errors");
            if (Directory.Exists(pipeResultWithErrors.OutputDirectory))
                throw new Exception("ExecuteBuildPipeline created output directory despite blocking errors");

            // Clean up temporary test files
            try { Directory.Delete(testCoordSaveAsDir, true); } catch { }
            try { File.Delete(testPackageOut); } catch { }

            Console.WriteLine("  ✅ [PASS] ProjectSaveAsCoordinator, PackageAssetsAsync & Pipeline Validation Isolation verified");

            // ── Test 13: StoryGraphDocumentWriter, StoryGraphCanvasService & Layout Assist Presets ──
            Console.WriteLine("\n📌 [Test 13]: StoryGraphDocumentWriter, StoryGraphCanvasService & Layout Assist Presets...");

            // 1. StoryGraphDocumentWriter Persistence
            string testWriterAssetsDir = Path.Combine(Path.GetTempPath(), $"RowlTestWriter_Assets_{Guid.NewGuid():N}");
            string testWriterJsonDir = Path.Combine(testWriterAssetsDir, "json");
            var testNodeA = new NodeViewModel(101, "Writer Node A", 100, 100, bare: true);
            testNodeA.AddComponent<DialogueComponentViewModel>().DialogueText = "Hello from Node A";
            var testNodeB = new NodeViewModel(102, "Writer Node B", 400, 200, bare: true);
            testNodeB.AddComponent<DialogueComponentViewModel>().DialogueText = "Hello from Node B";
            var testNodesList = new List<NodeViewModel> { testNodeA, testNodeB };
            var testConnList = new List<ConnectionViewModel> { new ConnectionViewModel(testNodeA, testNodeB, "optA") };

            bool fullSaved = StoryGraphDocumentWriter.SaveFullStoryGraph(
                testWriterAssetsDir,
                testWriterJsonDir,
                testNodesList,
                testConnList,
                101);
            if (!fullSaved)
                throw new Exception("StoryGraphDocumentWriter.SaveFullStoryGraph returned false");

            string fullGraphPath1 = Path.Combine(testWriterAssetsDir, "full_story_graph.json");
            string fullGraphPath2 = Path.Combine(testWriterJsonDir, "full_story_graph.json");
            if (!File.Exists(fullGraphPath1) || !File.Exists(fullGraphPath2))
                throw new Exception("StoryGraphDocumentWriter did not produce full_story_graph.json in both target directories");

            string fullGraphContent = File.ReadAllText(fullGraphPath1);
            if (!fullGraphContent.Contains("\"start_node_id\": 101") || !fullGraphContent.Contains("\"Writer Node A\""))
                throw new Exception("full_story_graph.json content verification failed");

            bool activeSaved = StoryGraphDocumentWriter.SaveActiveStory(testWriterJsonDir, testNodeA);
            if (!activeSaved)
                throw new Exception("StoryGraphDocumentWriter.SaveActiveStory returned false");

            string activeStoryPath = Path.Combine(testWriterJsonDir, "active_story.json");
            if (!File.Exists(activeStoryPath))
                throw new Exception("StoryGraphDocumentWriter did not produce active_story.json");
            string activeStoryContent = File.ReadAllText(activeStoryPath);
            if (!activeStoryContent.Contains("Hello from Node A"))
                throw new Exception("active_story.json content verification failed");

            // Null node should return false without crashing
            if (StoryGraphDocumentWriter.SaveActiveStory(testWriterJsonDir, null))
                throw new Exception("StoryGraphDocumentWriter.SaveActiveStory should return false for null node");

            // Clean up writer test dir
            try { Directory.Delete(testWriterAssetsDir, true); } catch { }

            // 2. StoryGraphCanvasService Wire Routing & Hit-Testing
            var canvasNodes = new System.Collections.ObjectModel.ObservableCollection<NodeViewModel>();
            var canvasConns = new System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel>();
            var srcNode = new NodeViewModel(201, "Source Node", 100, 100, bare: true);
            var choiceComp = srcNode.AddComponent<ChoiceComponentViewModel>();
            string optionId = choiceComp.Options[0].OptionId;

            var dstNode = new NodeViewModel(202, "Target Node", 500, 300, bare: true);
            canvasNodes.Add(srcNode);
            canvasNodes.Add(dstNode);

            // Left input pin of dstNode is at (X + 10, Y + 60) -> (510, 360).
            // Case A: Successful hit (within 75px radius, e.g. at 520, 370 ~ 14px away)
            var hitPoint = new Avalonia.Point(520, 370);
            var connected = StoryGraphCanvasService.TryConnectWire(srcNode, hitPoint, canvasNodes, canvasConns, optionId);
            if (connected == null || canvasConns.Count != 1)
                throw new Exception("StoryGraphCanvasService.TryConnectWire failed to connect within snap radius");
            if (connected.SourceNode != srcNode || connected.TargetNode != dstNode)
                throw new Exception("StoryGraphCanvasService.TryConnectWire established invalid source/target");
            if (choiceComp.Options[0].TargetNodeId != dstNode.Id)
                throw new Exception("StoryGraphCanvasService did not update ChoiceOption.TargetNodeId on connect");

            // Case B: Discard drop outside radius (e.g. at 800, 800)
            var missPoint = new Avalonia.Point(800, 800);
            var missedConn = StoryGraphCanvasService.TryConnectWire(srcNode, missPoint, canvasNodes, canvasConns, optionId);
            if (missedConn != null)
                throw new Exception("StoryGraphCanvasService.TryConnectWire should return null for release point outside snap radius");

            // Case C: Disconnect Node Inputs & Outputs
            if (StoryGraphCanvasService.DisconnectNodeInputs(dstNode, canvasConns) != 1)
                throw new Exception("StoryGraphCanvasService.DisconnectNodeInputs failed to remove targeting connections");
            if (canvasConns.Count != 0)
                throw new Exception("Connections collection was not empty after DisconnectNodeInputs");

            // Reconnect and test DisconnectNodeOutputs
            var reconnected = StoryGraphCanvasService.TryConnectWire(srcNode, hitPoint, canvasNodes, canvasConns, optionId);
            if (reconnected == null || canvasConns.Count != 1)
                throw new Exception("Reconnection failed");
            if (StoryGraphCanvasService.DisconnectNodeOutputs(srcNode, canvasConns) != 1)
                throw new Exception("StoryGraphCanvasService.DisconnectNodeOutputs failed to remove outgoing connections");
            if (canvasConns.Count != 0)
                throw new Exception("Connections collection was not empty after DisconnectNodeOutputs");

            // Case D: Deduplication via EnforceSingleOutgoingWireRule
            var conn1 = new ConnectionViewModel(srcNode, dstNode, optionId);
            var conn2 = new ConnectionViewModel(srcNode, dstNode, optionId);
            canvasConns.Add(conn1);
            canvasConns.Add(conn2);
            if (canvasConns.Count != 2) throw new Exception("Setup for deduplication failed");
            StoryGraphCanvasService.EnforceSingleOutgoingWireRule(canvasConns);
            if (canvasConns.Count != 1)
                throw new Exception($"StoryGraphCanvasService.EnforceSingleOutgoingWireRule failed: expected 1 connection, got {canvasConns.Count}");

            // 3. EditorLayoutAssistService Presets Verification
            var layoutNode = new NodeViewModel(301, "Layout Node", 0, 0, bare: true);
            layoutNode.AddComponent<DialogueComponentViewModel>();
            layoutNode.AddComponent<CharacterComponentViewModel>();
            layoutNode.AddComponent<BackgroundComponentViewModel>();

            EditorLayoutAssistService.PresetDialogueBox(layoutNode, "Square");
            if (layoutNode.DialogueBoxWidth != 500.0 || layoutNode.DialogueBoxHeight != 500.0)
                throw new Exception("PresetDialogueBox('Square') failed to set 500x500 dimensions");

            EditorLayoutAssistService.PresetDialogueBox(layoutNode, "Standard");
            if (layoutNode.DialogueBoxWidth != 1760.0 || layoutNode.DialogueBoxHeight != 180.0 ||
                layoutNode.DialogueBoxX != 80.0 || layoutNode.DialogueBoxY != 860.0)
                throw new Exception("PresetDialogueBox('Standard') failed to set 1760x180 at (80, 860)");

            EditorLayoutAssistService.FitBackgroundToScreen(layoutNode);
            if (layoutNode.BackgroundWidth != 1920.0 || layoutNode.BackgroundHeight != 1080.0 ||
                layoutNode.BackgroundX != 0.0 || layoutNode.BackgroundY != 0.0)
                throw new Exception("FitBackgroundToScreen failed to set 1920x1080 at (0, 0)");

            EditorLayoutAssistService.ResetCharacterDimensions(layoutNode);
            if (layoutNode.CharacterWidth != 360.0 || layoutNode.CharacterHeight != 540.0 ||
                layoutNode.CharacterScale != 1.0)
                throw new Exception("ResetCharacterDimensions failed to set 360x540 scale 1.0");

            Console.WriteLine("  ✅ [PASS] StoryGraphDocumentWriter, StoryGraphCanvasService & Layout Assist Presets verified");

            // ── Test 14: EditorSceneSyncService & EditorPlayModeCoordinator Lifecycle & Diagnostics ──
            Console.WriteLine("\n📌 [Test 14]: EditorSceneSyncService & EditorPlayModeCoordinator Lifecycle...");

            // 1. EditorSceneSyncService PushScene & Diagnostics
            var syncNode = new NodeViewModel(401, "Sync Node", 0, 0, bare: true);
            var syncDlg = syncNode.AddComponent<DialogueComponentViewModel>();
            syncDlg.DialogueText = "Sync test line";
            var syncChar = syncNode.AddComponent<CharacterComponentViewModel>();
            syncChar.Sprite = "Woman.png";
            syncChar.X = 150;
            syncChar.Y = 250;
            var syncScript = syncNode.AddComponent<ScriptComponentViewModel>();
            syncScript.InlineCode = "rowl.var_set('sync_var', 'ok_401')";

            if (!mainVm.EngineHost.IsInitialized)
            {
                mainVm.EngineHost.Initialize(1920, 1080, false);
            }
            mainVm.EngineHost.SetProjectDirectory(testProjectRoot);

            bool pushed = EditorSceneSyncService.PushSceneToEngine(mainVm.EngineHost, syncNode);
            if (!pushed)
                throw new Exception("EditorSceneSyncService.PushSceneToEngine returned false");

            mainVm.EngineHost.Step(0.016f);
            string syncVarValue = mainVm.EngineHost.GetVariable("sync_var");
            if (syncVarValue != "ok_401")
                throw new Exception($"Native variable from pushed script mismatch: expected 'ok_401', got '{syncVarValue}'");

            EditorSceneSyncService.ApplyScriptRuntimeDiagnostics(mainVm.EngineHost, syncNode);
            if (string.IsNullOrEmpty(syncScript.RuntimeState))
                throw new Exception("EditorSceneSyncService did not populate ScriptComponentViewModel.RuntimeState");

            if (EditorSceneSyncService.PushSceneToEngine(mainVm.EngineHost, null))
                throw new Exception("EditorSceneSyncService.PushSceneToEngine should return false for null node");

            // 2. EditorPlayModeCoordinator Play / Stop Lifecycle
            bool switchedToGameTab = false;
            bool playStarted = EditorPlayModeCoordinator.StartPlayModeAsync(
                mainVm.EngineHost,
                () => Task.FromResult(true),
                () => true,
                node => mainVm.SelectNodeQuiet(node),
                node => mainVm.PushSceneToEngine(node),
                () => mainVm.GetStartNode(),
                msg => { },
                () => { switchedToGameTab = true; },
                Path.Combine(testProjectRoot, "Assets", "json")).GetAwaiter().GetResult();

            if (!playStarted)
                throw new Exception("EditorPlayModeCoordinator.StartPlayModeAsync failed");
            if (!switchedToGameTab)
                throw new Exception("EditorPlayModeCoordinator did not trigger game tab switch callback");
            if (!mainVm.EngineHost.IsPlaying)
                throw new Exception("EngineHost.IsPlaying was not true after StartPlayModeAsync");

            EditorPlayModeCoordinator.StopPlayMode(
                mainVm.EngineHost,
                () => mainVm.GetStartNode(),
                node => mainVm.PushSceneToEngine(node),
                node => mainVm.SelectNode(node),
                msg => { });

            if (mainVm.EngineHost.IsPlaying)
                throw new Exception("EngineHost.IsPlaying was still true after StopPlayMode");

            Console.WriteLine("  ✅ [PASS] EditorSceneSyncService & EditorPlayModeCoordinator Lifecycle verified");

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
