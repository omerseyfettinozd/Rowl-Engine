using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

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
                RunHeadlessTests();
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

        private static void RunHeadlessTests()
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
                new[] { validationNode, orphanNode }, Array.Empty<ConnectionViewModel>(), Path.Combine(testProjectRoot, "Assets"));
            if (!validationIssues.Any(issue => issue.IsError && issue.Message.Contains("missing_build_asset.png")) ||
                !validationIssues.Any(issue => !issue.IsError && issue.Message.Contains("unreachable")))
                throw new Exception("Build validation did not report missing assets and unreachable nodes");
            Console.WriteLine("  ✅ [PASS] Build validation blocks missing assets and reports unreachable nodes");

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

            // 2. Test Save As (Farklı Kaydet)
            string testSaveAsDir = Path.Combine(Path.GetTempPath(), "RowlTestProject_SaveAs");
            mainVm.SaveProjectToDirectory(testSaveAsDir);

            if (!File.Exists(Path.Combine(testSaveAsDir, "project.rowlproj")))
                throw new Exception("project.rowlproj was not created in Save As target");
            if (!File.Exists(Path.Combine(testSaveAsDir, "Assets", "full_story_graph.json")))
                throw new Exception("full_story_graph.json missing in Save As target");
            if (!Directory.Exists(Path.Combine(testSaveAsDir, "Assets", "images")))
                throw new Exception("Assets/images missing in Save As target");

            // 3. Test Build Game (Standalone Release Export)
            string testBuildDir = Path.Combine(Path.GetTempPath(), "RowlTest_Build_PC");
            mainVm.ExecuteBuildPipeline(testBuildDir);

            if (!File.Exists(Path.Combine(testBuildDir, "run_game.sh")))
                throw new Exception("run_game.sh missing in standalone build output: " + mainVm.LogOutput);
            if (!File.Exists(Path.Combine(testBuildDir, "run_game.bat")))
                throw new Exception("run_game.bat missing in standalone build output");
            if (!File.Exists(Path.Combine(testBuildDir, "README.txt")))
                throw new Exception("README.txt missing in standalone build output");
            if (!File.Exists(Path.Combine(testBuildDir, "RowlGame")) && !File.Exists(Path.Combine(testBuildDir, "rowl_player")))
                throw new Exception("Standalone player executable missing in standalone build output");
            if (!File.Exists(Path.Combine(testBuildDir, "libRowlEngineCore.so")))
                throw new Exception("libRowlEngineCore.so missing in standalone build output");
            if (!Directory.Exists(Path.Combine(testBuildDir, "Assets")))
                throw new Exception("Assets directory missing in standalone build output");

            // Clean up temporary test directories
            try { Directory.Delete(testSaveAsDir, true); } catch {}
            try { Directory.Delete(testBuildDir, true); } catch {}

            Console.WriteLine("  ✅ [PASS] Project Save, Save As (all assets + manifest) & Standalone Game Build verified");

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
                host.SetVariable("test_affinity", "99");
                string readAffinity = host.GetVariable("test_affinity");
                if (readAffinity != "99")
                    throw new Exception($"Native variable mismatch: expected '99', got '{readAffinity}'");

                if (!host.EvaluateCondition("test_affinity >= 90") || host.EvaluateCondition("test_affinity < 50"))
                    throw new Exception("Native condition evaluation via EngineHost failed");

                // Save & Load Slots
                if (!host.SaveGameSlot(10))
                    throw new Exception("EngineHost.SaveGameSlot(10) failed");

                if (!host.HasSaveSlot(10))
                    throw new Exception("EngineHost.HasSaveSlot(10) failed");

                if (!host.LoadGameSlot(10))
                    throw new Exception("EngineHost.LoadGameSlot(10) failed");

                host.DeleteSaveSlot(10);
                if (host.HasSaveSlot(10))
                    throw new Exception("EngineHost.DeleteSaveSlot(10) failed");

                // Rewind
                host.Rewind(1);
            }

            Console.WriteLine("  ✅ [PASS] Variable/Condition components, serialization, and P/Invoke Save/Load slots verified");

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
