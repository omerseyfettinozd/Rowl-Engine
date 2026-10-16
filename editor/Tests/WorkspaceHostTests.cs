using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.LogicalTree;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels;
using RowlEngine.Editor.Views.Workspace;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 D3 — <see cref="WorkspaceHost"/> draws the split tree: tree → visual
/// tree mapping (headless), splitter drag → tree ratio, panels keep their
/// control instance / focus / scroll / selection across tree changes, one
/// <c>NodeGraphView</c> in split screen, RTL and live language change.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class WorkspaceHostTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public WorkspaceHostTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlWorkspaceHost_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("WorkspaceHostTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
        // KÖK GERİ VERİLMEZ: bkz. EditorAssetGridVirtualizationTests.
    }

    public void Dispose()
    {
        try { Dispatcher.UIThread.Invoke(() => _vm.Dispose()); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); }
        catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    // ---- helpers ----

    private static void Ui(Action action) => Dispatcher.UIThread.Invoke(action);

    /// <summary>Standalone host with placeholder panels; the "owner" applies resize requests to <paramref name="layout"/>.</summary>
    private static (Window Window, WorkspaceHost Host) StandaloneHost(WorkspaceLayout layout, double width = 1440, double height = 820)
    {
        var host = new WorkspaceHost();
        foreach (string id in PanelIds.Editor)
        {
            host.Panels.Add(new WorkspacePanel
            {
                PanelId = id,
                Title = "T:" + id,
                OwnsHeader = id is PanelIds.Hierarchy or PanelIds.Inspector or PanelIds.Bottom,
                PanelMinWidth = id is PanelIds.Hierarchy ? WorkspacePanelMetrics.HierarchyMinWidth
                    : id is PanelIds.Inspector ? WorkspacePanelMetrics.InspectorMinWidth
                    : id is PanelIds.Bottom ? WorkspacePanel.DefaultMinWidth
                    : WorkspacePanelMetrics.CenterMinWidth,
                PanelMinHeight = id is PanelIds.Bottom ? WorkspacePanelMetrics.BottomMinHeight : WorkspacePanel.DefaultMinHeight,
                Content = new TextBox { Name = "Box_" + id, Text = id },
            });
        }
        host.Layout = layout;
        host.SplitResized += (_, e) => host.Layout = host.Layout!.ResizeSplit(e.SplitId, e.Ratio);
        var window = new Window { Width = width, Height = height, Content = host };
        window.Show();
        window.UpdateLayout();
        return (window, host);
    }

    private static MainWindow OpenMainWindow(MainWindowViewModel vm, double width = 1440, double height = 900)
    {
        var window = new MainWindow(vm) { Width = width, Height = height };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        window.UpdateLayout();
        return window;
    }

    private static WorkspaceHost HostOf(Window window) =>
        window.FindControl<WorkspaceHost>("WorkspaceHost") ?? throw new Exception("WorkspaceHost not found");

    private static void AssertNear(double expected, double actual, double tolerance, string what) =>
        Assert.True(Math.Abs(expected - actual) <= tolerance, $"{what}: expected {expected} ± {tolerance}, got {actual}");

    private static void AssertNoOverlap(WorkspaceHost host, IEnumerable<string> views)
    {
        Rect[] rects = views.Select(host.GetPanelRect).ToArray();
        for (int i = 0; i < rects.Length; i++)
        {
            Assert.True(rects[i].Width > 0 && rects[i].Height > 0, "visible panel has no area");
            for (int j = i + 1; j < rects.Length; j++)
                Assert.False(rects[i].Intersects(rects[j]), $"panels overlap: {rects[i]} / {rects[j]}");
        }
    }

    // ---- tree → visual tree ----

    [Fact]
    public void TreeMapsToVisualTree_PanelsOverSlots_SplittersPerSplit()
    {
        Ui(() =>
        {
            WorkspaceLayout layout = EditorWorkspaceState.Default.Layout;
            var (window, host) = StandaloneHost(layout);
            try
            {
                IReadOnlyList<string> views = layout.Views;
                foreach (string id in PanelIds.Editor)
                    Assert.Equal(views.Contains(id), host.FindPanel(id)!.IsVisible);
                AssertNoOverlap(host, views);

                // x(hierarchy, x(y(node, bottom), inspector)): positions follow the tree.
                Rect h = host.GetPanelRect(PanelIds.Hierarchy);
                Rect n = host.GetPanelRect(PanelIds.Node);
                Rect b = host.GetPanelRect(PanelIds.Bottom);
                Rect i = host.GetPanelRect(PanelIds.Inspector);
                Assert.Equal(0, h.X);
                AssertNear(h.Right + WorkspaceHost.SplitterThickness, n.X, 0.5, "node after hierarchy splitter");
                AssertNear(n.Bottom + WorkspaceHost.SplitterThickness, b.Y, 0.5, "bottom under node");
                AssertNear(n.X, b.X, 0.5, "bottom aligned with node");
                AssertNear(n.Right + WorkspaceHost.SplitterThickness, i.X, 0.5, "inspector after center");
                AssertNear(1440, i.Right, 0.5, "inspector reaches the right edge");
                AssertNear(820, b.Bottom, 0.5, "bottom reaches the bottom edge");

                // Default ratios ≈ the old fixed pixels at the reference size.
                AssertNear(200, h.Width, 5, "hierarchy width");
                AssertNear(280, i.Width, 5, "inspector width");
                AssertNear(180, b.Height, 5, "bottom height");

                // One GridSplitter per split, star definitions = tree ratios.
                int splits = 0;
                void Visit(LayoutNode? node)
                {
                    if (node is not SplitNode split)
                        return;
                    splits++;
                    GridSplitter splitter = host.FindSplitter(split.Id)!;
                    Assert.Equal(split.Axis == SplitAxis.Horizontal ? GridResizeDirection.Columns : GridResizeDirection.Rows, splitter.ResizeDirection);
                    Grid grid = host.FindSplitGrid(split.Id)!;
                    GridLength a = split.Axis == SplitAxis.Horizontal ? grid.ColumnDefinitions[0].Width : grid.RowDefinitions[0].Height;
                    GridLength bLen = split.Axis == SplitAxis.Horizontal ? grid.ColumnDefinitions[2].Width : grid.RowDefinitions[2].Height;
                    Assert.True(a.IsStar && bLen.IsStar);
                    AssertNear(split.Ratio, a.Value / (a.Value + bLen.Value), 1e-9, "star ratio");
                    Visit(split.A);
                    Visit(split.B);
                }
                Visit(layout.Tree);
                Assert.Equal(3, splits);
                Assert.Equal(splits, host.GetVisualDescendants().OfType<GridSplitter>().Count());

                // Header rule: host header only for panels without their own header.
                foreach (string id in PanelIds.Editor)
                {
                    WorkspacePanel panel = host.FindPanel(id)!;
                    if (!panel.IsVisible)
                        continue;
                    Assert.NotNull(panel.HostHeader);
                    Assert.Equal(!panel.OwnsHeader, panel.HostHeader!.IsVisible);
                    Assert.Contains("panelHeader", panel.HostHeader.Classes);
                    TextBlock title = panel.HostHeader.GetVisualDescendants().OfType<TextBlock>().Single();
                    Assert.Contains("panelTitle", title.Classes);
                    Assert.Equal("T:" + id, title.Text);
                }
                AssertNear(40, host.FindPanel(PanelIds.Node)!.HostHeader!.Bounds.Height, 0.5, "panel header height (K2)");

                // Empty tree: everything hidden, no skeleton.
                host.Layout = new WorkspaceLayout(null, 0);
                window.UpdateLayout();
                Assert.All(PanelIds.Editor, id => Assert.False(host.FindPanel(id)!.IsVisible));
                Assert.Empty(host.GetVisualDescendants().OfType<GridSplitter>());
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void PanelMinimums_HoldWhenTheWindowShrinks()
    {
        Ui(() =>
        {
            var (window, host) = StandaloneHost(EditorWorkspaceState.Default.Layout, width: 1100, height: 600);
            try
            {
                // 0.14 × 1094 ≈ 153 < 200: the pixel minimum wins over the ratio.
                AssertNear(WorkspacePanelMetrics.HierarchyMinWidth, host.GetPanelRect(PanelIds.Hierarchy).Width, 0.5, "hierarchy minimum");
                Assert.True(host.GetPanelRect(PanelIds.Inspector).Width >= WorkspacePanelMetrics.InspectorMinWidth - 0.5);
                Assert.True(host.GetPanelRect(PanelIds.Bottom).Height >= WorkspacePanelMetrics.BottomMinHeight - 0.5);
                Assert.True(host.GetPanelRect(PanelIds.Node).Width >= WorkspacePanelMetrics.CenterMinWidth - 0.5);
                AssertNoOverlap(host, host.Layout!.Views);
            }
            finally
            {
                window.Close();
            }
        });
    }

    // ---- splitter → tree ----

    [Fact]
    public void SplitterDrag_WritesRatioToTree_OneWay()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            try
            {
                WorkspaceHost host = HostOf(window);
                var root = (SplitNode)_vm.WorkspaceLayout.Tree!;
                GridSplitter splitter = host.FindSplitter(root.Id)!;
                Point center = splitter.TranslatePoint(new Point(3, splitter.Bounds.Height / 2), window)!.Value;
                double before = host.GetPanelRect(PanelIds.Hierarchy).Width;

                window.MouseDown(center, MouseButton.Left);
                window.MouseMove(center + new Point(30, 0));
                window.MouseMove(center + new Point(60, 0));
                window.MouseUp(center + new Point(60, 0), MouseButton.Left);
                window.UpdateLayout();

                var newRoot = (SplitNode)_vm.WorkspaceLayout.Tree!;
                Assert.Equal(root.Id, newRoot.Id);
                double width = host.GetPanelRect(PanelIds.Hierarchy).Width;
                AssertNear(before + 60, width, 2, "dragged hierarchy width");
                AssertNear(width / (host.Bounds.Width - WorkspaceHost.SplitterThickness), newRoot.Ratio, 0.003, "tree ratio = drawn share");

                // The tree drives the view: a ratio set in the model is drawn (and clamped).
                _vm.ResizeWorkspaceSplit(root.Id, 0.3);
                window.UpdateLayout();
                AssertNear(0.3 * (host.Bounds.Width - 6), host.GetPanelRect(PanelIds.Hierarchy).Width, 1, "model ratio drawn");
                _vm.ResizeWorkspaceSplit(root.Id, 0.0);
                Assert.Equal(WorkspaceLayout.MinRatio, ((SplitNode)_vm.WorkspaceLayout.Tree!).Ratio);
                window.UpdateLayout();
                // 0.08 × 1434 ≈ 115 px is below the panel minimum: the minimum wins.
                AssertNear(WorkspacePanelMetrics.HierarchyMinWidth, host.GetPanelRect(PanelIds.Hierarchy).Width, 0.5, "minimum kept");
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SplitterKeyboardMove_WritesRatioToTree()
    {
        Ui(() =>
        {
            var (window, host) = StandaloneHost(EditorWorkspaceState.Default.Layout);
            try
            {
                var bottomSplit = (SplitNode)((SplitNode)((SplitNode)host.Layout!.Tree!).B).A;
                GridSplitter splitter = host.FindSplitter(bottomSplit.Id)!;
                double before = host.GetPanelRect(PanelIds.Bottom).Height;
                splitter.Focus();
                window.KeyPressQwerty(PhysicalKey.ArrowUp, RawInputModifiers.None);
                window.KeyReleaseQwerty(PhysicalKey.ArrowUp, RawInputModifiers.None);
                window.UpdateLayout();
                double after = host.GetPanelRect(PanelIds.Bottom).Height;
                Assert.True(after > before, $"keyboard move did not grow the strip ({before} → {after})");
                var split = (SplitNode)((SplitNode)((SplitNode)host.Layout!.Tree!).B).A;
                Assert.True(split.Ratio < bottomSplit.Ratio, "keyboard move was not written to the tree");
            }
            finally
            {
                window.Close();
            }
        });
    }

    // ---- instances, focus, scroll, selection ----

    [Fact]
    public void TreeChanges_KeepControlInstances_Focus_Scroll_Selection()
    {
        Ui(() =>
        {
            var (window, host) = StandaloneHost(EditorWorkspaceState.Default.Layout);
            try
            {
                Dictionary<string, object?> contents = PanelIds.Editor.ToDictionary(id => id, id => host.FindPanel(id)!.Content);
                var box = (TextBox)host.FindPanel(PanelIds.Node)!.Content!;
                box.Text = string.Join("\n", Enumerable.Range(0, 200).Select(i => "line " + i));
                box.AcceptsReturn = true;
                window.UpdateLayout();
                box.Focus();
                box.SelectionStart = 3;
                box.SelectionEnd = 9;
                ScrollViewer scroll = box.GetVisualDescendants().OfType<ScrollViewer>().First();
                scroll.Offset = new Vector(0, 120);
                window.UpdateLayout();
                Vector offset = scroll.Offset;
                Assert.True(offset.Y > 0);

                EditorWorkspaceState state = EditorWorkspaceState.Default;
                foreach (Func<EditorWorkspaceState, EditorWorkspaceState> change in new Func<EditorWorkspaceState, EditorWorkspaceState>[]
                {
                    s => s.WithPanel(PanelIds.Hierarchy, false),          // root split removed: subtree moves up
                    s => s.WithPanel(PanelIds.Inspector, false),
                    s => s.WithBottomTab(0, false).WithBottomTab(1, false), // strip leaf removed
                    s => s.WithSplitScreen(2),                            // center becomes split(node, edit)
                    s => s.WithSplitScreen(1),
                    s => s.WithPanel(PanelIds.Hierarchy, true).WithPanel(PanelIds.Inspector, true).WithBottomTab(0, true),
                    s => s.WithCenter(PanelIds.Node),
                })
                {
                    state = change(state);
                    host.Layout = state.Layout;
                    window.UpdateLayout();
                    Assert.True(host.FindPanel(PanelIds.Node)!.IsVisible);
                    Assert.Same(box, window.FocusManager!.GetFocusedElement());
                    Assert.Equal(offset, scroll.Offset);
                    Assert.Equal(3, box.SelectionStart);
                    Assert.Equal(9, box.SelectionEnd);
                    foreach (string id in PanelIds.Editor)
                        Assert.Same(contents[id], host.FindPanel(id)!.Content);
                    Assert.Same(host, host.FindPanel(PanelIds.Node)!.GetVisualParent());
                    AssertNoOverlap(host, state.Layout.Views);
                }

                // A closed panel keeps its instance (and stays attached, as before D3) and returns with it.
                TextBox hierarchyBox = (TextBox)contents[PanelIds.Hierarchy]!;
                state = state.WithPanel(PanelIds.Hierarchy, false);
                host.Layout = state.Layout;
                window.UpdateLayout();
                Assert.False(host.FindPanel(PanelIds.Hierarchy)!.IsVisible);
                Assert.True(hierarchyBox.IsAttachedToVisualTree());
                state = state.WithPanel(PanelIds.Hierarchy, true);
                host.Layout = state.Layout;
                window.UpdateLayout();
                Assert.Same(hierarchyBox, host.FindPanel(PanelIds.Hierarchy)!.Content);
                Assert.True(host.GetPanelRect(PanelIds.Hierarchy).Width > 0);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void MainWindow_PanelToggle_KeepsInstances_AndFocus()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            try
            {
                WorkspaceHost host = HostOf(window);
                NodeHierarchyView hierarchy = window.GetLogicalDescendants().OfType<NodeHierarchyView>().Single();
                NodeInspectorView inspector = window.GetLogicalDescendants().OfType<NodeInspectorView>().Single();
                TextBox search = hierarchy.GetVisualDescendants().OfType<TextBox>().First();
                search.Focus();
                search.Text = "abc";

                _vm.ShowPanel("Inspector");   // close
                window.UpdateLayout();
                Assert.False(host.FindPanel(PanelIds.Inspector)!.IsVisible);
                _vm.ShowPanel("Inspector");   // reopen
                _vm.ShowPanel("Log");         // bottom strip changes
                _vm.ShowPanel("SplitScreen");
                window.UpdateLayout();

                Assert.Same(search, window.FocusManager!.GetFocusedElement());
                Assert.Equal("abc", search.Text);
                Assert.Same(hierarchy, window.GetLogicalDescendants().OfType<NodeHierarchyView>().Single());
                Assert.Same(inspector, window.GetLogicalDescendants().OfType<NodeInspectorView>().Single());
                Assert.True(host.GetPanelRect(PanelIds.Inspector).Width > 0);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void MainWindow_DefaultLayout_MatchesOldPixelLayout()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            try
            {
                WorkspaceHost host = HostOf(window);
                // Pre-D3 MainWindow: Hierarchy 200, Inspector 280, bottom strip 180 px (±10%).
                AssertNear(200, host.GetPanelRect(PanelIds.Hierarchy).Width, 20, "hierarchy");
                AssertNear(280, host.GetPanelRect(PanelIds.Inspector).Width, 28, "inspector");
                AssertNear(180, host.GetPanelRect(PanelIds.Bottom).Height, 18, "bottom strip");
                Assert.Equal(new[] { PanelIds.Hierarchy, PanelIds.Node, PanelIds.Bottom, PanelIds.Inspector }.OrderBy(v => v),
                    PanelIds.Editor.Where(id => host.FindPanel(id)!.IsVisible).OrderBy(v => v));

                // Header rule: own headers (Hierarchy / Inspector panelHeader, strip tab row)
                // are not doubled; the center view gets the host header with its catalog title.
                foreach (string id in new[] { PanelIds.Hierarchy, PanelIds.Inspector })
                {
                    WorkspacePanel panel = host.FindPanel(id)!;
                    Assert.True(panel.OwnsHeader);
                    Assert.False(panel.HostHeader!.IsVisible);
                    Border own = Assert.IsType<Border>(host.FindHeader(id));
                    Assert.Contains("panelHeader", own.Classes);
                    Assert.True(own.IsEffectivelyVisible);
                }
                Assert.IsType<TabControl>(host.FindHeader(PanelIds.Bottom));
                WorkspacePanel node = host.FindPanel(PanelIds.Node)!;
                Assert.False(node.OwnsHeader);
                Assert.Same(node.HostHeader, host.FindHeader(PanelIds.Node));
                Assert.Equal(EditorStrings.T("View.Node"), ((TextBlock)node.HostHeader!.Child!).Text);
                Assert.Equal(EditorStrings.T("View.SceneEdit"), host.FindPanel(PanelIds.Edit)!.Title);
                Assert.Equal(EditorStrings.T("View.Game"), host.FindPanel(PanelIds.Game)!.Title);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SplitScreen_HasExactlyOneNodeGraphView_AndOnePreviewEach()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            try
            {
                WorkspaceHost host = HostOf(window);
                NodeGraphView graph = window.GetLogicalDescendants().OfType<NodeGraphView>().Single();
                LivePreviewControl live = window.GetLogicalDescendants().OfType<LivePreviewControl>().Single();
                EnginePreviewControl game = window.GetLogicalDescendants().OfType<EnginePreviewControl>().Single();

                foreach ((string action, int mode, bool stacked) in new[] { ("SplitScreen", 1, true), ("SplitScreen", 2, false), ("SplitScreen", 0, false) })
                {
                    _vm.ShowPanel(action);
                    window.UpdateLayout();
                    Assert.Equal(mode, _vm.SplitScreenMode);
                    Assert.Same(graph, window.GetLogicalDescendants().OfType<NodeGraphView>().Single());
                    Assert.Same(live, window.GetLogicalDescendants().OfType<LivePreviewControl>().Single());
                    Assert.Same(game, window.GetLogicalDescendants().OfType<EnginePreviewControl>().Single());
                    Assert.True(graph.IsEffectivelyVisible);
                    Assert.Equal(mode != 0, live.IsEffectivelyVisible);
                    if (mode != 0)
                    {
                        Rect n = host.GetPanelRect(PanelIds.Node);
                        Rect e = host.GetPanelRect(PanelIds.Edit);
                        Assert.False(n.Intersects(e));
                        if (stacked)
                            AssertNear(n.Bottom + WorkspaceHost.SplitterThickness, e.Y, 0.5, "edit under node");
                        else
                            AssertNear(n.Right + WorkspaceHost.SplitterThickness, e.X, 0.5, "edit beside node");
                    }
                }

                _vm.ShowPanel("EnginePreview");
                window.UpdateLayout();
                Assert.True(game.IsEffectivelyVisible);
                Assert.False(graph.IsEffectivelyVisible);
                Assert.Same(game, window.GetLogicalDescendants().OfType<EnginePreviewControl>().Single());
            }
            finally
            {
                window.Close();
            }
        });
    }

    // ---- RTL and live language ----

    [Fact]
    public void RightToLeft_MirrorsTheDrawnWorkspace_LayoutStaysTreeOrdered()
    {
        Ui(() =>
        {
            var (window, host) = StandaloneHost(EditorWorkspaceState.Default.Layout);
            try
            {
                Rect ltrHierarchy = host.GetPanelRect(PanelIds.Hierarchy);
                Rect ltrInspector = host.GetPanelRect(PanelIds.Inspector);
                window.FlowDirection = FlowDirection.RightToLeft;
                window.UpdateLayout();

                // Layout space is unchanged (tree order: A before B) ...
                Assert.Equal(ltrHierarchy, host.GetPanelRect(PanelIds.Hierarchy));
                Assert.Equal(ltrInspector, host.GetPanelRect(PanelIds.Inspector));
                Assert.Equal(FlowDirection.RightToLeft, host.FindPanel(PanelIds.Node)!.FlowDirection);
                // ... and the window mirrors it: Hierarchy is drawn at the right edge (Start = right in RTL).
                WorkspacePanel hierarchy = host.FindPanel(PanelIds.Hierarchy)!;
                WorkspacePanel inspector = host.FindPanel(PanelIds.Inspector)!;
                Matrix toWindowH = hierarchy.TransformToVisual(window)!.Value;
                Matrix toWindowI = inspector.TransformToVisual(window)!.Value;
                Rect drawnH = new Rect(hierarchy.Bounds.Size).TransformToAABB(toWindowH);
                Rect drawnI = new Rect(inspector.Bounds.Size).TransformToAABB(toWindowI);
                Assert.True(drawnH.X > drawnI.X, $"RTL: hierarchy {drawnH} should be right of inspector {drawnI}");
                AssertNear(1440, drawnH.Right, 0.5, "hierarchy at the right edge");

                // Splitter drag still writes the ratio in RTL (drag towards the center = left).
                var root = (SplitNode)host.Layout!.Tree!;
                GridSplitter splitter = host.FindSplitter(root.Id)!;
                Rect drawnSplitter = new Rect(splitter.Bounds.Size).TransformToAABB(splitter.TransformToVisual(window)!.Value);
                Point p = drawnSplitter.Center;
                window.MouseDown(p, MouseButton.Left);
                window.MouseMove(p - new Point(40, 0));
                window.MouseUp(p - new Point(40, 0), MouseButton.Left);
                window.UpdateLayout();
                Assert.True(((SplitNode)host.Layout!.Tree!).Ratio > root.Ratio, "RTL drag towards the center must grow Hierarchy");
                AssertNear(ltrHierarchy.Width + 40, host.GetPanelRect(PanelIds.Hierarchy).Width, 2, "RTL dragged width");
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void LiveLanguageChange_UpdatesHostHeaders()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            EditorStrings strings = EditorStrings.Instance;
            try
            {
                _vm.ShowPanel("SplitScreen");
                window.UpdateLayout();
                WorkspacePanel edit = HostOf(window).FindPanel(PanelIds.Edit)!;
                var title = (TextBlock)edit.HostHeader!.Child!;
                Assert.Equal("Sahne Düzenle", title.Text);
                strings.SetLanguage("en");
                Assert.Equal("Edit Scene", title.Text);
                strings.SetLanguage("tr");
                Assert.Equal("Sahne Düzenle", title.Text);
            }
            finally
            {
                strings.SetLanguage("tr");
                window.Close();
            }
        });
    }
}
