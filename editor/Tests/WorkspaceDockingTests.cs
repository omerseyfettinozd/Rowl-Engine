using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Interactivity;
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
/// Faz 5 D4 — moving panels by their header: real (headless) pointer drags to
/// the four edges, click threshold, Escape, pinned lock, self / outside drops,
/// close + reopen, the keyboard / context-menu path, right-to-left windows and
/// panels that draw their own header (Hierarchy, Inspector, bottom strip).
/// Tests check behaviour (trees, flags), never catalog text.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class WorkspaceDockingTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public WorkspaceDockingTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlWorkspaceDocking_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("WorkspaceDockingTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
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

    /// <summary>Node | Sahne Düzenle side by side (split screen 2) inside the full default frame.</summary>
    private static EditorWorkspaceState SplitState() =>
        EditorWorkspaceLayouts.Build(hierarchy: true, inspector: true, BottomTabs.Assets | BottomTabs.Log, activeTab: 0, PanelIds.Node, splitScreenMode: 2);

    /// <summary>The owner side of the one-way flow: applies the host's requests with the real state operations.</summary>
    private sealed class Owner
    {
        public Owner(WorkspaceHost host, EditorWorkspaceState state)
        {
            Host = host;
            host.PanelDockRequested += (_, e) => { DockRequests++; Set(State.Dock(e.SourcePanelId, e.TargetPanelId, e.Edge)); };
            host.PanelPinRequested += (_, e) => Set(State.WithPinned(e.PanelId, e.Pinned));
            host.PanelCloseRequested += (_, e) => { CloseRequests++; Set(State.ClosePanel(e.PanelId)); };
            host.SplitResized += (_, e) => Set(State.WithSplitRatio(e.SplitId, e.Ratio));
            Set(state);
        }

        public WorkspaceHost Host { get; }

        public EditorWorkspaceState State { get; private set; } = null!;

        public int DockRequests { get; private set; }

        public int CloseRequests { get; private set; }

        public void Set(EditorWorkspaceState state)
        {
            State = state;
            Host.Layout = state.Layout;
            Host.PinnedPanels = state.Pinned;
        }
    }

    private static (Window Window, WorkspaceHost Host, Owner Owner) StandaloneHost(EditorWorkspaceState state, bool rightToLeft = false)
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
        var owner = new Owner(host, state);
        var window = new Window { Width = 1440, Height = 820, Content = host };
        if (rightToLeft)
            window.FlowDirection = FlowDirection.RightToLeft;
        window.Show();
        window.UpdateLayout();
        return (window, host, owner);
    }

    private static MainWindow OpenMainWindow(MainWindowViewModel vm)
    {
        var window = new MainWindow(vm) { Width = 1440, Height = 900 };
        window.Show();
        Dispatcher.UIThread.RunJobs();
        window.UpdateLayout();
        return window;
    }

    private static WorkspaceHost HostOf(Window window) =>
        window.FindControl<WorkspaceHost>("WorkspaceHost") ?? throw new Exception("WorkspaceHost not found");

    /// <summary>Where <paramref name="local"/> (in <paramref name="visual"/>'s space) is drawn in the window.</summary>
    private static Point InWindow(Visual visual, Point local, Window window) =>
        visual.TranslatePoint(local, window) ?? throw new Exception("not in the window");

    /// <summary>A point inside a panel near one of its edges, in host layout space (logical edge).</summary>
    private static Point NearEdge(Rect r, DockEdge edge) => edge switch
    {
        DockEdge.Left => new Point(r.X + r.Width * 0.08, r.Center.Y),
        DockEdge.Right => new Point(r.Right - r.Width * 0.08, r.Center.Y),
        DockEdge.Top => new Point(r.Center.X, r.Y + r.Height * 0.08),
        _ => new Point(r.Center.X, r.Bottom - r.Height * 0.08),
    };

    /// <summary>Free spot on a host header (on the title, away from the pin / close buttons).</summary>
    private static Point HeaderGrip(WorkspaceHost host, string panelId, Window window)
    {
        Control header = host.FindHeader(panelId)!;
        return InWindow(header, new Point(30, header.Bounds.Height / 2), window);
    }

    private static void Press(Window window, Point p) => window.MouseDown(p, MouseButton.Left);

    private static void MoveTo(Window window, Point p) => window.MouseMove(p);

    private static void Release(Window window, Point p)
    {
        window.MouseUp(p, MouseButton.Left);
        window.UpdateLayout();
    }

    /// <summary>Press on <paramref name="from"/>, pass the threshold, move to <paramref name="to"/> (still pressed).</summary>
    private static void DragTo(Window window, Point from, Point to)
    {
        Press(window, from);
        MoveTo(window, from + new Point(10, 0));
        MoveTo(window, to);
        window.UpdateLayout();
    }

    private static void Click(Window window, Control control)
    {
        Point p = InWindow(control, new Rect(control.Bounds.Size).Center, window);
        window.MouseDown(p, MouseButton.Left);
        window.MouseUp(p, MouseButton.Left);
        window.UpdateLayout();
    }

    private static MenuItem MenuEntry(ContextMenu menu, object tag) =>
        menu.Items.OfType<MenuItem>().Single(i => Equals(i.Tag, tag));

    private static void Invoke(MenuItem item) => item.RaiseEvent(new RoutedEventArgs(MenuItem.ClickEvent));

    /// <summary>Move → (visual edge) → target, through the menu's own items.</summary>
    private static void MenuMove(ContextMenu menu, DockEdge visualEdge, string target)
    {
        MenuItem move = MenuEntry(menu, "move");
        Assert.True(move.IsEnabled);
        MenuItem edge = move.Items.OfType<MenuItem>().Single(i => Equals(i.Tag, visualEdge));
        Invoke(edge.Items.OfType<MenuItem>().Single(i => Equals(i.Tag, target)));
    }

    private static void AssertNear(Rect expected, Rect actual)
    {
        Assert.True(Math.Abs(expected.X - actual.X) <= 1 && Math.Abs(expected.Y - actual.Y) <= 1
            && Math.Abs(expected.Width - actual.Width) <= 1 && Math.Abs(expected.Height - actual.Height) <= 1,
            $"expected {expected} ± 1, got {actual}");
    }

    private static Rect Drawn(Visual visual, Window window) =>
        new Rect(visual.Bounds.Size).TransformToAABB(visual.TransformToVisual(window)!.Value);

    // ---- pure state (model) ----

    [Fact]
    public void State_Dock_UsesTheTree_AndRespectsPins()
    {
        EditorWorkspaceState state = SplitState();
        foreach (DockEdge edge in Enum.GetValues<DockEdge>())
        {
            EditorWorkspaceState docked = state.Dock(PanelIds.Edit, PanelIds.Node, edge);
            Assert.Equal(state.Layout.DockView(PanelIds.Edit, PanelIds.Node, edge), docked.Layout);
            Assert.True(docked.Layout.IsValid);
            Assert.Equal(state.Bottom, docked.Bottom);
        }
        Assert.Same(state, state.Dock(PanelIds.Edit, PanelIds.Edit, DockEdge.Left));
        Assert.Same(state, state.Dock(PanelIds.Game, PanelIds.Node, DockEdge.Left)); // not in the tree

        EditorWorkspaceState pinned = state.WithPinned(PanelIds.Edit, true);
        Assert.True(pinned.IsPinned(PanelIds.Edit));
        Assert.Same(pinned, pinned.Dock(PanelIds.Edit, PanelIds.Node, DockEdge.Top));
        // A pinned panel is still a valid target.
        Assert.NotSame(pinned, pinned.Dock(PanelIds.Node, PanelIds.Edit, DockEdge.Top));
        // Value equality: pin + unpin is the original state again.
        Assert.Equal(state, pinned.TogglePin(PanelIds.Edit));
        Assert.Same(state, state.WithPinned(PanelIds.Game, true)); // only panels in the tree
    }

    [Fact]
    public void State_ClosePanel_EachKind_ReopensThroughExistingActions_PinsFollow()
    {
        EditorWorkspaceState state = SplitState().WithPinned(PanelIds.Hierarchy, true).WithPinned(PanelIds.Bottom, true);

        EditorWorkspaceState noHierarchy = state.ClosePanel(PanelIds.Hierarchy);
        Assert.False(noHierarchy.IsHierarchyVisible);
        Assert.False(noHierarchy.IsPinned(PanelIds.Hierarchy)); // a closed panel loses its pin
        Assert.True(noHierarchy.IsPinned(PanelIds.Bottom));
        EditorWorkspaceState back = noHierarchy.HandlePanelAction("Hierarchy");
        Assert.True(back.IsHierarchyVisible);
        Assert.False(back.IsPinned(PanelIds.Hierarchy));

        EditorWorkspaceState noBottom = state.ClosePanel(PanelIds.Bottom);
        Assert.False(noBottom.IsBottomVisible);
        Assert.Equal(BottomTabs.None, noBottom.Bottom.Open);
        Assert.DoesNotContain(PanelIds.Bottom, noBottom.Pinned);
        EditorWorkspaceState log = noBottom.HandlePanelAction("Log");
        Assert.True(log.IsBottomVisible && log.Bottom.IsOpen(BottomStripState.LogTab));

        EditorWorkspaceState noEdit = state.ClosePanel(PanelIds.Edit);
        Assert.DoesNotContain(PanelIds.Edit, noEdit.Layout.Views);
        Assert.True(noEdit.Layout.IsValid);
        Assert.Contains(PanelIds.Edit, noEdit.HandlePanelAction("Preview").Layout.Views);
        EditorWorkspaceState noNode = state.ClosePanel(PanelIds.Node);
        Assert.False(noNode.IsNodeGraphActive);
        Assert.True(noNode.HandlePanelAction("NodeGraph").IsNodeGraphActive);

        // The last panel stays.
        var single = new EditorWorkspaceState(WorkspaceLayout.Initial(), new BottomStripState(BottomTabs.None, 0));
        Assert.False(single.CanClose(PanelIds.Node));
        Assert.Same(single, single.ClosePanel(PanelIds.Node));
        Assert.Same(state, state.ClosePanel(PanelIds.Game)); // not open

        // Presets keep the pins of panels they keep open.
        EditorWorkspaceState preset = state.ApplyPreset(EditorWorkspaceLayouts.SceneFocusPreset);
        Assert.True(preset.IsPinned(PanelIds.Bottom));
        Assert.False(preset.IsPinned(PanelIds.Hierarchy));
        Assert.True(state.WithBottomTab(BottomStripState.BacklogTab, true).IsPinned(PanelIds.Hierarchy));
    }

    [Fact]
    public void DockEdges_VisualToLogical_SwapsSidesOnlyRightToLeft()
    {
        foreach (DockEdge edge in Enum.GetValues<DockEdge>())
        {
            Assert.Equal(edge, WorkspaceDockEdges.FromVisual(edge, rightToLeft: false));
            Assert.Equal(edge, WorkspaceDockEdges.ToVisual(WorkspaceDockEdges.FromVisual(edge, true), true));
        }
        Assert.Equal(DockEdge.Right, WorkspaceDockEdges.FromVisual(DockEdge.Left, true));
        Assert.Equal(DockEdge.Left, WorkspaceDockEdges.FromVisual(DockEdge.Right, true));
        Assert.Equal(DockEdge.Top, WorkspaceDockEdges.FromVisual(DockEdge.Top, true));
        Assert.Equal(DockEdge.Bottom, WorkspaceDockEdges.FromVisual(DockEdge.Bottom, true));
    }

    // ---- drag & drop (standalone host, real pointer events) ----

    [Theory]
    [InlineData(DockEdge.Left)]
    [InlineData(DockEdge.Right)]
    [InlineData(DockEdge.Top)]
    [InlineData(DockEdge.Bottom)]
    public void HeaderDrag_ToEachEdge_DocksThere_WithHighlight(DockEdge edge)
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState());
            try
            {
                EditorWorkspaceState before = owner.State;
                Rect node = host.GetPanelRect(PanelIds.Node);
                Point start = HeaderGrip(host, PanelIds.Edit, window);
                Point target = InWindow(host, NearEdge(node, edge), window);

                DragTo(window, start, target);
                Assert.True(host.IsDragging);
                WorkspaceDropTarget drop = host.CurrentDropTarget!;
                Assert.Equal(PanelIds.Node, drop.TargetPanelId);
                Assert.Equal(edge, drop.Edge);
                Assert.Equal(edge, drop.VisualEdge);
                Assert.True(host.DropPreview.IsVisible);
                Assert.True(host.DragLabel.IsVisible);
                // The highlight is the target's half on that edge.
                Rect half = edge switch
                {
                    DockEdge.Left => new Rect(node.X, node.Y, node.Width / 2, node.Height),
                    DockEdge.Right => new Rect(node.X + node.Width / 2, node.Y, node.Width / 2, node.Height),
                    DockEdge.Top => new Rect(node.X, node.Y, node.Width, node.Height / 2),
                    _ => new Rect(node.X, node.Y + node.Height / 2, node.Width, node.Height / 2),
                };
                AssertNear(half, host.DropPreview.Bounds); // layout rounding: ±1 px
                Assert.Equal(EditorStrings.T(WorkspaceDockEdges.DropLabelKey(edge)), host.DropPreviewLabel.Text);
                Assert.Same(before, owner.State); // nothing changes before the drop

                Release(window, target);
                Assert.False(host.IsDragging);
                Assert.False(host.DropPreview.IsVisible);
                Assert.False(host.DragLabel.IsVisible);
                Assert.Equal(1, owner.DockRequests);
                Assert.Equal(before.Layout.DockView(PanelIds.Edit, PanelIds.Node, edge), owner.State.Layout);

                // Drawn result: Sahne Düzenle sits on that side of the Node graph.
                Rect n = host.GetPanelRect(PanelIds.Node);
                Rect e = host.GetPanelRect(PanelIds.Edit);
                const double gap = WorkspaceHost.SplitterThickness;
                switch (edge)
                {
                    case DockEdge.Left: Assert.Equal(e.Right + gap, n.X, 1); break;
                    case DockEdge.Right: Assert.Equal(n.Right + gap, e.X, 1); break;
                    case DockEdge.Top: Assert.Equal(e.Bottom + gap, n.Y, 1); break;
                    default: Assert.Equal(n.Bottom + gap, e.Y, 1); break;
                }
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void HeaderPress_BelowThreshold_IsAClick_NotADrag()
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState());
            try
            {
                EditorWorkspaceState before = owner.State;
                Point start = HeaderGrip(host, PanelIds.Edit, window);
                Press(window, start);
                MoveTo(window, start + new Point(2, 2)); // |dx|+|dy| = 4 = threshold → still a click
                Assert.False(host.IsDragging);
                Assert.False(host.DropPreview.IsVisible);
                Release(window, start + new Point(2, 2));
                Assert.Same(before, owner.State);
                Assert.Equal(0, owner.DockRequests);
                // The click focused the header (keyboard path continues from here).
                Assert.Same(host.FindHeader(PanelIds.Edit), window.FocusManager!.GetFocusedElement());

                // One pixel more starts the drag (the boundary is exclusive).
                Press(window, start);
                MoveTo(window, start + new Point(3, 2));
                Assert.True(host.IsDragging);
                Release(window, start + new Point(3, 2)); // over itself: no drop
                Assert.Same(before, owner.State);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void Escape_CancelsTheDrag_TreeUnchanged()
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState());
            try
            {
                EditorWorkspaceState before = owner.State;
                Point target = InWindow(host, NearEdge(host.GetPanelRect(PanelIds.Node), DockEdge.Top), window);
                DragTo(window, HeaderGrip(host, PanelIds.Edit, window), target);
                Assert.True(host.DropPreview.IsVisible);

                window.KeyPressQwerty(PhysicalKey.Escape, RawInputModifiers.None);
                window.KeyReleaseQwerty(PhysicalKey.Escape, RawInputModifiers.None);
                Assert.False(host.IsDragging);
                Assert.False(host.DropPreview.IsVisible);
                Release(window, target);
                Assert.Same(before, owner.State);
                Assert.Equal(0, owner.DockRequests);

                // The next gesture works normally.
                DragTo(window, HeaderGrip(host, PanelIds.Edit, window), target);
                Release(window, target);
                Assert.Equal(before.Layout.DockView(PanelIds.Edit, PanelIds.Node, DockEdge.Top), owner.State.Layout);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SelfDrop_OutsideDrop_AndSplitterGap_ChangeNothing()
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState());
            try
            {
                EditorWorkspaceState before = owner.State;
                Rect edit = host.GetPanelRect(PanelIds.Edit);
                Rect node = host.GetPanelRect(PanelIds.Node);
                Point start = HeaderGrip(host, PanelIds.Edit, window);

                // Over itself.
                Point self = InWindow(host, NearEdge(edit, DockEdge.Bottom), window);
                DragTo(window, start, self);
                Assert.True(host.IsDragging);
                Assert.Null(host.CurrentDropTarget);
                Assert.False(host.DropPreview.IsVisible);
                Release(window, self);

                // Into the splitter gap between Node and Sahne Düzenle.
                Point gap = InWindow(host, new Point(node.Right + WorkspaceHost.SplitterThickness / 2, node.Center.Y), window);
                DragTo(window, start, gap);
                Assert.Null(host.CurrentDropTarget);
                Release(window, gap);

                // Outside the workspace (pointer is captured, the drop is refused).
                Point outside = new(-40, 300);
                DragTo(window, start, InWindow(host, NearEdge(node, DockEdge.Left), window));
                Assert.NotNull(host.CurrentDropTarget);
                MoveTo(window, outside);
                Assert.Null(host.CurrentDropTarget);
                Assert.False(host.DropPreview.IsVisible);
                Release(window, outside);

                Assert.Same(before, owner.State);
                Assert.Equal(0, owner.DockRequests);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void PinnedPanel_CannotBeMoved_AndShowsIt()
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState());
            try
            {
                WorkspacePanel edit = host.FindPanel(PanelIds.Edit)!;
                PanelHeaderActions actions = edit.TemplateHeaderActions!;
                Assert.True(actions.IsVisible);
                Assert.False(edit.IsPinned);

                Click(window, actions.PinButton); // real click on the header's pin button
                Assert.True(owner.State.IsPinned(PanelIds.Edit));
                Assert.True(edit.IsPinned);
                Assert.Contains(WorkspacePanel.PinnedPseudoClass, edit.Classes);
                Assert.True(actions.PinButton.IsChecked);
                Assert.Contains(edit.GetVisualDescendants().OfType<Border>(), b => b.Classes.Contains("workspacePinnedOutline") && b.IsVisible);

                EditorWorkspaceState pinned = owner.State;
                Point target = InWindow(host, NearEdge(host.GetPanelRect(PanelIds.Node), DockEdge.Left), window);
                DragTo(window, HeaderGrip(host, PanelIds.Edit, window), target);
                Assert.False(host.IsDragging);
                Assert.False(host.DropPreview.IsVisible);
                Release(window, target);
                Assert.Same(pinned, owner.State);

                // The menu's Move entry is locked too.
                ContextMenu menu = host.OpenPanelMenu(PanelIds.Edit)!;
                Assert.False(MenuEntry(menu, "move").IsEnabled);
                Assert.True(MenuEntry(menu, "pin").IsChecked);
                menu.Close();

                // A pinned panel is still a drop target for others.
                Point onEdit = InWindow(host, NearEdge(host.GetPanelRect(PanelIds.Edit), DockEdge.Top), window);
                DragTo(window, HeaderGrip(host, PanelIds.Node, window), onEdit);
                Release(window, onEdit);
                Assert.Equal(pinned.Layout.DockView(PanelIds.Node, PanelIds.Edit, DockEdge.Top), owner.State.Layout);

                Click(window, actions.PinButton); // unpin
                Assert.False(owner.State.IsPinned(PanelIds.Edit));
                Assert.False(actions.PinButton.IsChecked);
                Assert.DoesNotContain(WorkspacePanel.PinnedPseudoClass, edit.Classes);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void LastPanel_CannotBeClosed()
    {
        Ui(() =>
        {
            var single = new EditorWorkspaceState(WorkspaceLayout.Initial(), new BottomStripState(BottomTabs.None, 0));
            var (window, host, owner) = StandaloneHost(single);
            try
            {
                WorkspacePanel node = host.FindPanel(PanelIds.Node)!;
                Assert.False(node.CanClose);
                Button close = node.TemplateHeaderActions!.CloseButton;
                Assert.False(close.IsEnabled);
                Click(window, close);
                node.RequestClose();
                Assert.Equal(0, owner.CloseRequests);
                Assert.Same(single, owner.State);
                ContextMenu menu = host.OpenPanelMenu(PanelIds.Node)!;
                Assert.False(MenuEntry(menu, "close").IsEnabled);
                Assert.False(MenuEntry(menu, "move").IsEnabled); // nowhere to move
                menu.Close();
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void KeyboardAndMenuPath_ProducesTheSameTreeAsDragging()
    {
        Ui(() =>
        {
            foreach (DockEdge edge in Enum.GetValues<DockEdge>())
            {
                // Drag result.
                var (dragWindow, dragHost, dragOwner) = StandaloneHost(SplitState());
                WorkspaceLayout dragged;
                try
                {
                    Point target = InWindow(dragHost, NearEdge(dragHost.GetPanelRect(PanelIds.Node), edge), dragWindow);
                    DragTo(dragWindow, HeaderGrip(dragHost, PanelIds.Edit, dragWindow), target);
                    Release(dragWindow, target);
                    dragged = dragOwner.State.Layout;
                }
                finally
                {
                    dragWindow.Close();
                }

                // Keyboard: focus the header, open the menu (alternating keys), Move → edge → Node.
                var (window, host, owner) = StandaloneHost(SplitState());
                try
                {
                    Control header = host.FindHeader(PanelIds.Edit)!;
                    Assert.True(header.Focusable);
                    Assert.True(header.Focus(NavigationMethod.Tab));
                    PhysicalKey key = edge switch
                    {
                        DockEdge.Left => PhysicalKey.Enter,
                        DockEdge.Right => PhysicalKey.Space,
                        DockEdge.Top => PhysicalKey.F10,
                        _ => PhysicalKey.ContextMenu,
                    };
                    RawInputModifiers mods = key == PhysicalKey.F10 ? RawInputModifiers.Shift : RawInputModifiers.None;
                    if (key == PhysicalKey.ContextMenu)
                    {
                        window.KeyPress(Key.Apps, mods, key, null);
                        window.KeyRelease(Key.Apps, mods, key, null);
                    }
                    else
                    {
                        window.KeyPressQwerty(key, mods);
                        window.KeyReleaseQwerty(key, mods);
                    }
                    ContextMenu menu = host.PanelMenu ?? throw new Exception($"menu not opened by {key}");
                    Assert.Equal(PanelIds.Edit, menu.Tag);
                    MenuMove(menu, edge, PanelIds.Node);
                    menu.Close();
                    window.UpdateLayout();
                    Assert.Equal(dragged, owner.State.Layout);
                }
                finally
                {
                    window.Close();
                }
            }
        });
    }

    [Fact]
    public void Menu_PinAndClose_WorkLikeTheButtons()
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState());
            try
            {
                Invoke(MenuEntry(host.OpenPanelMenu(PanelIds.Edit)!, "pin"));
                Assert.True(owner.State.IsPinned(PanelIds.Edit));
                host.PanelMenu?.Close();
                Invoke(MenuEntry(host.OpenPanelMenu(PanelIds.Edit)!, "pin"));
                Assert.False(owner.State.IsPinned(PanelIds.Edit));
                host.PanelMenu?.Close();

                // Right click on a header opens the same menu.
                Point grip = HeaderGrip(host, PanelIds.Node, window);
                window.MouseDown(grip, MouseButton.Right);
                window.MouseUp(grip, MouseButton.Right);
                Assert.Equal(PanelIds.Node, host.PanelMenu?.Tag);
                host.PanelMenu!.Close();

                Invoke(MenuEntry(host.OpenPanelMenu(PanelIds.Edit)!, "close"));
                window.UpdateLayout();
                Assert.DoesNotContain(PanelIds.Edit, owner.State.Layout.Views);
                Assert.False(host.FindPanel(PanelIds.Edit)!.IsVisible);
            }
            finally
            {
                window.Close();
            }
        });
    }

    // ---- right-to-left ----

    [Fact]
    public void RightToLeft_DropFollowsTheVisualEdge_AndMenuUsesVisualNames()
    {
        Ui(() =>
        {
            var (window, host, owner) = StandaloneHost(SplitState(), rightToLeft: true);
            try
            {
                EditorWorkspaceState before = owner.State;
                WorkspacePanel nodePanel = host.FindPanel(PanelIds.Node)!;
                Rect node = Drawn(nodePanel, window);
                // Visual LEFT edge of the Node graph as drawn in the mirrored window.
                Point visualLeft = new(node.X + node.Width * 0.08, node.Center.Y);

                DragTo(window, HeaderGrip(host, PanelIds.Edit, window), visualLeft);
                WorkspaceDropTarget drop = host.CurrentDropTarget!;
                Assert.Equal(DockEdge.Left, drop.VisualEdge);
                Assert.Equal(DockEdge.Right, drop.Edge); // logical end = visual left in RTL
                Assert.Equal(EditorStrings.T(WorkspaceDockEdges.DropLabelKey(DockEdge.Left)), host.DropPreviewLabel.Text);
                Rect preview = Drawn(host.DropPreview, window);
                AssertNear(new Rect(node.X, node.Y, node.Width / 2, node.Height), preview); // visual left half
                Release(window, visualLeft);

                WorkspaceLayout expected = before.Layout.DockView(PanelIds.Edit, PanelIds.Node, DockEdge.Right);
                Assert.Equal(expected, owner.State.Layout);
                Rect drawnEdit = Drawn(host.FindPanel(PanelIds.Edit)!, window);
                Rect drawnNode = Drawn(nodePanel, window);
                Assert.True(drawnEdit.Right <= drawnNode.X + 0.5, $"RTL: edit {drawnEdit} must be drawn left of node {drawnNode}");

                // The menu's visual "left" gives the same tree.
                owner.Set(before);
                window.UpdateLayout();
                MenuMove(host.OpenPanelMenu(PanelIds.Edit)!, DockEdge.Left, PanelIds.Node);
                host.PanelMenu?.Close();
                Assert.Equal(expected, owner.State.Layout);
            }
            finally
            {
                window.Close();
            }
        });
    }

    // ---- MainWindow: panels with their own header, close + reopen ----

    [Fact]
    public void MainWindow_OwnHeaders_DragFromFreeArea_InnerControlsKeepWorking()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            try
            {
                WorkspaceHost host = HostOf(window);
                // One visible pin/close pair per visible panel (Hierarchy / Inspector carry theirs inside).
                foreach (string id in _vm.WorkspaceLayout.Views)
                {
                    WorkspacePanel panel = host.FindPanel(id)!;
                    int shown = panel.GetVisualDescendants().OfType<PanelHeaderActions>().Count(a => a.IsEffectivelyVisible && ReferenceEquals(a.Panel, panel));
                    Assert.True(shown == 1, $"{id}: {shown} visible header action groups");
                }

                // Bottom strip: pressing a tab and dragging does not move the panel; the tab gets selected.
                EditorWorkspaceState before = _vm.Workspace;
                TabControl tabs = Assert.IsType<TabControl>(host.FindHeader(PanelIds.Bottom));
                TabItem logTab = tabs.GetLogicalChildren().OfType<TabItem>().ElementAt(BottomStripState.LogTab);
                Point onTab = InWindow(logTab, new Rect(logTab.Bounds.Size).Center, window);
                Point overNode = InWindow(host, NearEdge(host.GetPanelRect(PanelIds.Node), DockEdge.Top), window);
                DragTo(window, onTab, overNode);
                Assert.False(host.IsDragging);
                Release(window, overNode);
                Assert.Equal(before.Layout, _vm.Workspace.Layout);
                Assert.Equal(BottomStripState.LogTab, _vm.BottomPanelActiveTab);

                // Bottom strip: the empty part of the tab row is a drag handle.
                ItemsPresenter strip = tabs.GetVisualDescendants().OfType<ItemsPresenter>().First(p => ReferenceEquals(p.TemplatedParent, tabs));
                Point emptyRow = InWindow(tabs, new Point(tabs.Bounds.Width * 0.6, strip.Bounds.Center.Y), window);
                Assert.DoesNotContain(window.InputHitTest(emptyRow) is Visual v ? v.GetSelfAndVisualAncestors() : Array.Empty<Visual>(), a => a is TabItem or Button);
                WorkspaceLayout layout = _vm.Workspace.Layout;
                DragTo(window, emptyRow, overNode);
                Assert.True(host.IsDragging);
                Release(window, overNode);
                Assert.Equal(layout.DockView(PanelIds.Bottom, PanelIds.Node, DockEdge.Top), _vm.Workspace.Layout);

                // Hierarchy: the pin button inside its own header is a button, not a handle.
                WorkspacePanel hierarchyPanel = host.FindPanel(PanelIds.Hierarchy)!;
                PanelHeaderActions inner = window.GetVisualDescendants().OfType<NodeHierarchyView>().Single()
                    .GetVisualDescendants().OfType<PanelHeaderActions>().Single();
                Assert.Same(hierarchyPanel, inner.Panel);
                layout = _vm.Workspace.Layout;
                Point onPin = InWindow(inner.PinButton, new Rect(inner.PinButton.Bounds.Size).Center, window);
                DragTo(window, onPin, overNode);
                Assert.False(host.IsDragging);
                Release(window, overNode);
                Assert.Equal(layout, _vm.Workspace.Layout);
                Click(window, inner.PinButton);
                Assert.True(_vm.Workspace.IsPinned(PanelIds.Hierarchy));
                Assert.True(hierarchyPanel.IsPinned);
                Click(window, inner.PinButton);
                Assert.False(_vm.Workspace.IsPinned(PanelIds.Hierarchy));

                // Hierarchy: the free area of its header drags the panel; header is keyboard-focusable.
                Border header = Assert.IsType<Border>(host.FindHeader(PanelIds.Hierarchy));
                Assert.True(header.Focusable);
                TextBlock title = header.GetVisualDescendants().OfType<TextBlock>().First(t => t.Classes.Contains("panelTitle"));
                Point free = InWindow(title, new Rect(title.Bounds.Size).Center, window); // header text, not a control
                Assert.DoesNotContain(window.InputHitTest(free) is Visual hit ? hit.GetSelfAndVisualAncestors() : Array.Empty<Visual>(), a => a is Button);
                Point inspectorBottom = InWindow(host, NearEdge(host.GetPanelRect(PanelIds.Inspector), DockEdge.Bottom), window);
                DragTo(window, free, inspectorBottom);
                Assert.True(host.IsDragging);
                Assert.Equal(PanelIds.Inspector, host.CurrentDropTarget!.TargetPanelId);
                Release(window, inspectorBottom);
                Assert.Equal(layout.DockView(PanelIds.Hierarchy, PanelIds.Inspector, DockEdge.Bottom), _vm.Workspace.Layout);
                Assert.True(_vm.IsHierarchyPanelVisible); // still open, just moved

                // Right click on the Inspector's own header opens the panel menu.
                Border inspectorHeader = Assert.IsType<Border>(host.FindHeader(PanelIds.Inspector));
                Point inspectorFree = InWindow(inspectorHeader, new Point(20, inspectorHeader.Bounds.Height / 2), window);
                window.MouseDown(inspectorFree, MouseButton.Right);
                window.MouseUp(inspectorFree, MouseButton.Right);
                Assert.Equal(PanelIds.Inspector, host.PanelMenu?.Tag);
                host.PanelMenu!.Close();
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void MainWindow_Close_RemovesFromTree_ReopenKeepsTheControlInstance()
    {
        Ui(() =>
        {
            var window = OpenMainWindow(_vm);
            try
            {
                WorkspaceHost host = HostOf(window);
                NodeHierarchyView hierarchy = window.GetLogicalDescendants().OfType<NodeHierarchyView>().Single();
                NodeGraphView graph = window.GetLogicalDescendants().OfType<NodeGraphView>().Single();
                TabControl tabs = Assert.IsType<TabControl>(host.FindHeader(PanelIds.Bottom));

                // Hierarchy: pin, then close with its own × → leaves the tree (and its pin).
                _vm.SetWorkspacePanelPinned(PanelIds.Hierarchy, true);
                window.UpdateLayout();
                PanelHeaderActions inner = hierarchy.GetVisualDescendants().OfType<PanelHeaderActions>().Single();
                Assert.True(inner.CloseButton.IsEnabled);
                Click(window, inner.CloseButton);
                Assert.False(_vm.IsHierarchyPanelVisible);
                Assert.DoesNotContain(PanelIds.Hierarchy, _vm.WorkspaceLayout.Views);
                Assert.False(_vm.Workspace.IsPinned(PanelIds.Hierarchy));
                Assert.False(hierarchy.IsEffectivelyVisible);
                _vm.ShowPanel("Hierarchy"); // existing menu / Ctrl+1 action
                window.UpdateLayout();
                Assert.True(hierarchy.IsEffectivelyVisible);
                Assert.Same(hierarchy, window.GetLogicalDescendants().OfType<NodeHierarchyView>().Single());

                // Node graph: host header ×.
                WorkspacePanel nodePanel = host.FindPanel(PanelIds.Node)!;
                Click(window, nodePanel.TemplateHeaderActions!.CloseButton);
                Assert.DoesNotContain(PanelIds.Node, _vm.WorkspaceLayout.Views);
                Assert.False(_vm.IsNodeGraphActive);
                _vm.ShowPanel("NodeGraph");
                window.UpdateLayout();
                Assert.True(graph.IsEffectivelyVisible);
                Assert.Same(graph, window.GetLogicalDescendants().OfType<NodeGraphView>().Single());

                // Bottom strip: its × floats over the tab row; closing closes every tab.
                PanelHeaderActions bottomActions = host.FindPanel(PanelIds.Bottom)!.TemplateHeaderActions!;
                Assert.True(bottomActions.IsEffectivelyVisible);
                Click(window, bottomActions.CloseButton);
                Assert.False(_vm.IsBottomPanelVisible);
                Assert.False(_vm.IsAssetsPanelVisible || _vm.IsLogPanelVisible);
                _vm.ShowPanel("Log");
                window.UpdateLayout();
                Assert.True(_vm.IsBottomPanelVisible);
                Assert.Same(tabs, host.FindHeader(PanelIds.Bottom));
                Assert.True(tabs.IsEffectivelyVisible);
                Assert.True(_vm.WorkspaceLayout.IsValid);
            }
            finally
            {
                window.Close();
            }
        });
    }
}
