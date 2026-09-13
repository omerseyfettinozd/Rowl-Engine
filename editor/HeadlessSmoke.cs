using System;
using System.IO;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

/// <summary>
/// Display-free smoke behind `dotnet run -- --headless-test`.
/// Boots the Avalonia headless platform, loads the project graph into a
/// detached view model (no engine connection), runs the synchronous MS-2
/// save path and asserts the canonical-path contract. Returns a process
/// exit code: 0 on success, 1 on any failure.
/// </summary>
internal static class HeadlessSmoke
{
    public static int Run(string? benchmarkPath = null)
    {
        string? tempRoot = null;
        try
        {
            // Display-free SDL drivers via libc (see NativeEnvironment); the
            // smoke itself never inits the engine, this keeps the entry point
            // ready for flows that do.
            Services.NativeEnvironment.EnsureDisplayFreeDrivers();
            Program.BuildAvaloniaAppHeadless().SetupWithoutStarting();
            Console.WriteLine("[headless-test] Avalonia headless platform initialized.");

            string sourceAssets = MainWindowViewModel.AssetsPath;
            tempRoot = Path.Combine(Path.GetTempPath(), $"RowlHeadlessSmoke_{Guid.NewGuid():N}");
            CopyDirectory(sourceAssets, Path.Combine(tempRoot, "Assets"));

            using var vm = new MainWindowViewModel(tempRoot, connectEngine: false);
            Console.WriteLine($"[headless-test] Project loaded: {vm.Nodes.Count} nodes.");

            if (!vm.SaveProjectNow())
                throw new Exception("SaveProjectNow reported failure.");

            string canonical = Path.Combine(tempRoot, "Assets", "json", "full_story_graph.json");
            string legacy = Path.Combine(tempRoot, "Assets", "full_story_graph.json");
            if (!File.Exists(canonical))
                throw new Exception($"Canonical graph missing: {canonical}");
            if (File.Exists(legacy))
                throw new Exception($"Legacy graph copy must not be written: {legacy}");

            if (!string.IsNullOrWhiteSpace(benchmarkPath))
                Console.WriteLine($"[headless-test] Benchmark output requested at '{benchmarkPath}' (skipped in CLI smoke).");

            Console.WriteLine($"[headless-test] PASS: save round-trip ok ({vm.Nodes.Count} nodes, canonical path only).");
            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"[headless-test] FAIL: {ex.Message}");
            return 1;
        }
        finally
        {
            if (tempRoot != null)
            {
                try { Directory.Delete(tempRoot, true); }
                catch { }
            }
        }
    }

    private static void CopyDirectory(string sourceDir, string targetDir)
    {
        Directory.CreateDirectory(targetDir);
        foreach (string file in Directory.GetFiles(sourceDir))
            File.Copy(file, Path.Combine(targetDir, Path.GetFileName(file)), overwrite: true);
        foreach (string subDirectory in Directory.GetDirectories(sourceDir))
            CopyDirectory(subDirectory, Path.Combine(targetDir, Path.GetFileName(subDirectory)));
    }
}
