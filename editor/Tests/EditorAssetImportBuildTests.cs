using System;
using System.IO;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorAssetImportBuildTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 16]: EditorAssetImportService & EditorBuildCoordinator Isolation...");

        // 1. EditorAssetImportService Extension Subdirectory Resolution
        Console.WriteLine("    [Step 16.1]: Extension Subdirectory Resolution...");
        if (EditorAssetImportService.DetermineSubdirectory("test.png") != "images" ||
            EditorAssetImportService.DetermineSubdirectory("sprite.webp") != "images" ||
            EditorAssetImportService.DetermineSubdirectory("story.json") != "json" ||
            EditorAssetImportService.DetermineSubdirectory("script.lua") != "json" ||
            EditorAssetImportService.DetermineSubdirectory("bgm.ogg") != "audio" ||
            EditorAssetImportService.DetermineSubdirectory("font.ttf") != "fonts" ||
            EditorAssetImportService.DetermineSubdirectory("archive.rowlpkg") != "packages" ||
            EditorAssetImportService.DetermineSubdirectory("unknown.xyz") != "")
        {
            throw new Exception("EditorAssetImportService.DetermineSubdirectory returned incorrect subfolder mappings");
        }

        // 2. EditorAssetImportService Batch File Import & In-place Guard
        Console.WriteLine("    [Step 16.2]: Batch File Import...");
        string tempImportDir = Path.Combine(Path.GetTempPath(), $"RowlImportTest_{Guid.NewGuid():N}");
        Directory.CreateDirectory(tempImportDir);
        try
        {
            string srcImg = Path.Combine(tempImportDir, "sample_art.png");
            string srcAudio = Path.Combine(tempImportDir, "sample_sfx.wav");
            string srcLua = Path.Combine(tempImportDir, "sample_code.lua");
            File.WriteAllBytes(srcImg, new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            File.WriteAllBytes(srcAudio, new byte[] { 0x52, 0x49, 0x46, 0x46 });
            File.WriteAllText(srcLua, "-- sample lua script");

            var importedList = EditorAssetImportService.ImportAssetFiles(
                new[] { srcImg, srcAudio, srcLua, Path.Combine(tempImportDir, "non_existent.png") },
                Path.Combine(testProjectRoot, "Assets"),
                msg => { });

            if (importedList.Count != 3)
                throw new Exception($"Expected 3 imported assets, got {importedList.Count}");

            string destImgPath = Path.Combine(testProjectRoot, "Assets", "images", "sample_art.png");
            string destAudioPath = Path.Combine(testProjectRoot, "Assets", "audio", "sample_sfx.wav");
            string destLuaPath = Path.Combine(testProjectRoot, "Assets", "json", "sample_code.lua");

            if (!File.Exists(destImgPath) || !File.Exists(destAudioPath) || !File.Exists(destLuaPath))
                throw new Exception("Imported asset files were not placed in their expected subdirectories");

            // Single image import helper
            string singleImgName = EditorAssetImportService.ImportImageFile(srcImg, Path.Combine(testProjectRoot, "Assets"));
            if (singleImgName != "sample_art.png")
                throw new Exception($"EditorAssetImportService.ImportImageFile returned unexpected filename: {singleImgName}");
        }
        finally
        {
            try { Directory.Delete(tempImportDir, true); } catch { }
        }

        // 3. EditorBuildCoordinator Validation Gating & Standalone Build
        Console.WriteLine("    [Step 16.3]: Validation Gating...");
        bool persistInvoked = false;
        var buildGateNode = new NodeViewModel(9901, "Invalid Route Node", 0, 0, bare: true);
        var buildGateConn = new ConnectionViewModel(buildGateNode, null!, "opt1");

        var blockedResult = EditorBuildCoordinator.BuildStandaloneGameAsync(
            testProjectRoot,
            Path.Combine(testProjectRoot, "Assets"),
            Path.Combine(testProjectRoot, "BuildOut"),
            new[] { buildGateNode },
            new[] { buildGateConn },
            9901,
            () => { persistInvoked = true; },
            issues => { },
            msg => { }).GetAwaiter().GetResult();

        if (blockedResult.Succeeded || !persistInvoked)
            throw new Exception("EditorBuildCoordinator should have blocked build with missing connection target");

        bool pipePersistInvoked = false;
        var pipeResult = EditorBuildCoordinator.ExecuteBuildPipeline(
            testProjectRoot,
            Path.Combine(testProjectRoot, "Assets"),
            Path.Combine(Path.GetTempPath(), "ShouldNotBeCreatedCoordDir"),
            new[] { buildGateNode },
            new[] { buildGateConn },
            9901,
            () => { pipePersistInvoked = true; },
            issues => { },
            msg => { });

        if (pipeResult.Succeeded || !pipePersistInvoked)
            throw new Exception("EditorBuildCoordinator.ExecuteBuildPipeline should fail when validation has blocking errors");

        // 4. EditorBuildCoordinator PackageAssetsAsync
        Console.WriteLine("    [Step 16.4]: PackageAssetsAsync...");
        string testPackageDir = Path.Combine(Path.GetTempPath(), $"RowlTestPkg_{Guid.NewGuid():N}");
        try
        {
            var coordPkgResult = EditorBuildCoordinator.PackageAssetsAsync(
                Path.Combine(testProjectRoot, "Assets"),
                testPackageDir,
                msg => { }).GetAwaiter().GetResult();

            if (!coordPkgResult.Succeeded || !File.Exists(coordPkgResult.PackagePath) || new FileInfo(coordPkgResult.PackagePath).Length == 0)
                throw new Exception("EditorBuildCoordinator.PackageAssetsAsync failed to produce valid .rowlpkg archive");
        }
        finally
        {
            try { Directory.Delete(testPackageDir, true); } catch { }
        }

        Console.WriteLine("  ✅ [PASS] EditorAssetImportService & EditorBuildCoordinator Isolation verified");
    }
}
