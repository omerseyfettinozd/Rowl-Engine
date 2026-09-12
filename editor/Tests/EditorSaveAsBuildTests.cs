using System;
using System.IO;
using System.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorSaveAsBuildTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
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
    }
}
