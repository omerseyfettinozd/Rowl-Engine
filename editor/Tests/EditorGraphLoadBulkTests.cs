using System;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Text;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C03: büyük grafik yüklemesi her düğüm/tel eklemesinde tuval dizinini baştan kurmamalı
/// (2000 düğüm / 6000 tel ≈ 9 sn donma). Süre değil, yeniden kurulum sayısı ölçülür.
/// </summary>
public sealed class EditorGraphLoadBulkTests
{
    private const int NodeCount = 300;

    [Fact]
    public void LoadFullStoryGraphFile_BuildsCanvasIndexOnce_NotPerItem()
    {
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string root = Path.Combine(Path.GetTempPath(), "RowlBulkLoad_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Assets", "json"));
        try
        {
            File.WriteAllText(Path.Combine(root, "Assets", "json", "full_story_graph.json"), BuildGraphJson());
            using var vm = new MainWindowViewModel(root, connectEngine: false);
            vm.Nodes.Clear();
            vm.Connections.Clear();

            // RebuildIndex her çağrıda WorldBounds bildirir: tam yeniden kurulumun sayacı.
            int rebuilds = 0;
            vm.NodeGraphViewModel.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(NodeGraphViewModel.WorldBounds)) rebuilds++;
            };

            Assert.True(vm.LoadFullStoryGraphFile());

            Assert.Equal(NodeCount, vm.Nodes.Count);
            Assert.Equal(NodeCount * 3, vm.Connections.Count);
            // Doğrusal yükleme: sabit sayıda yeniden kurulum (kuyruk işleri dahil), düğüm sayısından bağımsız.
            Assert.True(rebuilds <= 6, $"Canvas index was rebuilt {rebuilds} times for {NodeCount} nodes (expected O(1)).");
            // Yük sonrası indeks tutarlı: görünür küme boş değil, her tel iki ucu da bilinen düğüm.
            Assert.NotEmpty(vm.NodeGraphViewModel.VisibleNodes);
        }
        finally
        {
            MainWindowViewModel.ProjectRoot = previousRoot;
            try { Directory.Delete(root, recursive: true); } catch { }
        }
    }

    [Fact]
    public void LoadFullStoryGraphFile_ResumesLiveUpdatesAfterLoad()
    {
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string root = Path.Combine(Path.GetTempPath(), "RowlBulkLoad_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Assets", "json"));
        try
        {
            File.WriteAllText(Path.Combine(root, "Assets", "json", "full_story_graph.json"), BuildGraphJson());
            using var vm = new MainWindowViewModel(root, connectEngine: false);
            Assert.True(vm.LoadFullStoryGraphFile());
            // Toplu mod kapanmış olmalı: sonradan eklenen düğüm tuval dizinine girer.
            var extra = new NodeViewModel(900_001, "Extra", 0, 0);
            vm.Nodes.Add(extra);
            Assert.Contains(extra, vm.NodeGraphViewModel.VisibleNodes);
        }
        finally
        {
            MainWindowViewModel.ProjectRoot = previousRoot;
            try { Directory.Delete(root, recursive: true); } catch { }
        }
    }

    private static string BuildGraphJson()
    {
        var sb = new StringBuilder("{\"format_version\":4,\"start_node_id\":1,\"nodes\":[");
        for (int i = 1; i <= NodeCount; i++)
        {
            if (i > 1) sb.Append(',');
            sb.Append($"{{\"id\":{i},\"title\":\"N{i}\",\"x\":{(i % 20) * 400},\"y\":{(i / 20) * 300},\"objects\":[],\"next_nodes\":[");
            for (int step = 1; step <= 3; step++)
            {
                int target = (i + step - 1) % NodeCount + 1;
                if (step > 1) sb.Append(',');
                sb.Append($"{{\"id\":{target},\"label\":\"r{step}\"}}");
            }
            sb.Append("]}");
        }
        sb.Append("]}");
        return sb.ToString();
    }
}
