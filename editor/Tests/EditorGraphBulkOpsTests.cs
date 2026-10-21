using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C03: çoklu düğüm ekleme/silme/geri alma yolları öğe başına dizin kurmamalı;
/// rozet yenileme düğüm eklemede tüm grafı taramamalı. Süre değil, sayaç ölçülür.
/// </summary>
public sealed class EditorGraphBulkOpsTests
{
    private const int N = 300;

    [Fact]
    public void Subgraphs_AddingNodes_RefreshesBadgesIncrementally()
    {
        var nodes = new System.Collections.ObjectModel.ObservableCollection<NodeViewModel>();
        var service = new SubgraphNavigationService();
        service.Attach(nodes);
        service.LoadDefinitions(new[]
        {
            new SubgraphDefinition("sg", "SG", 1, new ulong[] { 3 }, new ulong[] { 1, 2, 3 })
        });
        service.ResetBadgeVisitCount();

        for (int i = 1; i <= N; i++)
            nodes.Add(new NodeViewModel((ulong)i, "N" + i, 0, 0));

        // Doğrusal: her ekleme yalnız yeni düğümü ziyaret eder (kuadratik olsaydı ~N²/2).
        Assert.True(service.BadgeVisitCount <= N * 2, $"Badge visits: {service.BadgeVisitCount} for {N} adds.");
        Assert.Equal("SG", nodes[0].SubgraphBadge);
        Assert.True(nodes[0].IsSubgraphBoundary);
        Assert.False(nodes[1].IsSubgraphBoundary);
        Assert.True(nodes[2].IsSubgraphBoundary);
        Assert.Equal(string.Empty, nodes[10].SubgraphBadge);
    }

    [Fact]
    public void DeleteSelectedNodes_UndoRedo_RebuildCanvasIndexConstantTimes()
    {
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string root = Path.Combine(Path.GetTempPath(), "RowlBulkOps_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Assets", "json"));
        try
        {
            using var vm = new MainWindowViewModel(root, connectEngine: false);
            vm.Nodes.Clear();
            vm.Connections.Clear();
            vm.NodeGraphViewModel.BeginBulkUpdate();
            try
            {
                for (int i = 1; i <= N; i++)
                    vm.Nodes.Add(new NodeViewModel((ulong)i, "N" + i, (i % 20) * 400, (i / 20) * 300));
                for (int i = 0; i < N - 1; i++)
                    vm.Connections.Add(new ConnectionViewModel(vm.Nodes[i], vm.Nodes[i + 1]));
            }
            finally { vm.NodeGraphViewModel.EndBulkUpdate(); }

            int rebuilds = 0;
            vm.NodeGraphViewModel.PropertyChanged += (_, e) =>
            {
                if (e.PropertyName == nameof(NodeGraphViewModel.WorldBounds)) rebuilds++;
            };

            vm.SelectedNodes.Clear();
            foreach (var n in vm.Nodes.ToList()) vm.SelectedNodes.Add(n);

            vm.DeleteSelectedNodes();
            int afterDelete = rebuilds;
            Assert.Empty(vm.Nodes);
            Assert.True(afterDelete <= 6, $"Delete rebuilt index {afterDelete} times.");

            rebuilds = 0;
            UndoRedoService.Instance.Undo();
            Assert.Equal(N, vm.Nodes.Count);
            Assert.Equal(N - 1, vm.Connections.Count);
            Assert.True(rebuilds <= 6, $"Undo rebuilt index {rebuilds} times for {N} nodes (expected O(1)).");
            Assert.NotEmpty(vm.NodeGraphViewModel.VisibleNodes);

            rebuilds = 0;
            UndoRedoService.Instance.Redo();
            Assert.Empty(vm.Nodes);
            Assert.True(rebuilds <= 6, $"Redo rebuilt index {rebuilds} times.");
        }
        finally
        {
            MainWindowViewModel.ProjectRoot = previousRoot;
            try { Directory.Delete(root, recursive: true); } catch { }
        }
    }
}
