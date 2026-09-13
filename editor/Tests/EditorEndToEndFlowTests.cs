using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorEndToEndFlowTests
{
    public static void Run(string mainProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 29]: End-to-end editor flow (Hub create → nodes → inspector → import → preview → save → build → package)...");

        string previousProjectRoot = MainWindowViewModel.ProjectRoot;
        string e2eParent = Path.Combine(Path.GetTempPath(), $"RowlE2E_{Guid.NewGuid():N}");
        string? e2eRoot = null;
        string? saveAsDir = null;
        string? buildBaseDir = null;
        string? packageOutDir = null;
        string? importSrcDir = null;
        MainWindowViewModel? flowVm = null;
        try
        {
            // Step 29.1: Project Hub creation path.
            var created = ProjectFactory.CreateNewProject("E2EFlow", e2eParent);
            if (!created.Success || created.Info == null)
                throw new Exception($"ProjectFactory.CreateNewProject failed: {created.Error}");
            e2eRoot = created.Info.Path;
            if (!File.Exists(Path.Combine(e2eRoot, "project.rowlproj")))
                throw new Exception("Project creation did not publish project.rowlproj");
            if (!File.Exists(Path.Combine(e2eRoot, "Assets", "json", "full_story_graph.json")))
                throw new Exception("Project creation did not publish the starter story graph");

            // Step 29.2: Project open path (fresh ViewModel on the new root).
            flowVm = new MainWindowViewModel(e2eRoot, connectEngine: false);
            if (!flowVm.LoadFullStoryGraphFile())
                throw new Exception("Fresh project graph did not load on open");
            var startNode = flowVm.Nodes.FirstOrDefault(n => n.Id == 101);
            if (startNode == null)
                throw new Exception("Starter node #101 missing after open");

            // Step 29.3: Node creation and connection through the ViewModel.
            flowVm.AddNode();
            var secondNode = flowVm.SelectedNode;
            if (secondNode == null || secondNode.Id != 102)
                throw new Exception($"AddNode did not create deterministic node #102 (got #{secondNode?.Id})");
            flowVm.Connections.Add(new ConnectionViewModel(startNode, secondNode, "opt_e2e"));
            if (flowVm.Connections.Count != 1)
                throw new Exception("Connection was not registered on the graph");

            // Step 29.4: Inspector component data entry through the node's
            // default components (facade properties, as the Inspector does).
            startNode.CharacterSprite = "images/e2e_sprite.png";
            secondNode.Speaker = "E2E Warden";
            secondNode.DialogueText = "End-to-end verification line.";
            secondNode.BackgroundTexture = "images/e2e_bg.png";
            secondNode.CharacterSprite = "images/e2e_sprite.png";
            secondNode.CharacterX = 760;
            secondNode.CharacterY = 140;
            var audio = secondNode.AddComponent<AudioComponentViewModel>();
            audio.DspFilter = "Normal";
            audio.BgmTrack = "audio/e2e_tone.wav";

            // Step 29.5: Relative asset import into the project.
            importSrcDir = Path.Combine(Path.GetTempPath(), $"RowlE2EImport_{Guid.NewGuid():N}");
            Directory.CreateDirectory(importSrcDir);
            string srcBg = Path.Combine(importSrcDir, "e2e_bg.png");
            string srcSprite = Path.Combine(importSrcDir, "e2e_sprite.png");
            string srcTone = Path.Combine(importSrcDir, "e2e_tone.wav");
            File.WriteAllBytes(srcBg, new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            File.WriteAllBytes(srcSprite, new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            File.WriteAllBytes(srcTone, new byte[] { 0x52, 0x49, 0x46, 0x46 });
            var imported = EditorAssetImportService.ImportAssetFiles(
                new[] { srcBg, srcSprite, srcTone },
                Path.Combine(e2eRoot, "Assets"),
                msg => { });
            if (imported.Count != 3 ||
                !File.Exists(Path.Combine(e2eRoot, "Assets", "images", "e2e_bg.png")) ||
                !File.Exists(Path.Combine(e2eRoot, "Assets", "images", "e2e_sprite.png")) ||
                !File.Exists(Path.Combine(e2eRoot, "Assets", "audio", "e2e_tone.wav")))
                throw new Exception("Asset import did not place all files in their subdirectories");

            // Step 29.6: Live preview and state synchronization.
            if (!flowVm.EngineHost.IsInitialized)
                flowVm.EngineHost.Initialize(1920, 1080, false);
            flowVm.EngineHost.SetProjectDirectory(e2eRoot);
            if (!flowVm.PushSceneToEngine(secondNode))
                throw new Exception("Live preview scene push returned false");
            flowVm.SelectNodeQuiet(secondNode);
            flowVm.ScheduleEnginePreviewUpdate(secondNode);
            if (flowVm.DeliverScheduledEnginePreview())
                throw new Exception("Unchanged preview should coalesce instead of redrawing");
            secondNode.DialogueText = "End-to-end verification line, take two.";
            flowVm.ScheduleEnginePreviewUpdate(secondNode);
            if (!flowVm.DeliverScheduledEnginePreview())
                throw new Exception("Edited preview did not deliver the selected node");

            // Step 29.7: Save and Save As.
            flowVm.SaveProject();
            string savedGraph = Path.Combine(e2eRoot, "Assets", "json", "full_story_graph.json");
            if (!File.Exists(savedGraph))
                throw new Exception("Save did not publish full_story_graph.json");
            string savedText = File.ReadAllText(savedGraph);
            if (!savedText.Contains("102") || !savedText.Contains("End-to-end verification line"))
                throw new Exception("Saved graph does not contain the edited node and dialogue");
            saveAsDir = Path.Combine(Path.GetTempPath(), $"RowlE2E_SaveAs_{Guid.NewGuid():N}");
            flowVm.SaveProjectToDirectory(saveAsDir);
            if (!File.Exists(Path.Combine(saveAsDir, "project.rowlproj")))
                throw new Exception("Save As did not publish project.rowlproj");

            // Step 29.8: Standalone build with validation gating.
            buildBaseDir = Path.Combine(Path.GetTempPath(), $"RowlE2E_Build_{Guid.NewGuid():N}");
            string reportedIssues = "";
            var buildResult = EditorBuildCoordinator.BuildStandaloneGameAsync(
                e2eRoot,
                Path.Combine(e2eRoot, "Assets"),
                buildBaseDir,
                flowVm.Nodes,
                flowVm.Connections,
                flowVm.GetStartNode()?.Id,
                () => flowVm.SaveProject(),
                issues => reportedIssues = string.Join(" | ", issues.Select(i => (i.IsError ? "ERR " : "WARN ") + i.Message)),
                msg => { }).GetAwaiter().GetResult();
            if (!buildResult.Succeeded || !Directory.Exists(buildResult.OutputDirectory))
                throw new Exception($"Standalone build failed: {buildResult.Message} [{reportedIssues}]");
            string buildNotices = Path.Combine(buildResult.OutputDirectory, "THIRD_PARTY_NOTICES.md");
            if (!File.Exists(buildNotices) || new FileInfo(buildNotices).Length == 0)
                throw new Exception("Standalone build did not publish its third-party license inventory");

            // Step 29.9: .rowlpkg packaging of the edited project.
            packageOutDir = Path.Combine(Path.GetTempPath(), $"RowlE2E_Pkg_{Guid.NewGuid():N}");
            var packageResult = EditorBuildCoordinator.PackageAssetsAsync(
                Path.Combine(e2eRoot, "Assets"),
                packageOutDir,
                msg => { }).GetAwaiter().GetResult();
            if (!packageResult.Succeeded || string.IsNullOrEmpty(packageResult.PackagePath) ||
                !File.Exists(packageResult.PackagePath) ||
                new FileInfo(packageResult.PackagePath).Length == 0)
                throw new Exception("Package step did not produce a valid .rowlpkg archive");

            Console.WriteLine("  ✅ [PASS] End-to-end editor flow verified (create → open → nodes → inspector → import → preview → save → build → package)");
        }
        finally
        {
            flowVm?.Dispose();
            MainWindowViewModel.ProjectRoot = previousProjectRoot;
            foreach (string? dir in new[] { e2eParent, saveAsDir, buildBaseDir, packageOutDir, importSrcDir })
            {
                try { if (!string.IsNullOrEmpty(dir) && Directory.Exists(dir)) Directory.Delete(dir, true); } catch { }
            }
        }
    }
}
