using System;
using System.IO;
using System.Text.Json;
using System.Threading;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorProjectPersistenceTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 7]: Project Save, Save As & Standalone Build Pipeline...");
        mainVm.SaveProject();

        // Project-owned settings and unknown manifest fields must survive Save As.
        File.WriteAllText(
            Path.Combine(testProjectRoot, "project.rowlproj"),
            "{ \"custom_release_field\": \"keep\" }");
        mainVm.Settings.ProjectSaveSlotCount = 17;
        mainVm.Settings.ProjectDefaultBgmTransition = "crossfade";
        mainVm.Settings.ProjectDefaultBgmTransitionDurationSeconds = 2.5f;

        string testSaveAsDir = Path.Combine(Path.GetTempPath(), "RowlTestProject_SaveAs");
        mainVm.SaveProjectToDirectory(testSaveAsDir);

        if (!File.Exists(Path.Combine(testSaveAsDir, "project.rowlproj")))
            throw new Exception("project.rowlproj was not created in Save As target");
        if (!File.Exists(Path.Combine(testSaveAsDir, "Assets", "full_story_graph.json")))
            throw new Exception("full_story_graph.json missing in Save As target");
        if (!Directory.Exists(Path.Combine(testSaveAsDir, "Assets", "images")))
            throw new Exception("Assets/images missing in Save As target");
        using (var copiedManifest = JsonDocument.Parse(
                   File.ReadAllText(Path.Combine(testSaveAsDir, "project.rowlproj"))))
        {
            if (copiedManifest.RootElement.GetProperty("custom_release_field").GetString() != "keep" ||
                copiedManifest.RootElement.GetProperty("startNodeId").GetUInt64() != mainVm.GetStartNode()!.Id ||
                copiedManifest.RootElement.GetProperty("save_slot_count").GetInt32() != 17)
            {
                throw new Exception("Save As did not preserve project metadata and runtime settings");
            }
        }

        string testBuildDir = Path.Combine(Path.GetTempPath(), "RowlTest_Build_PC");
        mainVm.ExecuteBuildPipeline(testBuildDir);

        if (!File.Exists(Path.Combine(testBuildDir, "run_game.sh")))
            throw new Exception("run_game.sh missing in standalone build output: " + mainVm.LogOutput);
        if (!File.Exists(Path.Combine(testBuildDir, "run_game.bat")))
            throw new Exception("run_game.bat missing in standalone build output");
        if (!File.Exists(Path.Combine(testBuildDir, "README.txt")))
            throw new Exception("README.txt missing in standalone build output");
        string expectedPlayer = OperatingSystem.IsWindows() ? "RowlGame.exe" : "RowlGame";
        if (!File.Exists(Path.Combine(testBuildDir, expectedPlayer)) &&
            !File.Exists(Path.Combine(
                testBuildDir, OperatingSystem.IsWindows() ? "rowl_player.exe" : "rowl_player")))
        {
            throw new Exception($"Standalone player executable missing ({expectedPlayer}) in standalone build output");
        }

        string expectedLibrary = OperatingSystem.IsWindows() ? "RowlEngineCore.dll"
            : OperatingSystem.IsMacOS() ? "libRowlEngineCore.dylib" : "libRowlEngineCore.so";
        if (!File.Exists(Path.Combine(testBuildDir, expectedLibrary)))
            throw new Exception($"{expectedLibrary} missing in standalone build output");
        if (!File.Exists(Path.Combine(testBuildDir, "Assets", "packages", "game.rowlpkg")))
            throw new Exception("game.rowlpkg missing in standalone build output");
        if (!Directory.Exists(Path.Combine(testBuildDir, "mods")) ||
            !File.Exists(Path.Combine(testBuildDir, "mods", "README.md")))
        {
            throw new Exception("mods override directory missing in standalone build output");
        }

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

        var repeatBuild = ProjectBuildService.BuildStandalone(
            testProjectRoot, Path.Combine(testProjectRoot, "Assets"), testBuildDir);
        if (repeatBuild.Succeeded || !File.Exists(Path.Combine(testBuildDir, "README.txt")))
            throw new Exception("Build must preserve a published package when its output directory already exists");

        try { Directory.Delete(testSaveAsDir, true); } catch { }
        try { Directory.Delete(testBuildDir, true); } catch { }

        Console.WriteLine("  ✅ [PASS] Project Save, Save As (all assets + manifest) & Standalone Game Build verified");

        Console.WriteLine("\n📌 [Test 7b]: Project runtime settings migration...");
        string manifestPath = Path.Combine(testProjectRoot, "runtime-settings.rowlproj");
        File.WriteAllText(manifestPath, "{ \"name\": \"Legacy\" }");
        var legacySettings = ProjectRuntimeSettingsService.Load(manifestPath);
        if (legacySettings.SaveSlotCount != 10 || legacySettings.DefaultBgmTransition != "instant" ||
            legacySettings.DefaultBgmTransitionDurationSeconds != 1)
        {
            throw new Exception("Legacy project settings did not receive safe defaults");
        }

        ProjectRuntimeSettingsService.Save(manifestPath, new ProjectRuntimeSettings
        {
            SaveSlotCount = 14,
            DefaultBgmTransition = "crossfade",
            DefaultBgmTransitionDurationSeconds = 2.5f
        });
        var savedSettings = ProjectRuntimeSettingsService.Load(manifestPath);
        if (savedSettings.SaveSlotCount != 14 || savedSettings.DefaultBgmTransition != "crossfade" ||
            savedSettings.DefaultBgmTransitionDurationSeconds != 2.5f)
        {
            throw new Exception("Project runtime settings did not round-trip");
        }

        Console.WriteLine("  ✅ [PASS] Project runtime settings migration and round-trip verified");

        string editorProfilePath = Path.Combine(testProjectRoot, "editor-settings.json");
        new EditorSettingsProfile
        {
            AutoSaveEnabled = false,
            AutoSaveIntervalSeconds = 1
        }.Save(editorProfilePath);
        var editorProfile = EditorSettingsProfile.Load(editorProfilePath);
        if (editorProfile.AutoSaveEnabled || editorProfile.AutoSaveIntervalSeconds != 15)
            throw new Exception("Editor settings profile did not persist and sanitize autosave behaviour");

        Console.WriteLine("  ✅ [PASS] Editor-local autosave settings persistence and bounds verified");
    }
}
