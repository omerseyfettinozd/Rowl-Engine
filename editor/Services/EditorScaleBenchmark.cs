using System;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>Measures the real editor load/search/select/save paths against the canonical scale fixture.</summary>
internal static class EditorScaleBenchmark
{
    public const string FixtureId = "editor-productization-scale-v1";

    public static void Record(EditorInteractionBenchmark benchmark, string fixtureRoot)
    {
        string fixturePath = Path.Combine(fixtureRoot, "editor", "Tests", "Fixtures", "editor_scale_2000.json");
        if (!File.Exists(fixturePath))
            throw new FileNotFoundException("Canonical editor scale fixture is missing.", fixturePath);

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        long memoryBefore = GC.GetTotalMemory(forceFullCollection: true);

        var stopwatch = Stopwatch.StartNew();
        using var document = JsonDocument.Parse(File.ReadAllBytes(fixturePath));
        StoryGraphLoadResult loaded = StoryGraphLoaderService.Load(document);
        stopwatch.Stop();
        if (!loaded.Success || loaded.Nodes.Count != 2_000 || loaded.Connections.Count != 6_000)
            throw new InvalidOperationException("Canonical editor scale fixture did not hydrate as 2,000 nodes and 6,000 connections.");
        benchmark.Record("scale_graph_load_ms", stopwatch.Elapsed.TotalMilliseconds);

        string previousProjectRoot = MainWindowViewModel.ProjectRoot;
        string isolatedProjectRoot = Path.Combine(
            Path.GetTempPath(), "RowlEditorScaleProject_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(isolatedProjectRoot, "Assets", "json"));
        try
        {
            RecordViewModelOperations(benchmark, loaded, isolatedProjectRoot, memoryBefore, stopwatch);
        }
        finally
        {
            MainWindowViewModel.ProjectRoot = previousProjectRoot;
            try { Directory.Delete(isolatedProjectRoot, recursive: true); } catch { }
        }
    }

    private static void RecordViewModelOperations(
        EditorInteractionBenchmark benchmark,
        StoryGraphLoadResult loaded,
        string isolatedProjectRoot,
        long memoryBefore,
        Stopwatch stopwatch)
    {
        using var scaleVm = new MainWindowViewModel(isolatedProjectRoot, connectEngine: false);
        scaleVm.Nodes.Clear();
        scaleVm.Connections.Clear();
        stopwatch.Restart();
        foreach (NodeViewModel node in loaded.Nodes) scaleVm.Nodes.Add(node);
        foreach (ConnectionViewModel connection in loaded.Connections) scaleVm.Connections.Add(connection);
        stopwatch.Stop();
        benchmark.Record("scale_graph_viewmodel_population_ms", stopwatch.Elapsed.TotalMilliseconds);

        const int iterations = 50;
        stopwatch.Restart();
        for (int index = 0; index < iterations; index++)
        {
            NodeViewModel? match = EditorWorkspaceLayoutService.FindMatchingNode(scaleVm.Nodes, "Scale Node 2000");
            if (match?.Id != 2_000) throw new InvalidOperationException("Scale benchmark search returned the wrong node.");
        }
        stopwatch.Stop();
        benchmark.Record("scale_graph_worst_case_search_ms", stopwatch.Elapsed.TotalMilliseconds / iterations);

        stopwatch.Restart();
        for (int index = 0; index < iterations; index++)
            scaleVm.SelectNodeQuiet(index % 2 == 0 ? scaleVm.Nodes[0] : scaleVm.Nodes[^1]);
        stopwatch.Stop();
        benchmark.Record("scale_graph_selection_input_ms", stopwatch.Elapsed.TotalMilliseconds / iterations);

        stopwatch.Restart();
        string serialized = StoryGraphSerializer.SerializeFullStoryGraph(
            scaleVm.Nodes, scaleVm.Connections, startNodeId: 1);
        stopwatch.Stop();
        benchmark.Record("scale_graph_serialize_ms", stopwatch.Elapsed.TotalMilliseconds);

        string savePath = Path.Combine(isolatedProjectRoot, "Assets", "json", "full_story_graph.json");
        stopwatch.Restart();
        ProjectFileSystem.WriteAllTextAtomically(savePath, serialized);
        stopwatch.Stop();
        if (!File.Exists(savePath)) throw new IOException("Scale benchmark graph was not published atomically.");
        benchmark.Record("scale_graph_atomic_save_ms", stopwatch.Elapsed.TotalMilliseconds);

        long memoryAfter = GC.GetTotalMemory(forceFullCollection: true);
        benchmark.Record("scale_graph_managed_memory_bytes", Math.Max(0, memoryAfter - memoryBefore));
    }
}
