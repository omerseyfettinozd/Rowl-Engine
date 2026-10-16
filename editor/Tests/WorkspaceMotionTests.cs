using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.Views.Workspace;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 D6 — workspace motion: the transitions are the prototype's (200 ms layout, 180 ms
/// highlight, <c>cubic-bezier(.22,1,.36,1)</c>), reduce-motion makes them 0 ms, a splitter
/// drag / keyboard move never animates, and the keyboard steps follow the arrow on screen.
/// Deterministic: the headless clock does not tick by itself, so a started motion stays at
/// progress 0 until <see cref="WorkspaceHost.CompleteMotion"/> (or an explicit render tick).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class WorkspaceMotionTests
{
    public WorkspaceMotionTests() => RowlEyeHeadlessRuntime.EnsureInitialized();

    private static void Ui(Action action) => Dispatcher.UIThread.Invoke(action);

    private static WorkspaceLayout WithoutHierarchy => EditorWorkspaceState.Default.WithPanel(PanelIds.Hierarchy, false).Layout;

    private static WorkspaceLayout Full => EditorWorkspaceState.Default.Layout;

    /// <summary>Hierarchy wide enough to move in both directions (the default width sits at its 200 px minimum).</summary>
    private static WorkspaceLayout Wide => Full.ResizeSplit(((SplitNode)Full.Tree!).Id, 0.3);

    /// <summary>
    /// Same transitions, one hour long: the headless clock advances with real time whenever the
    /// dispatcher runs, so a test that reads a motion's start (progress 0) parks it like this.
    /// </summary>
    private static void ParkClock(WorkspaceHost host)
    {
        host.Transitions = new Transitions
        {
            new DoubleTransition { Property = WorkspaceHost.LayoutProgressProperty, Duration = TimeSpan.FromHours(1), Easing = WorkspaceHost.MotionEasing },
            new DoubleTransition { Property = WorkspaceHost.DropProgressProperty, Duration = TimeSpan.FromHours(1), Easing = WorkspaceHost.MotionEasing },
        };
    }

    /// <summary>Standalone host (placeholder panels); like the real owner it writes splitter moves back to the tree.</summary>
    private static (Window Window, WorkspaceHost Host) Standalone(WorkspaceLayout layout, bool reduceMotion = false, bool parkClock = false)
    {
        var host = new WorkspaceHost { ReduceMotion = reduceMotion };
        if (parkClock)
            ParkClock(host);
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
                Content = new TextBox { Text = id },
            });
        }
        host.Layout = layout;
        host.SplitResized += (_, e) => host.Layout = host.Layout!.ResizeSplit(e.SplitId, e.Ratio);
        var window = new Window { Width = 1440, Height = 820, Content = host };
        window.Show();
        window.UpdateLayout();
        return (window, host);
    }

    private static void AssertNear(Rect expected, Rect actual, double tolerance = 0.5) =>
        Assert.True(Math.Abs(expected.X - actual.X) <= tolerance && Math.Abs(expected.Y - actual.Y) <= tolerance
            && Math.Abs(expected.Width - actual.Width) <= tolerance && Math.Abs(expected.Height - actual.Height) <= tolerance,
            $"expected {expected} ± {tolerance}, got {actual}");

    private static void AssertNear(double expected, double actual, double tolerance, string what) =>
        Assert.True(Math.Abs(expected - actual) <= tolerance, $"{what}: expected {expected} ± {tolerance}, got {actual}");

    private static IEnumerable<string> SplitIds(LayoutNode? node) => node switch
    {
        SplitNode s => new[] { s.Id }.Concat(SplitIds(s.A)).Concat(SplitIds(s.B)),
        _ => [],
    };

    private static string RootSplitId(WorkspaceHost host) => ((SplitNode)host.Layout!.Tree!).Id;

    private static void Key(Window window, PhysicalKey key, RawInputModifiers modifiers = RawInputModifiers.None)
    {
        window.KeyPressQwerty(key, modifiers);
        window.KeyReleaseQwerty(key, modifiers);
        window.UpdateLayout();
    }

    private static Rect Drawn(Visual visual, Window window) =>
        new Rect(visual.Bounds.Size).TransformToAABB(visual.TransformToVisual(window)!.Value);

    // ---- the definition ----

    [Fact]
    public void Transitions_AreThePrototypes_200msLayout_180msHighlight_SameEasing()
    {
        Ui(() =>
        {
            Assert.Equal(TimeSpan.FromMilliseconds(200), WorkspaceHost.LayoutMotionDuration); // workspace-motion.js animate() default
            Assert.Equal(TimeSpan.FromMilliseconds(180), WorkspaceHost.DropMotionDuration); // workspace-motion.js exit()
            var easing = Assert.IsType<SplineEasing>(WorkspaceHost.MotionEasing); // cubic-bezier(.22,1,.36,1)
            Assert.Equal(0.22, easing.X1);
            Assert.Equal(1.0, easing.Y1);
            Assert.Equal(0.36, easing.X2);
            Assert.Equal(1.0, easing.Y2);
            Assert.Equal(6, WorkspaceHost.EnterRise);

            var host = new WorkspaceHost();
            Assert.False(host.ReduceMotion); // default
            Transitions transitions = Assert.IsType<Transitions>(host.Transitions);
            Assert.Equal(2, transitions.Count);

            DoubleTransition layout = transitions.OfType<DoubleTransition>().Single(t => t.Property == WorkspaceHost.LayoutProgressProperty);
            Assert.Equal(TimeSpan.FromMilliseconds(200), layout.Duration);
            Assert.Same(WorkspaceHost.MotionEasing, layout.Easing);

            DoubleTransition drop = transitions.OfType<DoubleTransition>().Single(t => t.Property == WorkspaceHost.DropProgressProperty);
            Assert.Equal(TimeSpan.FromMilliseconds(180), drop.Duration);
            Assert.Same(WorkspaceHost.MotionEasing, drop.Easing);
        });
    }

    [Fact]
    public void ReduceMotion_RemovesEveryTransition_AndCanBeToggled()
    {
        Ui(() =>
        {
            var host = new WorkspaceHost { ReduceMotion = true };
            Assert.True(host.Transitions is null || host.Transitions.Count == 0); // 0 ms everywhere
            host.ReduceMotion = false;
            Assert.Equal(2, host.Transitions!.Count);
            host.ReduceMotion = true;
            Assert.True(host.Transitions is null || host.Transitions.Count == 0);
        });
    }

    [Fact]
    public void ReduceMotion_FollowsSettingsProfile_AndUpdatesLiveHosts()
    {
        Ui(() =>
        {
            string tempDir = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "RowlMotionProfile_" + Guid.NewGuid().ToString("N"));
            System.IO.Directory.CreateDirectory(tempDir);
            string settingsPath = System.IO.Path.Combine(tempDir, "editor-settings.json");
            string playerPath = System.IO.Path.Combine(tempDir, "player-settings.json");
            var host = new WorkspaceHost();
            using var engineHost = new RowlEngine.Editor.Native.EngineHost();
            try
            {
                Assert.False(host.EffectiveReduceMotion);
                Assert.Equal(2, host.Transitions!.Count);

                var settings = new RowlEngine.Editor.ViewModels.SettingsViewModel { ReduceMotion = true };
                Assert.True(WorkspaceHost.ProfilePrefersReducedMotion);
                Assert.True(host.ReduceMotion);
                Assert.True(host.EffectiveReduceMotion);
                Assert.True(host.Transitions is null || host.Transitions.Count == 0);

                RowlEngine.Editor.Services.EditorSettingsSyncService.HandleSettingsPropertyChanged(
                    nameof(RowlEngine.Editor.ViewModels.SettingsViewModel.ReduceMotion),
                    settings,
                    settingsPath,
                    playerPath,
                    tempDir,
                    engineHost,
                    () => new RowlEngine.Editor.Services.ProjectRuntimeSettings(),
                    _ => { },
                    () => { });

                Assert.True(RowlEngine.Editor.Services.EditorSettingsProfile.Load(settingsPath).ReduceMotion);

                settings.ResetDefaults();
                Assert.False(settings.ReduceMotion);
                Assert.False(WorkspaceHost.ProfilePrefersReducedMotion);
                Assert.False(host.EffectiveReduceMotion);
                Assert.Equal(2, host.Transitions!.Count);

                RowlEngine.Editor.Services.EditorSettingsSyncService.LoadEditorSettings(settingsPath, settings);
                Assert.True(settings.ReduceMotion);
                Assert.True(WorkspaceHost.ProfilePrefersReducedMotion);
                Assert.True(host.EffectiveReduceMotion);
                Assert.True(host.Transitions is null || host.Transitions.Count == 0);
            }
            finally
            {
                WorkspaceHost.ProfilePrefersReducedMotion = false;
                try { System.IO.Directory.Delete(tempDir, recursive: true); } catch (Exception) { }
            }
        });
    }

    // ---- layout changes ----

    [Fact]
    public void ReduceMotion_LayoutChangeIsAtOnce()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full, reduceMotion: true);
            try
            {
                host.Layout = WithoutHierarchy;
                window.UpdateLayout();
                Assert.False(host.IsMotionRunning);
                Assert.Equal(1.0, host.LayoutProgress);
                AssertNear(host.GetPanelRect(PanelIds.Node), host.GetDisplayedPanelRect(PanelIds.Node));
                Assert.True(host.GetPanelRect(PanelIds.Node).X < 1); // already moved to the far edge

                host.Layout = Full; // reopen: the panel is simply there, fully opaque
                window.UpdateLayout();
                WorkspacePanel hierarchy = host.FindPanel(PanelIds.Hierarchy)!;
                Assert.True(hierarchy.IsVisible);
                Assert.Equal(1.0, hierarchy.Opacity);
                Assert.Null(hierarchy.RenderTransform);
                AssertNear(host.GetPanelRect(PanelIds.Hierarchy), host.GetDisplayedPanelRect(PanelIds.Hierarchy));
                Assert.False(host.IsMotionRunning);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void LayoutChange_StartsFromThePreviousRects_AndCompleteMotionSettles()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full, parkClock: true);
            try
            {
                Rect nodeBefore = host.GetPanelRect(PanelIds.Node);
                host.Layout = WithoutHierarchy;
                window.UpdateLayout();

                Assert.True(host.IsMotionRunning);
                Assert.Equal(0.0, host.LayoutProgress);
                Rect nodeAfter = host.GetPanelRect(PanelIds.Node); // the final slot (hit tests, the owner) …
                Assert.True(nodeAfter.Width > nodeBefore.Width + 100);
                AssertNear(nodeBefore, host.GetDisplayedPanelRect(PanelIds.Node)); // … while it is still drawn where it was
                Assert.False(host.FindPanel(PanelIds.Hierarchy)!.IsVisible);

                // The splitters slide with their neighbours.
                TranslateTransform[] slides = SplitIds(host.Layout!.Tree)
                    .Select(id => host.FindSplitter(id)!.RenderTransform)
                    .OfType<TranslateTransform>().ToArray();
                Assert.Contains(slides, s => Math.Abs(s.X) > 1);

                host.CompleteMotion();
                window.UpdateLayout();
                Assert.False(host.IsMotionRunning);
                Assert.Equal(1.0, host.LayoutProgress);
                foreach (string id in WorkspaceLayout.Leaves(host.Layout!.Tree))
                {
                    AssertNear(host.GetPanelRect(id), host.GetDisplayedPanelRect(id));
                    Assert.Equal(id, host.FindPanel(id)!.PanelId);
                    Assert.Equal(1.0, host.FindPanel(id)!.Opacity);
                    Assert.Null(host.FindPanel(id)!.RenderTransform);
                }
                foreach (string id in SplitIds(host.Layout!.Tree))
                    Assert.Null(host.FindSplitter(id)!.RenderTransform);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void EnteringPanel_FadesInAndRises_NeighboursBlendHalfway()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(WithoutHierarchy, parkClock: true);
            try
            {
                Rect nodeBefore = host.GetPanelRect(PanelIds.Node);
                host.Layout = Full;
                window.UpdateLayout();
                Rect nodeAfter = host.GetPanelRect(PanelIds.Node);

                WorkspacePanel hierarchy = host.FindPanel(PanelIds.Hierarchy)!;
                Assert.Contains(PanelIds.Hierarchy, host.EnteringPanels);
                Assert.Equal(0.0, hierarchy.Opacity);
                Assert.Equal(WorkspaceHost.EnterRise, Assert.IsType<TranslateTransform>(hierarchy.RenderTransform).Y);
                Assert.True(hierarchy.ZIndex < host.FindPanel(PanelIds.Node)!.ZIndex, "entering panel is painted below its neighbours");

                // Halfway (the clock is parked so the value can be set directly): linear blend of the rectangles.
                host.Transitions = null;
                host.SetValue(WorkspaceHost.LayoutProgressProperty, 0.5);
                window.UpdateLayout();
                Assert.Equal(0.5, hierarchy.Opacity, 3);
                Assert.Equal(WorkspaceHost.EnterRise / 2, ((TranslateTransform)hierarchy.RenderTransform!).Y, 3);
                Rect half = new((nodeBefore.X + nodeAfter.X) / 2, (nodeBefore.Y + nodeAfter.Y) / 2,
                    (nodeBefore.Width + nodeAfter.Width) / 2, (nodeBefore.Height + nodeAfter.Height) / 2);
                AssertNear(half, host.GetDisplayedPanelRect(PanelIds.Node));
                AssertNear(nodeAfter, host.GetPanelRect(PanelIds.Node)); // the final slot never moves

                host.CompleteMotion();
                window.UpdateLayout();
                Assert.Equal(1.0, hierarchy.Opacity);
                Assert.Null(hierarchy.RenderTransform);
                AssertNear(nodeAfter, host.GetDisplayedPanelRect(PanelIds.Node));
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void RealTransition_RunsToItsEnd()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full);
            try
            {
                host.Layout = WithoutHierarchy;
                window.UpdateLayout();
                Assert.True(host.IsMotionRunning);
                // Drive the headless clock until the transition ends (bounded wait, no fixed sleep).
                bool ended = SpinWait.SpinUntil(() =>
                {
                    AvaloniaHeadlessPlatform.ForceRenderTimerTick(1);
                    Dispatcher.UIThread.RunJobs();
                    window.UpdateLayout();
                    return !host.IsMotionRunning;
                }, TimeSpan.FromSeconds(10));
                Assert.True(ended, "layout motion did not finish");
                Assert.Equal(1.0, host.LayoutProgress);
                AssertNear(host.GetPanelRect(PanelIds.Node), host.GetDisplayedPanelRect(PanelIds.Node));
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void PointerPress_SettlesARunningMotion_LikeThePrototypesCancel()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full, parkClock: true);
            try
            {
                host.Layout = WithoutHierarchy;
                window.UpdateLayout();
                Assert.True(host.IsMotionRunning);

                window.MouseDown(new Point(700, 400), MouseButton.Left);
                window.MouseUp(new Point(700, 400), MouseButton.Left);
                window.UpdateLayout();
                Assert.False(host.IsMotionRunning);
                AssertNear(host.GetPanelRect(PanelIds.Node), host.GetDisplayedPanelRect(PanelIds.Node));
            }
            finally
            {
                window.Close();
            }
        });
    }

    // ---- splitters ----

    [Fact]
    public void SplitterMouseDrag_NeverAnimates()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full);
            try
            {
                string id = RootSplitId(host);
                Point at = host.FindSplitter(id)!.TranslatePoint(new Point(3, 100), window)!.Value;
                double before = host.GetPanelRect(PanelIds.Hierarchy).Width;
                window.MouseDown(at, MouseButton.Left);
                window.MouseMove(at + new Point(30, 0));
                window.MouseMove(at + new Point(60, 0));
                Assert.False(host.IsMotionRunning); // while dragging
                window.MouseUp(at + new Point(60, 0), MouseButton.Left);
                window.UpdateLayout();

                Assert.False(host.IsMotionRunning); // and after the ratio went through the owner
                Assert.Equal(1.0, host.LayoutProgress);
                AssertNear(before + 60, host.GetPanelRect(PanelIds.Hierarchy).Width, 2, "dragged width");
                AssertNear(host.GetPanelRect(PanelIds.Hierarchy), host.GetDisplayedPanelRect(PanelIds.Hierarchy));
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SplitterKeyboard_Steps16px_Shift64px_Instantly_AndWritesTheTree()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Wide);
            try
            {
                string id = RootSplitId(host);
                host.FindSplitter(id)!.Focus(NavigationMethod.Tab);
                double w0 = host.GetPanelRect(PanelIds.Hierarchy).Width;
                double r0 = ((SplitNode)host.Layout!.Tree!).Ratio;

                Key(window, PhysicalKey.ArrowRight);
                AssertNear(w0 + WorkspaceHost.KeyboardStep, host.GetPanelRect(PanelIds.Hierarchy).Width, 0.6, "Right: +16");
                Assert.Equal(16, WorkspaceHost.KeyboardStep);
                Assert.True(((SplitNode)host.Layout!.Tree!).Ratio > r0, "written to the tree");
                Assert.False(host.IsMotionRunning);

                Key(window, PhysicalKey.ArrowRight, RawInputModifiers.Shift);
                AssertNear(w0 + 16 + WorkspaceHost.KeyboardStepLarge, host.GetPanelRect(PanelIds.Hierarchy).Width, 1.2, "Shift+Right: +64");
                Assert.Equal(64, WorkspaceHost.KeyboardStepLarge);

                Key(window, PhysicalKey.ArrowLeft, RawInputModifiers.Shift);
                Key(window, PhysicalKey.ArrowLeft);
                AssertNear(w0, host.GetPanelRect(PanelIds.Hierarchy).Width, 1.8, "Left steps undo");

                // Up / Down do nothing on a column divider; Ctrl+arrow is left alone.
                double w1 = host.GetPanelRect(PanelIds.Hierarchy).Width;
                Key(window, PhysicalKey.ArrowDown);
                Key(window, PhysicalKey.ArrowUp, RawInputModifiers.Shift);
                Key(window, PhysicalKey.ArrowRight, RawInputModifiers.Control);
                AssertNear(w1, host.GetPanelRect(PanelIds.Hierarchy).Width, 0.5, "other keys");

                Assert.False(host.IsMotionRunning);
                Assert.Equal(1.0, host.LayoutProgress);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SplitterKeyboard_StopsAtThePanelMinimums_AndRowDividerUsesUpDown()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full);
            try
            {
                string id = RootSplitId(host);
                host.FindSplitter(id)!.Focus(NavigationMethod.Tab);
                for (int i = 0; i < 4; i++)
                    Key(window, PhysicalKey.ArrowLeft, RawInputModifiers.Shift); // far past the minimum
                AssertNear(WorkspacePanelMetrics.HierarchyMinWidth, host.GetPanelRect(PanelIds.Hierarchy).Width, 0.5, "Hierarchy minimum");

                // The divider between the Node graph and the bottom strip is a row divider.
                var bottomSplit = (SplitNode)((SplitNode)((SplitNode)host.Layout!.Tree!).B).A;
                Assert.Equal(SplitAxis.Vertical, bottomSplit.Axis);
                GridSplitter rows = host.FindSplitter(bottomSplit.Id)!;
                rows.Focus(NavigationMethod.Tab);
                double h0 = host.GetPanelRect(PanelIds.Bottom).Height;
                double w0 = host.GetPanelRect(PanelIds.Node).Width;
                Key(window, PhysicalKey.ArrowUp);
                AssertNear(h0 + 16, host.GetPanelRect(PanelIds.Bottom).Height, 0.6, "Up grows the strip (divider moves up)");
                Key(window, PhysicalKey.ArrowDown, RawInputModifiers.Shift); // 64 px, but the strip stops at its 140 px minimum
                Assert.True(host.GetPanelRect(PanelIds.Bottom).Height >= WorkspacePanelMetrics.BottomMinHeight - 0.5);
                AssertNear(WorkspacePanelMetrics.BottomMinHeight, host.GetPanelRect(PanelIds.Bottom).Height, 0.5, "Shift+Down clamps at the minimum");
                Key(window, PhysicalKey.ArrowLeft);
                AssertNear(w0, host.GetPanelRect(PanelIds.Node).Width, 0.5, "Left ignored on a row divider");
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SplitterKeyboard_RightToLeft_FollowsTheArrowOnScreen()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Wide);
            try
            {
                window.FlowDirection = FlowDirection.RightToLeft;
                window.UpdateLayout();
                string id = RootSplitId(host);
                GridSplitter splitter = host.FindSplitter(id)!;
                splitter.Focus(NavigationMethod.Tab);
                double x0 = Drawn(splitter, window).X;
                double w0 = host.GetPanelRect(PanelIds.Hierarchy).Width; // layout space: tree order, drawn on the right

                Key(window, PhysicalKey.ArrowRight);
                AssertNear(x0 + 16, Drawn(splitter, window).X, 0.6, "Right moves the divider right on screen");
                AssertNear(w0 - 16, host.GetPanelRect(PanelIds.Hierarchy).Width, 0.6, "the right-hand Hierarchy shrinks");

                Key(window, PhysicalKey.ArrowLeft, RawInputModifiers.Shift);
                AssertNear(x0 + 16 - 64, Drawn(splitter, window).X, 1.2, "Shift+Left moves it 64 px left");

                // LTR control: same arrows, same screen direction.
                window.FlowDirection = FlowDirection.LeftToRight;
                window.UpdateLayout();
                splitter.Focus(NavigationMethod.Tab);
                double x1 = Drawn(splitter, window).X;
                Key(window, PhysicalKey.ArrowRight);
                AssertNear(x1 + 16, Drawn(splitter, window).X, 0.6, "LTR Right moves the divider right");
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void SplitterFocusVisible_UsesTheAccentToken_HoverTheSubtleBorder()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full);
            try
            {
                GridSplitter splitter = host.FindSplitter(RootSplitId(host))!;
                Color idle = ((ISolidColorBrush)splitter.Background!).Color;
                Color accent = Token(window, "Accent");
                Color subtle = Token(window, "BorderSubtle");

                splitter.Focus(NavigationMethod.Tab);
                window.UpdateLayout();
                Assert.Contains(":focus-visible", splitter.Classes);
                Assert.Equal(accent, ((ISolidColorBrush)splitter.Background!).Color);
                Assert.NotEqual(idle, accent);

                window.FocusManager!.ClearFocus();
                window.UpdateLayout();
                Assert.Equal(idle, ((ISolidColorBrush)splitter.Background!).Color);

                Point over = splitter.TranslatePoint(new Point(3, 50), window)!.Value;
                window.MouseMove(over);
                window.UpdateLayout();
                Assert.Equal(subtle, ((ISolidColorBrush)splitter.Background!).Color);
            }
            finally
            {
                window.Close();
            }
        });
    }

    private static Color Token(Window window, string key)
    {
        Assert.True(window.TryFindResource(key, window.ActualThemeVariant, out object? value), key);
        return ((ISolidColorBrush)value!).Color;
    }

    // ---- drop highlight ----

    [Fact]
    public void DropHighlight_FadesIn_ThenEasesBetweenTargets()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full, parkClock: true);
            try
            {
                Rect hierarchy = host.GetPanelRect(PanelIds.Hierarchy);
                Control grip = host.FindHeader(PanelIds.Node)!;
                Point start = grip.TranslatePoint(new Point(30, grip.Bounds.Height / 2), window)!.Value;
                Point left = host.TranslatePoint(new Point(hierarchy.X + hierarchy.Width * 0.08, hierarchy.Center.Y), window)!.Value;
                Point top = host.TranslatePoint(new Point(hierarchy.Center.X, hierarchy.Y + hierarchy.Height * 0.08), window)!.Value;

                window.MouseDown(start, MouseButton.Left);
                window.MouseMove(start + new Point(10, 0));
                window.MouseMove(left);
                window.UpdateLayout();
                Assert.Equal(DockEdge.Left, host.CurrentDropTarget!.Edge);
                Rect leftHalf = host.CurrentDropTarget.PreviewRect;
                Assert.True(host.DropPreview.IsVisible);
                Assert.Equal(0.0, host.DropProgress);
                Assert.Equal(0.0, host.DropPreview.Opacity); // a new highlight starts transparent …
                AssertNear(leftHalf, host.DropPreview.Bounds, 1); // … in place

                host.CompleteMotion();
                window.UpdateLayout();
                Assert.Equal(1.0, host.DropPreview.Opacity);

                window.MouseMove(top); // another edge: the highlight travels there
                window.UpdateLayout();
                Assert.Equal(DockEdge.Top, host.CurrentDropTarget!.Edge);
                Assert.Equal(1.0, host.DropPreview.Opacity);
                AssertNear(leftHalf, host.DropPreview.Bounds, 1); // still on its way (progress 0)

                host.Transitions = null; // park the clock to read the middle
                host.SetValue(WorkspaceHost.DropProgressProperty, 0.5);
                window.UpdateLayout();
                Rect topHalf = host.CurrentDropTarget.PreviewRect;
                AssertNear(new Rect((leftHalf.X + topHalf.X) / 2, (leftHalf.Y + topHalf.Y) / 2,
                    (leftHalf.Width + topHalf.Width) / 2, (leftHalf.Height + topHalf.Height) / 2), host.DropPreview.Bounds, 1);

                host.CompleteMotion();
                window.UpdateLayout();
                AssertNear(topHalf, host.DropPreview.Bounds, 1);

                window.MouseUp(top, MouseButton.Left);
                window.UpdateLayout();
                Assert.False(host.DropPreview.IsVisible);
                Assert.Equal(1.0, host.DropPreview.Opacity);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void DropHighlight_ReduceMotion_AppearsAndMovesAtOnce()
    {
        Ui(() =>
        {
            var (window, host) = Standalone(Full, reduceMotion: true);
            try
            {
                Rect hierarchy = host.GetPanelRect(PanelIds.Hierarchy);
                Control grip = host.FindHeader(PanelIds.Node)!;
                Point start = grip.TranslatePoint(new Point(30, grip.Bounds.Height / 2), window)!.Value;
                Point left = host.TranslatePoint(new Point(hierarchy.X + hierarchy.Width * 0.08, hierarchy.Center.Y), window)!.Value;
                Point top = host.TranslatePoint(new Point(hierarchy.Center.X, hierarchy.Y + hierarchy.Height * 0.08), window)!.Value;

                window.MouseDown(start, MouseButton.Left);
                window.MouseMove(start + new Point(10, 0));
                window.MouseMove(left);
                window.UpdateLayout();
                Assert.Equal(1.0, host.DropPreview.Opacity);
                AssertNear(host.CurrentDropTarget!.PreviewRect, host.DropPreview.Bounds, 1);

                window.MouseMove(top);
                window.UpdateLayout();
                Assert.Equal(1.0, host.DropPreview.Opacity);
                AssertNear(host.CurrentDropTarget!.PreviewRect, host.DropPreview.Bounds, 1);
                window.MouseUp(top, MouseButton.Left);
            }
            finally
            {
                window.Close();
            }
        });
    }
}
