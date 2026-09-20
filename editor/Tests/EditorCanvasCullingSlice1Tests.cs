using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Avalonia;
using RowlEngine.Editor.Controls;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 4 Dilim 1 — viewport culling: spatial-index correctness, viewport
/// math, visible-set diffing, Fit/Zoom commands, minimap mapping and a
/// 2.000-node / 6.000-edge parity + budget gate. Headless; no GPU needed.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorCanvasCullingSlice1Tests
{
    private const double Margin = NodeGraphViewModel.CullMargin;

    private sealed class CulledShell : IDisposable
    {
        /// <summary>Static root seen before this shell was built.</summary>
        public string PreviousRoot { get; }

        public MainWindowViewModel Vm { get; }

        public string Root { get; }

        public CulledShell()
        {
            // MainWindowViewModel construction stamps the static project
            // root; xUnit runs classes in parallel, so every shell saves
            // and restores it to avoid stomping a concurrent suite.
            PreviousRoot = MainWindowViewModel.ProjectRoot;
            string parent = Path.Combine(Path.GetTempPath(), "RowlCull_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(parent);
            var created = ProjectFactory.CreateNewProject("CullTest", parent);
            if (!created.Success || created.Info == null)
                throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
            Root = created.Info.Path;
            Vm = new MainWindowViewModel(Root, connectEngine: false);
            Vm.Nodes.Clear();
            Vm.Connections.Clear();
            // The shell only needs its instance from here on: hand the
            // static root back immediately (compare-and-swap) so a
            // parallel suite never validates against this temp project.
            // NodeGraphViewModel and the tests below use instance state.
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root,
                        StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }

        public void Dispose()
        {
            try { Directory.Delete(Path.GetDirectoryName(Root)!, recursive: true); }
            catch (Exception) { }
            // Compare-and-swap: only restore when nobody else claimed the
            // static since (a parallel suite may own it now — never clobber).
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root,
                        StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }
    }

    private static NodeViewModel AddNode(
        MainWindowViewModel vm, ulong id, double x, double y) =>
        AddNode(vm, id, "Node " + id, x, y);

    private static NodeViewModel AddNode(
        MainWindowViewModel vm, ulong id, string title, double x, double y)
    {
        var node = new NodeViewModel(id, title, x, y);
        vm.Nodes.Add(node);
        return node;
    }

    private static HashSet<NodeViewModel> BruteForceVisible(
        IEnumerable<NodeViewModel> nodes, Rect view)
    {
        var expanded = new Rect(
            view.X - Margin, view.Y - Margin,
            view.Width + Margin * 2, view.Height + Margin * 2);
        var result = new HashSet<NodeViewModel>();
        foreach (var node in nodes)
        {
            var (x, y, w, h) = NodeGraphViewModel.NodeBounds(node);
            if (x <= expanded.Right && x + w >= expanded.X &&
                y <= expanded.Bottom && y + h >= expanded.Y)
                result.Add(node);
        }
        return result;
    }

    // ── Spatial index ────────────────────────────────────────────────

    [Fact]
    public void Index_QueryReturnsIntersectingAndTouchingOnly()
    {
        var index = new CanvasSpatialIndex();
        Assert.True(index.Add(1, 0, 0, 100, 100));
        Assert.True(index.Add(2, 100, 0, 100, 100)); // touches at x=100
        Assert.True(index.Add(3, 1000, 1000, 50, 50));
        var hit = new HashSet<ulong>(index.Query(0, 0, 100, 100));
        Assert.Equal(new HashSet<ulong> { 1, 2 }, hit);
        Assert.Empty(index.Query(5000, 5000, 10, 10));
    }

    [Fact]
    public void Index_HandlesNegativeCoordinatesMoveRemoveClear()
    {
        var index = new CanvasSpatialIndex();
        Assert.True(index.Add(7, -2000, -1500, 300, 200));
        Assert.Contains(7UL, index.Query(-2100, -1600, 500, 500));
        Assert.True(index.Move(7, 5000, 5000, 300, 200));
        Assert.DoesNotContain(7UL, index.Query(-2100, -1600, 500, 500));
        Assert.Contains(7UL, index.Query(4900, 4900, 500, 500));
        Assert.True(index.Remove(7));
        Assert.False(index.Remove(7));
        Assert.Equal(0, index.Count);
        index.Add(8, 0, 0, 10, 10);
        index.Clear();
        Assert.Equal(0, index.Count);
        Assert.Empty(index.Query(-10000, -10000, 20000, 20000));
    }

    [Fact]
    public void Index_RejectsDegenerateRects()
    {
        var index = new CanvasSpatialIndex();
        Assert.False(index.Add(1, 0, 0, 0, 10));
        Assert.False(index.Add(2, 0, 0, 10, -5));
        Assert.False(index.Add(3, double.NaN, 0, 10, 10));
        Assert.Empty(index.Query(0, 0, 0, 0));
        Assert.Empty(index.Query(double.NaN, 0, 10, 10));
        Assert.Equal(0, index.Count);
    }

    [Fact]
    public void Index_MatchesBruteForceOnRandomLayout()
    {
        var random = new Random(1337);
        var index = new CanvasSpatialIndex();
        var rects = new List<(ulong id, double x, double y, double w, double h)>();
        for (ulong id = 1; id <= 500; id++)
        {
            double x = random.NextDouble() * 20000 - 5000;
            double y = random.NextDouble() * 12000 - 3000;
            double w = 50 + random.NextDouble() * 400;
            double h = 50 + random.NextDouble() * 300;
            rects.Add((id, x, y, w, h));
            Assert.True(index.Add(id, x, y, w, h));
        }
        for (int q = 0; q < 30; q++)
        {
            double x = random.NextDouble() * 20000 - 6000;
            double y = random.NextDouble() * 12000 - 4000;
            double zoom = new[] { 0.15, 0.5, 1.0, 2.0, 4.0 }[q % 5];
            double w = 1280 / zoom;
            double h = 800 / zoom;
            var expected = new HashSet<ulong>(rects
                .Where(r => r.x <= x + w && r.x + r.w >= x &&
                            r.y <= y + h && r.y + r.h >= y)
                .Select(r => r.id));
            Assert.Equal(expected, new HashSet<ulong>(index.Query(x, y, w, h)));
        }
    }

    // ── Viewport math ────────────────────────────────────────────────

    [Fact]
    public void ViewportRect_MapsPanZoomToCanvas()
    {
        Rect view = NodeGraphViewModel.ComputeViewportRect(-100, -50, 2.0, 800, 600);
        Assert.Equal(50, view.X, precision: 6);
        Assert.Equal(25, view.Y, precision: 6);
        Assert.Equal(400, view.Width, precision: 6);
        Assert.Equal(300, view.Height, precision: 6);
        Rect fallback = NodeGraphViewModel.ComputeViewportRect(0, 0, 0, 800, 600);
        Assert.Equal(800, fallback.Width, precision: 6);
    }

    // ── Culling behavior ─────────────────────────────────────────────

    [Fact]
    public void Culling_ShowsOnlyVisibleNodes()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var near = AddNode(vm, 1, 100, 100);
            var far = AddNode(vm, 2, 10000, 8000);
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.PanX = 0;
            vm.PanY = 0;
            vm.ZoomScale = 1.0;
            graph.RefreshVisible();
            Assert.Contains(near, graph.VisibleNodes);
            Assert.DoesNotContain(far, graph.VisibleNodes);

            vm.PanX = -10000;
            vm.PanY = -8000;
            Assert.Contains(far, graph.VisibleNodes);
            Assert.DoesNotContain(near, graph.VisibleNodes);
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public void Culling_ConnectionVisibleWhenEndpointVisible()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var a = AddNode(vm, 1, 100, 100);
            var b = AddNode(vm, 2, 400, 100);
            var c = AddNode(vm, 3, 20000, 20000);
            var edgeAB = new ConnectionViewModel(a, b, "go");
            var edgeAC = new ConnectionViewModel(a, c, "far");
            vm.Connections.Add(edgeAB);
            vm.Connections.Add(edgeAC);
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.PanX = 0;
            vm.PanY = 0;
            vm.ZoomScale = 1.0;
            graph.RefreshVisible();
            // A and B visible: AB fully inside; AC touches A so it stays too.
            Assert.Contains(edgeAB, graph.VisibleConnections);
            Assert.Contains(edgeAC, graph.VisibleConnections);

            vm.PanX = -50000;
            vm.PanY = -50000;
            Assert.Empty(graph.VisibleNodes);
            Assert.Empty(graph.VisibleConnections);
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public void Culling_NodeMoveUpdatesVisibility()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var node = AddNode(vm, 1, 30000, 30000);
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.PanX = 0;
            vm.PanY = 0;
            vm.ZoomScale = 1.0;
            graph.RefreshVisible();
            Assert.DoesNotContain(node, graph.VisibleNodes);
            node.X = 200;
            node.Y = 200;
            Assert.Contains(node, graph.VisibleNodes);
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public void FitToSelection_CentersSelectedNode()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var node = AddNode(vm, 1, 1000, 500);
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.ZoomScale = 1.0;
            vm.SelectNodeQuiet(node);
            graph.FitToSelectionCommand.Execute(null);
            var (x, y, w, h) = NodeGraphViewModel.NodeBounds(node);
            Assert.Equal(640 - (x + w / 2), vm.TargetPanX, precision: 3);
            Assert.Equal(400 - (y + h / 2), vm.TargetPanY, precision: 3);
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public void ZoomToFit_FitsWorldAndIgnoresEmpty()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            double beforeZoom = vm.TargetZoom;
            graph.ZoomToFitCommand.Execute(null);
            Assert.Equal(beforeZoom, vm.TargetZoom); // empty: no-op

            AddNode(vm, 1, 0, 0);
            AddNode(vm, 2, 4000, 2000);
            graph.ZoomToFitCommand.Execute(null);
            Rect world = graph.WorldBounds;
            double expected = Math.Min(1280 / world.Width, 800 / world.Height);
            expected = Math.Clamp(expected, 0.15, 4.0);
            Assert.Equal(expected, vm.TargetZoom, precision: 6);
            Assert.Equal(640 - (world.X + world.Width / 2) * expected,
                vm.TargetPanX, precision: 3);
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public void PanTo_CentersCanvasPoint()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.ZoomScale = 2.0;
            graph.PanTo(100, 200);
            Assert.Equal(640 - 200, vm.PanX, precision: 6);
            Assert.Equal(400 - 400, vm.PanY, precision: 6);
        }
        finally
        {
            shell.Dispose();
        }
    }

    // ── Minimap mapping ──────────────────────────────────────────────

    [Fact]
    public void Minimap_MapsControlPointToCanvas()
    {
        var world = new Rect(0, 0, 2000, 1000);
        var center = MinimapProjection.CanvasFromControlPoint(
            new Size(200, 150), world, new Point(100, 75));
        Assert.NotNull(center);
        Assert.Equal(1000, center!.Value.X, precision: 6);
        Assert.Equal(500, center!.Value.Y, precision: 6);
        Assert.Null(MinimapProjection.CanvasFromControlPoint(
            new Size(0, 150), world, new Point(0, 0)));
        Assert.Null(MinimapProjection.CanvasFromControlPoint(
            new Size(200, 150), new Rect(0, 0, 0, 0), new Point(0, 0)));
    }

    // ── Faz 6 Dilim 4: minimap kelepçesi ───────────────────────────────

    [Fact]
    public void WorldBounds_UnionsNodesWithViewportAndKeepsEmptyDegenerate()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.PanX = 0;
            vm.PanY = 0;
            vm.ZoomScale = 1.0;
            graph.RefreshVisible();
            // Boş graf dejenereliği korunur.
            Assert.Equal(0, graph.WorldBounds.Width);
            Assert.Equal(0, graph.WorldBounds.Height);

            var node = AddNode(vm, 1, 100, 100);
            graph.RefreshVisible();
            var (nx, ny, nw, nh) = NodeGraphViewModel.NodeBounds(node);
            Rect view = graph.ViewportRect;
            Rect world = graph.WorldBounds;
            // El hesabı birleşim + 64px pay (üretim sabitiyle aynı).
            double x1 = Math.Min(nx, view.X) - 64;
            double y1 = Math.Min(ny, view.Y) - 64;
            double x2 = Math.Max(nx + nw, view.X + view.Width) + 64;
            double y2 = Math.Max(ny + nh, view.Y + view.Height) + 64;
            Assert.Equal(x1, world.X, precision: 6);
            Assert.Equal(y1, world.Y, precision: 6);
            Assert.Equal(x2 - x1, world.Width, precision: 6);
            Assert.Equal(y2 - y1, world.Height, precision: 6);

            // Uzak pan: dünya görünümü de sığdırır, çerçeve içeride kalır.
            vm.PanX = -10000;
            Rect view2 = graph.ViewportRect;
            Rect world2 = graph.WorldBounds;
            Assert.True(world2.X <= view2.X - 64 + 1e-6);
            Assert.True(world2.Y <= view2.Y - 64 + 1e-6);
            Assert.True(world2.X + world2.Width >= view2.X + view2.Width + 64 - 1e-6);
            Assert.True(world2.Y + world2.Height >= view2.Y + view2.Height + 64 - 1e-6);
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public void Minimap_ClampsDragTargetToWorld()
    {
        var world = new Rect(0, 0, 2000, 1000);
        var size = new Size(200, 150);
        // Ölçek 0.1, ofset (0, 25): (500,500) -> (5000,4750) -> kelepçelenir.
        var far = MinimapProjection.CanvasFromControlPoint(size, world, new Point(500, 500));
        Assert.NotNull(far);
        Assert.Equal(2000, far!.Value.X, precision: 6);
        Assert.Equal(1000, far.Value.Y, precision: 6);
        var neg = MinimapProjection.CanvasFromControlPoint(size, world, new Point(-50, -50));
        Assert.NotNull(neg);
        Assert.Equal(0, neg!.Value.X, precision: 6);
        Assert.Equal(0, neg.Value.Y, precision: 6);
        // İç nokta aynen eşlenir (kelepçe etkisiz).
        var center = MinimapProjection.CanvasFromControlPoint(size, world, new Point(100, 75));
        Assert.NotNull(center);
        Assert.Equal(1000, center!.Value.X, precision: 6);
        Assert.Equal(500, center.Value.Y, precision: 6);
    }

    // ── 2.000 node / 6.000 edge gate ─────────────────────────────────

    [Fact]
    public void ScaleFixture_CullingParityAndBudget()
    {
        using var shell = new CulledShell();
        MainWindowViewModel vm = shell.Vm;
        try
        {
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1920;
            graph.ViewportHeight = 1080;

            var budget = Stopwatch.StartNew();
            graph.BeginBulkUpdate();
            const int cols = 50;
            for (ulong id = 1; id <= 2000; id++)
            {
                double x = ((id - 1) % cols) * 400.0;
                double y = ((id - 1) / cols) * 300.0;
                vm.Nodes.Add(new NodeViewModel(id, "Scale Node " + id, x, y));
            }
            for (ulong id = 1; id <= 2000; id++)
            {
                var source = vm.Nodes[(int)id - 1];
                for (ulong step = 1; step <= 3; step++)
                {
                    ulong target = id + step;
                    if (target > 2000)
                        target -= 2000;
                    vm.Connections.Add(new ConnectionViewModel(
                        source, vm.Nodes[(int)target - 1], "opt_" + step));
                }
            }
            graph.EndBulkUpdate();
            budget.Stop();
            Assert.Equal(2000, vm.Nodes.Count);
            Assert.Equal(6000, vm.Connections.Count);
            Assert.True(budget.Elapsed.TotalMilliseconds < 2000,
                $"Bulk index build took {budget.Elapsed.TotalMilliseconds:F0} ms.");

            var pans = new (double x, double y, double zoom)[]
            {
                (0, 0, 1.0), (-5000, -3000, 1.0), (-15000, -9000, 1.0),
                (0, 0, 0.25), (-9000, -5000, 0.5), (-3000, -1500, 2.0),
            };
            var sweep = Stopwatch.StartNew();
            foreach (var (panX, panY, zoom) in pans)
            {
                vm.PanX = panX;
                vm.PanY = panY;
                vm.ZoomScale = zoom;
                graph.RefreshVisible();

                var expectedNodes = BruteForceVisible(vm.Nodes, graph.ViewportRect);
                Assert.Equal(expectedNodes, new HashSet<NodeViewModel>(graph.VisibleNodes));
                var expectedEdges = new HashSet<ConnectionViewModel>(
                    vm.Connections.Where(c =>
                        (c.SourceNode is not null && expectedNodes.Contains(c.SourceNode)) ||
                        (c.TargetNode is not null && expectedNodes.Contains(c.TargetNode))));
                Assert.Equal(expectedEdges,
                    new HashSet<ConnectionViewModel>(graph.VisibleConnections));

                if (Math.Abs(zoom - 1.0) < 0.001 && panX == 0 && panY == 0)
                    Assert.True(graph.VisibleNodes.Count < 200,
                        $"Zoomed-in view materializes {graph.VisibleNodes.Count} nodes.");
            }
            sweep.Stop();
            Assert.True(sweep.Elapsed.TotalMilliseconds < 2000,
                $"Six culled refreshes took {sweep.Elapsed.TotalMilliseconds:F0} ms.");
        }
        finally
        {
            shell.Dispose();
        }
    }
}
