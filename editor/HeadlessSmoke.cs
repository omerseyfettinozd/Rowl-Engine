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

            RunMs5Tour(vm, tempRoot);

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

    // MS-5 gate for the `--headless-test` run mode: drag-cancel default,
    // keyboard-tour undo round-trip, and the missing-asset badge.
    private static void RunMs5Tour(MainWindowViewModel vm, string projectRoot)
    {
        var undo = Services.UndoRedoService.Instance;
        var a = new NodeViewModel(9201, "MS5 Smoke A", 100, 100, bare: true);
        var b = new NodeViewModel(9202, "MS5 Smoke B", 600, 100, bare: true);
        vm.Nodes.Add(a);
        vm.Nodes.Add(b);
        try
        {
            bool canUndoBefore = undo.CanUndo;
            string descBefore = undo.UndoDescription;

            vm.SelectNodeQuiet(a);
            double x0 = a.X, y0 = a.Y;
            vm.BeginNodeDragSnapshot();
            a.X += 99; a.Y += 88;
            vm.CancelNodeDrag();
            if (a.X != x0 || a.Y != y0)
                throw new Exception("MS-5: CancelNodeDrag did not restore start pos.");
            if (undo.CanUndo != canUndoBefore || undo.UndoDescription != descBefore)
                throw new Exception("MS-5: cancelled drag recorded undo.");

            vm.SelectNodeQuiet(a);
            vm.CopySelectedNodesCommand.Execute(null);
            if (vm.ClipboardNodeCount != 1)
                throw new Exception("MS-5: copy did not fill the clipboard.");
            int nodesBefore = vm.Nodes.Count;
            vm.PasteClipboardNodesCommand.Execute(null);
            if (vm.Nodes.Count != nodesBefore + 1)
                throw new Exception("MS-5: paste did not add one node.");
            vm.NudgeSelectedNodes(2, 2);
            undo.Undo(); // nudge
            undo.Undo(); // paste
            if (vm.Nodes.Count != nodesBefore)
                throw new Exception("MS-5: undo of paste/nudge did not restore node count.");
            if (undo.CanUndo != canUndoBefore || undo.UndoDescription != descBefore)
                throw new Exception("MS-5: keyboard tour left stray undo records.");

            string probe = Path.Combine(projectRoot, "Assets", "ms5_smoke.txt");
            File.WriteAllText(probe, "ms5");
            vm.AssetBrowserViewModel.RefreshAssets();
            File.Delete(probe);
            vm.AssetBrowserViewModel.RefreshAssets();
            if (vm.AssetBrowserViewModel.MissingAssetCount != 1)
                throw new Exception("MS-5: missing-asset badge was not raised.");
            File.WriteAllText(probe, "ms5");
            vm.AssetBrowserViewModel.RefreshAssets();
            if (vm.AssetBrowserViewModel.MissingAssetCount != 0)
                throw new Exception("MS-5: missing-asset badge did not clear.");
            File.Delete(probe);
            vm.AssetBrowserViewModel.RefreshAssets();

            Console.WriteLine("[headless-test] PASS: MS-5 tour ok (drag-cancel, keyboard undo, missing badge).");
        }
        finally
        {
            vm.Nodes.Remove(a);
            vm.Nodes.Remove(b);
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
