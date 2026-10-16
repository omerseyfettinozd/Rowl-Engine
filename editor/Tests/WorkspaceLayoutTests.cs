using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 D1 — locks the pure workspace layout model (split tree, operations,
/// invariants, JSON). Scenarios 1:1 from the prototype's
/// <c>layout-state.test.mjs</c>, plus randomized invariants and strict JSON loading.
/// </summary>
public sealed class WorkspaceLayoutTests
{
    // ---- helpers (JS: check / three / checkNonoverlap) ----

    private static void Check(WorkspaceLayout layout, WorkspaceViewState state)
    {
        IReadOnlyList<string> views = layout.Views;
        Assert.Equal(state.Visible.OrderBy(v => v, StringComparer.Ordinal), views.OrderBy(v => v, StringComparer.Ordinal));
        Assert.Equal(views.Count, views.Distinct(StringComparer.Ordinal).Count());
        Assert.Null(layout.Validate());
        VisitRatios(layout.Tree);
        CheckNonOverlap(layout.Tree);
        CheckJsonRoundTrip(layout);
    }

    private static void VisitRatios(LayoutNode? tree)
    {
        if (tree is not SplitNode split)
            return;
        Assert.NotNull(split.A);
        Assert.NotNull(split.B);
        Assert.InRange(split.Ratio, WorkspaceLayout.MinRatio, WorkspaceLayout.MaxRatio);
        VisitRatios(split.A);
        VisitRatios(split.B);
    }

    private static void CheckJsonRoundTrip(WorkspaceLayout layout)
    {
        string json = WorkspaceLayoutJson.Serialize(layout);
        WorkspaceLayoutLoadResult loaded = WorkspaceLayoutJson.Parse(json);
        Assert.True(loaded.IsValid, loaded.Error);
        Assert.Equal(layout, loaded.Layout);
        Assert.Equal(json, WorkspaceLayoutJson.Serialize(loaded.Layout!));
    }

    private static (WorkspaceLayout Layout, WorkspaceViewState State) Three()
    {
        WorkspaceViewState state = WorkspaceViewState.Initial().OpenView(PanelIds.Game).OpenView(PanelIds.Edit);
        return (WorkspaceLayout.Initial().Sync(WorkspaceViewState.Initial(), state), state);
    }

    private readonly record struct Box(double X, double Y, double W, double H);

    /// <summary>Projects the tree onto a unit workspace and checks the areas are disjoint and cover it.</summary>
    private static void CheckNonOverlap(LayoutNode? tree)
    {
        var unit = new Box(0, 0, 1, 1);
        var boxes = new List<Box>();
        Areas(tree, unit, boxes);
        for (int i = 0; i < boxes.Count; i++)
        {
            Box a = boxes[i];
            Assert.True(a.W > 0 && a.H > 0);
            for (int j = i + 1; j < boxes.Count; j++)
            {
                Box b = boxes[j];
                double width = Math.Min(a.X + a.W, b.X + b.W) - Math.Max(a.X, b.X);
                double height = Math.Min(a.Y + a.H, b.Y + b.H) - Math.Max(a.Y, b.Y);
                Assert.True(width <= 1e-10 || height <= 1e-10, "Panel areas overlap");
            }
        }
        if (tree is not null)
            Assert.True(Math.Abs(boxes.Sum(b => b.W * b.H) - 1) < 1e-10);
    }

    private static void Areas(LayoutNode? node, Box box, List<Box> into)
    {
        switch (node)
        {
            case LeafNode:
                into.Add(box);
                break;
            case SplitNode split:
                bool vertical = split.Axis == SplitAxis.Vertical;
                Box first = vertical ? box with { H = box.H * split.Ratio } : box with { W = box.W * split.Ratio };
                Box second = vertical
                    ? box with { Y = box.Y + first.H, H = box.H - first.H }
                    : box with { X = box.X + first.W, W = box.W - first.W };
                Areas(split.A, first, into);
                Areas(split.B, second, into);
                break;
        }
    }

    private static SplitNode Root(WorkspaceLayout layout) => Assert.IsType<SplitNode>(layout.Tree);

    private static LeafNode Leaf(LayoutNode node) => Assert.IsType<LeafNode>(node);

    private static readonly DockEdge[] Edges = { DockEdge.Left, DockEdge.Right, DockEdge.Top, DockEdge.Bottom };

    // ---- JS scenarios ----

    [Fact]
    public void NodeBesideGameAndEdit_SiblingMoveSimplifiesTree()
    {
        var (layout, state) = Three();
        SplitNode root = Root(layout);
        Assert.Equal(SplitAxis.Horizontal, root.Axis);
        Assert.Equal(PanelIds.Node, Leaf(root.A).View);
        Assert.Equal(SplitAxis.Vertical, Assert.IsType<SplitNode>(root.B).Axis);

        layout = layout.DockView(PanelIds.Edit, PanelIds.Game, DockEdge.Top);
        Assert.Equal(new[] { PanelIds.Edit, PanelIds.Game }, WorkspaceLayout.Leaves(Root(layout).B));
        Assert.Equal(SplitAxis.Vertical, Assert.IsType<SplitNode>(Root(layout).B).Axis);

        layout = layout.DockView(PanelIds.Node, PanelIds.Edit, DockEdge.Right);
        Check(layout, state);
        SplitNode a = Assert.IsType<SplitNode>(Root(layout).A);
        Assert.Equal(SplitAxis.Horizontal, a.Axis);
        Assert.Equal(new[] { PanelIds.Edit, PanelIds.Node }, WorkspaceLayout.Leaves(a));
    }

    [Fact]
    public void ToolWindowsGatherInOwnColumn_PinnedSourceCannotMove()
    {
        var (layout, state) = Three();
        foreach (string view in new[] { PanelIds.Hierarchy, PanelIds.Inspector, PanelIds.Assets, PanelIds.Console })
        {
            WorkspaceViewState next = state.OpenView(view);
            layout = layout.Sync(state, next);
            state = next;
            Check(layout, state);
        }
        Assert.Equal(0.74, Root(layout).Ratio);
        Assert.Equal(new[] { PanelIds.Node, PanelIds.Game, PanelIds.Edit }, WorkspaceLayout.Leaves(Root(layout).A));
        Assert.Equal(
            new[] { PanelIds.Hierarchy, PanelIds.Inspector, PanelIds.Assets, PanelIds.Console },
            WorkspaceLayout.Leaves(Root(layout).B));

        state = state.TogglePin(PanelIds.Hierarchy);
        Assert.Same(layout, layout.DockView(PanelIds.Hierarchy, PanelIds.Node, DockEdge.Top, state.Pinned.ToArray()));
    }

    [Theory]
    [InlineData(DockEdge.Left)]
    [InlineData(DockEdge.Right)]
    [InlineData(DockEdge.Top)]
    [InlineData(DockEdge.Bottom)]
    public void WindowsDockBySplittingArea_InAllFourDirections_NeverCopied(DockEdge edge)
    {
        var (layout, state) = Three();
        layout = layout.DockView(PanelIds.Edit, PanelIds.Node, edge);
        Check(layout, state);
        SplitNode a = Assert.IsType<SplitNode>(Root(layout).A);
        Assert.Equal(edge is DockEdge.Left or DockEdge.Right ? SplitAxis.Horizontal : SplitAxis.Vertical, a.Axis);
        // JS: 'floating' in layout === false. The model has no floating windows at all.
        Assert.Null(typeof(WorkspaceLayout).GetProperty("Floating"));
    }

    [Fact]
    public void NestedSplitsAtExtremeSizes_NeverOverlapPanels()
    {
        var (layout, state) = Three();
        foreach (DockEdge edge in Edges)
        {
            layout = layout.DockView(PanelIds.Node, PanelIds.Edit, edge);
            foreach (double ratio in new[] { WorkspaceLayout.MinRatio, WorkspaceLayout.MaxRatio, 0.2, 0.8, 0.35, 0.65 })
            {
                layout = layout.ResizeSplit(Root(layout).Id, ratio);
                Check(layout, state);
            }
        }
    }

    [Fact]
    public void MobileStartWithThreeViews_SharesAreaEqually()
    {
        WorkspaceViewState state = WorkspaceViewState.Initial().OpenView(PanelIds.Game).OpenView(PanelIds.Edit);
        WorkspaceLayout layout = WorkspaceLayout.Initial().Sync(WorkspaceViewState.Initial(), state, SplitAxis.Vertical);
        Assert.Equal(SplitAxis.Vertical, Root(layout).Axis);
        Assert.Equal(1d / 3d, Root(layout).Ratio);
        Assert.Equal(SplitAxis.Vertical, Assert.IsType<SplitNode>(Root(layout).B).Axis);
        Check(layout, state);
    }

    [Fact]
    public void InvalidAndSelfDrop_KeepCurrentLayout()
    {
        var (layout, _) = Three();
        Assert.Same(layout, layout.DockView(PanelIds.Game, PanelIds.Game, DockEdge.Top));
        Assert.Same(layout, layout.DockView(PanelIds.Game, "missing", DockEdge.Left));
        Assert.Same(layout, layout.DockView(PanelIds.Game, PanelIds.Node, (DockEdge)99));
    }

    [Fact]
    public void ManualClose_RemovesBranch_KeepsOtherPinnedWindow()
    {
        var (layout, state) = Three();
        state = state.TogglePin(PanelIds.Game);
        WorkspaceViewState next = state.CloseView(PanelIds.Edit);
        layout = layout.Sync(state, next);
        Check(layout, next);
        Assert.Contains(PanelIds.Game, layout.Views);
    }

    [Fact]
    public void Resize_KeepsAccessibleBounds()
    {
        var (layout, _) = Three();
        string id = Root(layout).Id;
        // D3: the prototype's 0.2 / 0.8 bound became 0.08 / 0.92 so a 200 px Hierarchy
        // column fits a 1440 px window; reachability is kept by the view's pixel
        // minimums (WorkspacePanel.PanelMinWidth / PanelMinHeight, WorkspaceHostTests).
        Assert.Equal(WorkspaceLayout.MinRatio, Root(layout.ResizeSplit(id, 0)).Ratio);
        Assert.Equal(WorkspaceLayout.MaxRatio, Root(layout.ResizeSplit(id, 1)).Ratio);
        Assert.Equal(0.08, WorkspaceLayout.MinRatio);
        Assert.Equal(0.92, WorkspaceLayout.MaxRatio);
        Assert.Equal(0.5, Root(layout.ResizeSplit(id, 0.5)).Ratio);
        CheckNonOverlap(layout.ResizeSplit(id, WorkspaceLayout.MaxRatio).Tree);
    }

    [Fact]
    public void ToolStack_KeepsPrototypeCap_AfterResizeBoundsLoosened()
    {
        // D3 loosened MinRatio/MaxRatio for resizing only; Sync's tool stacking keeps
        // the prototype's Math.min(.8, n / (n + 1)) (sixth tool: 5/6 → 0.8).
        WorkspaceViewState state = WorkspaceViewState.Initial();
        WorkspaceLayout layout = WorkspaceLayout.Initial();
        foreach (string tool in new[] { PanelIds.Hierarchy, PanelIds.Inspector, PanelIds.Assets, PanelIds.Console, PanelIds.Library, PanelIds.Store })
        {
            WorkspaceViewState next = state.OpenView(tool);
            layout = layout.Sync(state, next);
            state = next;
        }
        Check(layout, state);
        var ratios = new List<double>();
        void Collect(LayoutNode? node)
        {
            if (node is not SplitNode split)
                return;
            ratios.Add(split.Ratio);
            Collect(split.A);
            Collect(split.B);
        }
        Collect(layout.Tree);
        Assert.All(ratios, r => Assert.True(r <= WorkspaceLayout.ToolStackMaxRatio));
        Assert.Contains(WorkspaceLayout.ToolStackMaxRatio, ratios);
    }

    [Fact]
    public void ThousandMixedOperations_EachViewInSingleArea_NoOverlap()
    {
        var (layout, state) = Three();
        uint seed = 7;
        int Random(int n)
        {
            seed = unchecked(seed * 1664525u + 1013904223u);
            return (int)(seed % (uint)n);
        }

        for (int i = 0; i < 1000; i++)
        {
            string view = PanelIds.All[Random(PanelIds.All.Length)];
            string target = PanelIds.All[Random(PanelIds.All.Length)];
            int action = Random(5);
            if (action == 0)
                layout = layout.DockView(view, target, Edges[Random(4)], state.Pinned.ToArray());
            else if (action == 1 && layout.Tree is SplitNode root)
                layout = layout.ResizeSplit(root.Id, Random(101) / 100d);
            else
            {
                WorkspaceViewState next = action == 2 ? state.OpenView(view)
                    : action == 3 ? state.CloseView(view)
                    : state.TogglePin(view);
                layout = layout.Sync(state, next);
                state = next;
            }
            Check(layout, state);
        }
    }

    // ---- additions: randomized invariants ----

    [Fact]
    public void RandomOperations_KeepInvariants_AndJsonRoundTripsExactly()
    {
        var random = new Random(20261016);
        var (layout, state) = Three();
        var gone = new HashSet<string>(StringComparer.Ordinal);

        for (int i = 0; i < 1000; i++)
        {
            string view = PanelIds.All[random.Next(PanelIds.All.Length)];
            string target = PanelIds.All[random.Next(PanelIds.All.Length)];
            IReadOnlyList<string> before = layout.Views.OrderBy(v => v, StringComparer.Ordinal).ToArray();
            switch (random.Next(6))
            {
                case 0:
                    layout = layout.DockView(view, target, Edges[random.Next(4)], state.Pinned.ToArray());
                    Assert.Equal(before, layout.Views.OrderBy(v => v, StringComparer.Ordinal));
                    break;
                case 1:
                    List<string> splitIds = CollectSplitIds(layout.Tree);
                    string id = splitIds.Count > 0 && random.Next(8) != 0 ? splitIds[random.Next(splitIds.Count)] : "split-unknown";
                    layout = layout.ResizeSplit(id, random.NextDouble() * 3 - 1);
                    Assert.Equal(before, layout.Views.OrderBy(v => v, StringComparer.Ordinal));
                    break;
                case 2:
                    WorkspaceViewState opened = state.OpenView(view);
                    layout = layout.Sync(state, opened, random.Next(2) == 0 ? SplitAxis.Horizontal : SplitAxis.Vertical);
                    state = opened;
                    gone.Remove(view);
                    break;
                case 3:
                    WorkspaceViewState closed = state.CloseView(view);
                    layout = layout.Sync(state, closed);
                    state = closed;
                    gone.Add(view);
                    break;
                case 4:
                    state = state.TogglePin(view);
                    break;
                default:
                    layout = layout.RemoveView(view);
                    state = state.CloseView(view);
                    break;
            }
            Check(layout, state);
            Assert.DoesNotContain(layout.Views, v => gone.Contains(v) && !state.IsVisible(v));
            AssertAllRatiosOpenInterval(layout.Tree);
        }
    }

    private static List<string> CollectSplitIds(LayoutNode? node)
    {
        var ids = new List<string>();
        void Walk(LayoutNode? n)
        {
            if (n is SplitNode s)
            {
                ids.Add(s.Id);
                Walk(s.A);
                Walk(s.B);
            }
        }
        Walk(node);
        return ids;
    }

    private static void AssertAllRatiosOpenInterval(LayoutNode? node)
    {
        if (node is not SplitNode s)
            return;
        Assert.True(s.Ratio > 0 && s.Ratio < 1);
        AssertAllRatiosOpenInterval(s.A);
        AssertAllRatiosOpenInterval(s.B);
    }

    [Fact]
    public void ResizeSplit_UnknownIdOrNaN_ReturnsSameLayout()
    {
        var (layout, _) = Three();
        Assert.Same(layout, layout.ResizeSplit("split-404", 0.5));
        Assert.Same(layout, layout.ResizeSplit(Root(layout).Id, double.NaN));
    }

    [Fact]
    public void PanelIdsAreComparedOrdinally_TurkishCasingDoesNotMatch()
    {
        var (layout, _) = Three();
        Assert.Same(layout, layout.DockView("NODE", PanelIds.Game, DockEdge.Top));
        Assert.Same(layout, layout.DockView("edİt", PanelIds.Game, DockEdge.Top));
        Assert.False(PanelIds.IsKnown("INSPECTOR"));
        Assert.True(PanelIds.IsKnown("inspector"));
    }

    [Fact]
    public void Operations_DoNotMutateTheOriginalLayout()
    {
        var (layout, _) = Three();
        string before = WorkspaceLayoutJson.Serialize(layout);
        _ = layout.DockView(PanelIds.Edit, PanelIds.Node, DockEdge.Left);
        _ = layout.ResizeSplit(Root(layout).Id, 0.3);
        _ = layout.RemoveView(PanelIds.Game);
        Assert.Equal(before, WorkspaceLayoutJson.Serialize(layout));
    }

    [Fact]
    public void EmptyLayout_AcceptsFirstPanelAgain()
    {
        WorkspaceViewState state = WorkspaceViewState.Initial();
        WorkspaceLayout layout = WorkspaceLayout.Initial();
        WorkspaceViewState empty = state.CloseView(PanelIds.Node);
        layout = layout.Sync(state, empty);
        Assert.Null(layout.Tree);
        Check(layout, empty);
        WorkspaceViewState reopened = empty.OpenView(PanelIds.Lua);
        layout = layout.Sync(empty, reopened);
        Assert.Equal(new[] { PanelIds.Lua }, layout.Views);
        Check(layout, reopened);
    }

    [Fact]
    public void ViewState_CloseMovesFocusToMostRecentVisible_AndDropsPin()
    {
        WorkspaceViewState state = WorkspaceViewState.Initial().OpenView(PanelIds.Game).OpenView(PanelIds.Edit).TogglePin(PanelIds.Edit);
        WorkspaceViewState closed = state.CloseView(PanelIds.Edit);
        Assert.Equal(PanelIds.Game, closed.Focused);
        Assert.Empty(closed.Pinned);
        Assert.Same(closed, closed.CloseView(PanelIds.Edit));
        Assert.Same(state, state.OpenView("nonsense"));
        WorkspaceViewState none = closed.CloseView(PanelIds.Game).CloseView(PanelIds.Node);
        Assert.Null(none.Focused);
        Assert.Empty(none.Visible);
    }

    // ---- additions: JSON ----

    [Fact]
    public void Json_SavesSmallDeterministicTextWithVersionAndNoClassNames()
    {
        var (layout, _) = Three();
        string json = WorkspaceLayoutJson.Serialize(layout);
        Assert.Equal(
            "{\"version\":1,\"lastSplitId\":2,\"tree\":{\"type\":\"split\",\"id\":\"split-1\",\"axis\":\"x\",\"ratio\":0.5,"
            + "\"a\":{\"type\":\"leaf\",\"view\":\"node\"},"
            + "\"b\":{\"type\":\"split\",\"id\":\"split-2\",\"axis\":\"y\",\"ratio\":0.5,"
            + "\"a\":{\"type\":\"leaf\",\"view\":\"game\"},\"b\":{\"type\":\"leaf\",\"view\":\"edit\"}}}}",
            json);
        Assert.DoesNotContain("$type", json);
        Assert.DoesNotContain("Node", json);
    }

    [Fact]
    public void Json_RoundTrip_ReproducesEqualTreeAndIdenticalBytes()
    {
        var (layout, state) = Three();
        foreach (string view in new[] { PanelIds.Hierarchy, PanelIds.Lua, PanelIds.Library })
        {
            WorkspaceViewState next = state.OpenView(view);
            layout = layout.Sync(state, next);
            state = next;
        }
        layout = layout.ResizeSplit(Root(layout).Id, 1d / 3d);

        string first = WorkspaceLayoutJson.Serialize(layout);
        WorkspaceLayoutLoadResult loaded = WorkspaceLayoutJson.Parse(first);
        Assert.True(loaded.IsValid, loaded.Error);
        Assert.Equal(layout, loaded.Layout);
        string second = WorkspaceLayoutJson.Serialize(loaded.Layout!);
        Assert.Equal(first, second);
        Assert.Equal(Encoding.UTF8.GetBytes(first), Encoding.UTF8.GetBytes(second));
    }

    [Theory]
    [InlineData("tr-TR")]
    [InlineData("de-DE")]
    [InlineData("ar-SA")]
    public void Json_IsCultureIndependent(string culture)
    {
        var (layout, _) = Three();
        layout = layout.ResizeSplit(Root(layout).Id, 0.4);
        string expected = WorkspaceLayoutJson.Serialize(layout);

        CultureInfo previous = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = new CultureInfo(culture);
            Assert.Equal(expected, WorkspaceLayoutJson.Serialize(layout));
            Assert.Equal(layout, WorkspaceLayoutJson.Parse(expected).Layout);
            Assert.Contains("\"ratio\":0.4,", expected);
            Assert.DoesNotContain(",4", expected);
        }
        finally
        {
            CultureInfo.CurrentCulture = previous;
        }
    }

    [Fact]
    public void Json_EmptyTree_IsValidAndRoundTrips()
    {
        var empty = new WorkspaceLayout(null, 5);
        string json = WorkspaceLayoutJson.Serialize(empty);
        Assert.Equal("{\"version\":1,\"lastSplitId\":5,\"tree\":null}", json);
        Assert.Equal(empty, WorkspaceLayoutJson.Parse(json).Layout);
    }

    private const string Leaf1 = "{\"type\":\"leaf\",\"view\":\"node\"}";
    private const string Leaf2 = "{\"type\":\"leaf\",\"view\":\"game\"}";

    private static string Split(string id, string axis, string ratio, string a, string b) =>
        "{\"type\":\"split\",\"id\":\"" + id + "\",\"axis\":\"" + axis + "\",\"ratio\":" + ratio + ",\"a\":" + a + ",\"b\":" + b + "}";

    private static string Doc(string tree, string version = "1", string last = "1") =>
        "{\"version\":" + version + ",\"lastSplitId\":" + last + ",\"tree\":" + tree + "}";

    public static TheoryData<string?> BrokenDocuments() => new()
    {
        null,
        "",
        "   ",
        "{",
        "[]",
        "null",
        "42",
        "\"layout\"",
        "{}",
        "not json at all",
        Doc(Leaf1) + " trailing",
        Doc(Leaf1, version: "2"),
        Doc(Leaf1, version: "0"),
        Doc(Leaf1, version: "\"1\""),
        Doc(Leaf1, version: "1.5"),
        "{\"lastSplitId\":1,\"tree\":" + Leaf1 + "}",
        "{\"version\":1,\"tree\":" + Leaf1 + "}",
        "{\"version\":1,\"lastSplitId\":1}",
        Doc(Leaf1, last: "-1"),
        Doc(Leaf1, last: "\"x\""),
        "{\"version\":1,\"version\":1,\"lastSplitId\":1,\"tree\":" + Leaf1 + "}",
        "{\"version\":1,\"lastSplitId\":1,\"tree\":" + Leaf1 + ",\"extra\":1}",
        Doc("{\"type\":\"leaf\"}"),
        Doc("{\"type\":\"leaf\",\"view\":\"nonexistent\"}"),
        Doc("{\"type\":\"leaf\",\"view\":\"NODE\"}"),
        Doc("{\"type\":\"leaf\",\"view\":7}"),
        Doc("{\"type\":\"leaf\",\"view\":\"node\",\"ratio\":0.5}"),
        Doc("{\"type\":\"leaf\",\"view\":\"node\",\"view\":\"node\"}"),
        Doc("{\"type\":\"tab\",\"view\":\"node\"}"),
        Doc("{\"view\":\"node\"}"),
        Doc("\"node\""),
        Doc("[]"),
        Doc(Split("split-1", "z", "0.5", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "0", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "1", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "1.5", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "-0.2", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "\"0.5\"", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "1e999", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "0.5", Leaf1, "null")),
        Doc(Split("split-1", "x", "0.5", Leaf1, "{}")),
        Doc(Split("", "x", "0.5", Leaf1, Leaf2)),
        Doc(Split("split-1", "x", "0.5", Leaf1, Leaf1)),
        Doc(Split("split-1", "x", "0.5", Leaf1, Split("split-1", "y", "0.5", Leaf2, "{\"type\":\"leaf\",\"view\":\"edit\"}")), last: "2"),
        Doc(Split("split-9", "x", "0.5", Leaf1, Leaf2), last: "1"),
        "{\"version\":1,\"lastSplitId\":1,\"tree\":{\"type\":\"split\",\"id\":\"split-1\",\"axis\":\"x\",\"ratio\":0.5,\"a\":" + Leaf1 + "}}",
        "{\"version\":1,\"lastSplitId\":1,\"tree\":{\"type\":\"split\",\"id\":\"split-1\",\"axis\":\"x\",\"ratio\":0.5,\"a\":" + Leaf1 + ",\"b\":" + Leaf2 + ",\"view\":\"x\"}}",
        Doc(DeepChain(200), last: "200"),
    };

    private static string DeepChain(int depth)
    {
        string node = Leaf1;
        for (int i = 0; i < depth; i++)
            node = Split("split-" + (i + 1).ToString(CultureInfo.InvariantCulture), "x", "0.5", node, Leaf2);
        return node;
    }

    [Theory]
    [MemberData(nameof(BrokenDocuments))]
    public void Json_BrokenOrIncomplete_ReturnsInvalid_WithoutThrowing(string? json)
    {
        WorkspaceLayoutLoadResult result = WorkspaceLayoutJson.Parse(json);
        Assert.False(result.IsValid);
        Assert.Null(result.Layout);
        Assert.False(string.IsNullOrEmpty(result.Error));
    }

    [Fact]
    public void Json_AcceptsHandWrittenValidDocument()
    {
        string json = Doc(Split("split-1", "y", "0.25", Leaf1, Leaf2));
        WorkspaceLayoutLoadResult result = WorkspaceLayoutJson.Parse(json);
        Assert.True(result.IsValid, result.Error);
        Assert.Equal(new[] { PanelIds.Node, PanelIds.Game }, result.Layout!.Views);
        Assert.Equal(0.25, Root(result.Layout).Ratio);
    }

    [Fact]
    public void Validate_ReportsHandBuiltInvalidLayouts()
    {
        var node = new LeafNode(PanelIds.Node);
        Assert.NotNull(new WorkspaceLayout(new SplitNode("split-1", SplitAxis.Horizontal, 0.5, node, node), 1).Validate());
        Assert.NotNull(new WorkspaceLayout(new SplitNode("split-1", SplitAxis.Horizontal, 0, node, new LeafNode(PanelIds.Game)), 1).Validate());
        Assert.NotNull(new WorkspaceLayout(new SplitNode("split-1", SplitAxis.Horizontal, double.NaN, node, new LeafNode(PanelIds.Game)), 1).Validate());
        Assert.NotNull(new WorkspaceLayout(new LeafNode("bogus"), 0).Validate());
        Assert.Null(new WorkspaceLayout(null, 0).Validate());
        Assert.Null(WorkspaceLayout.Initial().Validate());
    }
}
