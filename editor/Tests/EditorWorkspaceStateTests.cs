using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 D2 — the split tree is the workspace's single source of truth:
/// presets and split screen produce trees (pure), panel actions are tree
/// operations, and the old flag behaviour is preserved (checked step by step
/// against a copy of the pre-D2 flag logic).
/// </summary>
public sealed class EditorWorkspaceStateTests
{
    private static readonly string[] PanelActions =
    {
        "Hierarchy", "Inspector", "Log", "Assets", "Backlog", "SaveSlots", "ProjectIssues",
        "NodeGraph", "Preview", "EnginePreview", "SplitScreen", "Unknown",
    };

    public static IEnumerable<object[]> PresetCases() => new[]
    {
        new object[] { "Varsayılan", new[] { "node", "bottom", "inspector", "hierarchy" }, BottomTabs.Assets | BottomTabs.Log },
        new object[] { "Sahne Odağı", new[] { "node", "bottom" }, BottomTabs.Assets },
        new object[] { "Oyun Testi", new[] { "game", "inspector" }, BottomTabs.None },
    };

    // ---- presets → trees ----

    [Theory]
    [MemberData(nameof(PresetCases))]
    public void Preset_ProducesValidTree_WithExpectedPanels_AndJsonRoundTrips(string preset, string[] panels, BottomTabs tabs)
    {
        EditorWorkspaceState state = EditorWorkspaceLayouts.Preset(preset)!;

        Assert.NotNull(state);
        Assert.Null(state.Layout.Validate());
        Assert.Equal(panels.OrderBy(p => p, StringComparer.Ordinal), state.Layout.Views.OrderBy(p => p, StringComparer.Ordinal));
        Assert.All(state.Layout.Views, v => Assert.Contains(v, PanelIds.Editor));
        Assert.Equal(tabs, state.Bottom.Open);
        Assert.Equal(0, state.Bottom.ActiveTab);
        Assert.Equal(state.Bottom.AnyOpen, state.IsBottomVisible);
        Assert.Equal(0, state.SplitScreenMode);
        AssertJsonRoundTrip(state.Layout);

        // Pure: a fresh, equal value every call.
        Assert.Equal(state, EditorWorkspaceLayouts.Preset(preset));
        Assert.NotSame(state.Layout.Tree, EditorWorkspaceLayouts.Preset(preset)!.Layout.Tree);
    }

    [Fact]
    public void DefaultPreset_MirrorsMainWindowGrid()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;

        // hierarchy | ((center / bottom) | inspector)
        Assert.Equal("x(hierarchy,x(y(node,bottom),inspector))", Shape(state.Layout.Tree));
        var root = Assert.IsType<SplitNode>(state.Layout.Tree);
        Assert.Equal(EditorWorkspaceLayouts.HierarchyRatio, root.Ratio);
        var rest = Assert.IsType<SplitNode>(root.B);
        Assert.Equal(EditorWorkspaceLayouts.InspectorSplitRatio, rest.Ratio);
        Assert.Equal(EditorWorkspaceLayouts.BottomSplitRatio, Assert.IsType<SplitNode>(rest.A).Ratio);
        Assert.Equal(
            "{\"version\":1,\"lastSplitId\":3,\"tree\":{\"type\":\"split\",\"id\":\"split-3\",\"axis\":\"x\",\"ratio\":0.14,"
            + "\"a\":{\"type\":\"leaf\",\"view\":\"hierarchy\"},\"b\":{\"type\":\"split\",\"id\":\"split-2\",\"axis\":\"x\",\"ratio\":0.772,"
            + "\"a\":{\"type\":\"split\",\"id\":\"split-1\",\"axis\":\"y\",\"ratio\":0.7,\"a\":{\"type\":\"leaf\",\"view\":\"node\"},"
            + "\"b\":{\"type\":\"leaf\",\"view\":\"bottom\"}},\"b\":{\"type\":\"leaf\",\"view\":\"inspector\"}}}}",
            WorkspaceLayoutJson.Serialize(state.Layout));
    }

    [Fact]
    public void Presets_ListedInMenuOrder_UnknownNameIsNoOp()
    {
        Assert.Equal(new[] { "Varsayılan", "Sahne Odağı", "Oyun Testi" }, EditorWorkspaceLayouts.PresetNames);
        Assert.Null(EditorWorkspaceLayouts.Preset("Uydurma Düzen"));
        Assert.Null(EditorWorkspaceLayouts.Preset(null));
        Assert.Null(EditorWorkspaceLayouts.Preset("varsayılan")); // ordinal
        EditorWorkspaceState state = EditorWorkspaceState.Default.HandlePanelAction("Hierarchy");
        Assert.Same(state, state.ApplyPreset("Bilinmeyen"));
    }

    // ---- split screen → trees ----

    [Fact]
    public void SplitScreen_CyclesRowsColumnsOff_AsTreeOfNodeAndSceneEdit()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;

        state = state.HandlePanelAction("SplitScreen");
        Assert.Equal(1, state.SplitScreenMode);
        Assert.Equal("x(hierarchy,x(y(y(node,edit),bottom),inspector))", Shape(state.Layout.Tree));
        Assert.True(state.IsNodeGraphActive);
        Assert.True(state.IsPreviewActive); // Faz 10: açık her merkez görünüm vurgulanır
        Assert.False(state.IsEnginePreviewActive);
        AssertJsonRoundTrip(state.Layout);

        state = state.HandlePanelAction("SplitScreen");
        Assert.Equal(2, state.SplitScreenMode);
        Assert.Equal("x(hierarchy,x(y(x(node,edit),bottom),inspector))", Shape(state.Layout.Tree));
        Assert.Null(state.Layout.Validate());

        state = state.HandlePanelAction("SplitScreen");
        Assert.Equal(0, state.SplitScreenMode);
        Assert.Equal(Shape(EditorWorkspaceState.Default.Layout.Tree), Shape(state.Layout.Tree));
        Assert.True(state.IsNodeGraphActive);

        // Faz 10: Sahne Düzenle düğmesi bağımsız aç-kapa — bölünmüş ekrandaki Edit'i kapatır, Node kalır.
        state = state.HandlePanelAction("SplitScreen").HandlePanelAction("Preview");
        Assert.Equal(0, state.SplitScreenMode);
        Assert.Equal(Shape(EditorWorkspaceState.Default.Layout.Tree), Shape(state.Layout.Tree));
        Assert.False(state.IsPreviewActive);
        Assert.True(state.IsNodeGraphActive);
    }

    [Fact]
    public void SplitScreen_WithoutSidePanelsOrStrip_StillSplitsCenterOnly()
    {
        EditorWorkspaceState state = EditorWorkspaceLayouts.Preset("Oyun Testi")!
            .HandlePanelAction("Inspector")
            .HandlePanelAction("SplitScreen");
        Assert.Equal("y(node,edit)", Shape(state.Layout.Tree));
        Assert.Equal(1, state.SplitScreenMode);
        state = state.HandlePanelAction("Log");
        Assert.Equal("y(y(node,edit),bottom)", Shape(state.Layout.Tree));
        Assert.Equal(1, state.SplitScreenMode);
    }

    // ---- panel actions are tree operations ----

    [Theory]
    [InlineData("Hierarchy", "hierarchy")]
    [InlineData("Inspector", "inspector")]
    public void SidePanelToggle_RemovesLeaf_AndRestoresCanonicalShape(string action, string panel)
    {
        EditorWorkspaceState start = EditorWorkspaceState.Default;
        EditorWorkspaceState closed = start.HandlePanelAction(action);
        Assert.DoesNotContain(panel, closed.Layout.Views);
        Assert.Null(closed.Layout.Validate());
        EditorWorkspaceState reopened = closed.HandlePanelAction(action);
        Assert.Contains(panel, reopened.Layout.Views);
        Assert.Equal(Shape(start.Layout.Tree), Shape(reopened.Layout.Tree));
        Assert.Null(reopened.Layout.Validate());
        Assert.True(reopened.Layout.LastSplitId > start.Layout.LastSplitId); // fresh split id, never reused
    }

    [Fact]
    public void BottomLeaf_FollowsOpenTabs()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default; // Assets + Log open, Assets selected

        state = state.HandlePanelAction("Assets"); // selected → closes, falls back to Log
        Assert.Contains(PanelIds.Bottom, state.Layout.Views);
        Assert.Equal(BottomStripState.LogTab, state.Bottom.ActiveTab);

        state = state.HandlePanelAction("Log"); // last tab → strip leaf leaves the tree
        Assert.DoesNotContain(PanelIds.Bottom, state.Layout.Views);
        Assert.Equal("x(hierarchy,x(node,inspector))", Shape(state.Layout.Tree));

        state = state.HandlePanelAction("ProjectIssues");
        Assert.Equal("x(hierarchy,x(y(node,bottom),inspector))", Shape(state.Layout.Tree));
        Assert.Equal(BottomStripState.IssuesTab, state.Bottom.ActiveTab);

        int refreshed = 0;
        state = state.HandlePanelAction("SaveSlots", () => refreshed++);
        Assert.Equal(1, refreshed);
        Assert.Equal(BottomStripState.SaveSlotsTab, state.Bottom.ActiveTab);
        state = state.HandlePanelAction("SaveSlots", () => refreshed++);
        Assert.Equal(1, refreshed); // closing does not refresh
    }

    [Fact]
    public void NoChange_ReturnsSameInstance()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        Assert.Same(state, state.WithPanel(PanelIds.Hierarchy, true));
        Assert.Same(state, state.WithBottomTab(BottomStripState.LogTab, true));
        Assert.Same(state, state.WithActiveBottomTab(0));
        Assert.Same(state, state.WithCenter(PanelIds.Node));
        Assert.Same(state, state.WithSplitScreen(0));
        Assert.Same(state, state.WithBottomTab(99, true));
        Assert.Throws<ArgumentException>(() => state.WithPanel(PanelIds.Bottom, false));
        Assert.Throws<ArgumentException>(() => state.WithCenter(PanelIds.Inspector));
    }

    // ---- behaviour preservation: tree-derived flags == pre-D2 flag logic ----

    [Fact]
    public void RandomActions_DerivedFlagsMatchPreD2Logic_AndTreeStaysValid()
    {
        var random = new Random(20261016);
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        var legacy = LegacyFlags.Default();
        AssertSame(legacy, state, "start");

        for (int i = 0; i < 3000; i++)
        {
            string step;
            if (random.Next(10) == 0)
            {
                string preset = random.Next(4) switch { 0 => "Varsayılan", 1 => "Sahne Odağı", 2 => "Oyun Testi", _ => "Bilinmeyen" };
                step = "preset " + preset;
                legacy = legacy.ApplyPreset(preset);
                EditorWorkspaceState next = state.ApplyPreset(preset);
                state = next.WithActiveBottomTab(BottomStripState.ClampTab(next.Bottom.ActiveTab));
            }
            else
            {
                string action = PanelActions[random.Next(PanelActions.Length)];
                step = action;
                legacy = legacy.HandlePanelAction(action, state.SplitScreenMode);
                EditorWorkspaceState next = state.HandlePanelAction(action);
                state = next.WithActiveBottomTab(BottomStripState.ClampTab(next.Bottom.ActiveTab));
            }

            string where = $"step {i} ({step})";
            AssertSame(legacy, state, where);
            Assert.True(state.Layout.Validate() is null, where + ": " + state.Layout.Validate());
            Assert.All(state.Layout.Views, v => Assert.Contains(v, PanelIds.Editor));
            Assert.Equal(state.Bottom.AnyOpen, state.IsBottomVisible);
            // Faz 10: merkez görünümler bağımsız; en az bir, en çok üç (Node / Game / Edit).
            int centers = state.Layout.Views.Count(EditorWorkspaceLayouts.IsCenter);
            Assert.InRange(centers, 1, 3);
            Assert.Equal((state.IsNodeGraphActive ? 1 : 0) + (state.IsPreviewActive ? 1 : 0) + (state.IsEnginePreviewActive ? 1 : 0), centers);
            if (i % 97 == 0)
                AssertJsonRoundTrip(state.Layout);
        }
    }

    [Fact]
    public void LegacyFlagAdapter_RoundTripsThroughTree()
    {
        bool h = true, a = false, ins = true, log = true, back = false, slots = false, issues = false;
        int tab = 1;
        bool graph = true, prev = false, eng = false;
        int split = 0;
        EditorWorkspaceLayoutService.HandlePanelAction("SplitScreen",
            ref h, ref a, ref ins, ref log, ref back, ref slots, ref issues,
            ref tab, ref graph, ref prev, ref eng, ref split);
        Assert.Equal(1, split);
        Assert.True(graph);
        Assert.True(log);
        Assert.Equal(1, tab);

        EditorWorkspaceState state = EditorWorkspaceLayoutService.FromFlags(h, a, ins, log, back, slots, issues, tab, graph, prev, eng, split);
        Assert.Equal("x(hierarchy,x(y(y(node,edit),bottom),inspector))", Shape(state.Layout.Tree));

        EditorWorkspaceLayoutService.ApplyLayoutPreset("Bilinmeyen",
            ref h, ref a, ref ins, ref log, ref back, ref slots, ref issues,
            ref tab, ref graph, ref prev, ref eng, ref split);
        Assert.Equal(1, split); // unknown preset: no-op
        EditorWorkspaceLayoutService.ApplyLayoutPreset("Oyun Testi",
            ref h, ref a, ref ins, ref log, ref back, ref slots, ref issues,
            ref tab, ref graph, ref prev, ref eng, ref split);
        Assert.True(eng && !graph && !prev && !h && ins && !log && !a && split == 0);
    }

    // ---- helpers ----

    private static void AssertSame(LegacyFlags expected, EditorWorkspaceState state, string where)
    {
        EditorWorkspaceLayoutService.ToFlags(state,
            out bool h, out bool a, out bool ins, out bool log, out bool back, out bool slots, out bool issues,
            out int tab, out bool graph, out bool prev, out bool eng, out int split);
        // Split < 0: merkez aç-kapa sonrası bölme kipi ağaç biçimine bağlı, referans onu izlemez.
        var actual = new LegacyFlags(h, a, ins, log, back, slots, issues, tab, graph, prev, eng, expected.Split < 0 ? -1 : split);
        Assert.True(expected == actual, $"{where}: expected {expected}, got {actual}");
    }

    private static void AssertJsonRoundTrip(WorkspaceLayout layout)
    {
        string json = WorkspaceLayoutJson.Serialize(layout);
        WorkspaceLayoutLoadResult loaded = WorkspaceLayoutJson.Parse(json);
        Assert.True(loaded.IsValid, loaded.Error);
        Assert.Equal(layout, loaded.Layout);
        Assert.Equal(json, WorkspaceLayoutJson.Serialize(loaded.Layout!));
    }

    /// <summary>Tree shape without ids / ratios: <c>x(a,b)</c> side by side, <c>y(a,b)</c> stacked.</summary>
    internal static string Shape(LayoutNode? node) => node switch
    {
        null => "-",
        LeafNode leaf => leaf.View,
        SplitNode split => $"{(split.Axis == SplitAxis.Horizontal ? "x" : "y")}({Shape(split.A)},{Shape(split.B)})",
        _ => "?",
    };

    /// <summary>
    /// Verbatim copy of the pre-D2 flag logic (EditorWorkspaceLayoutService
    /// HandlePanelAction / ApplyLayoutPreset / CycleSplitScreen at 9f01837 plus
    /// the view model's ClampBottomTab), used as the reference behaviour.
    /// </summary>
    private readonly record struct LegacyFlags(
        bool Hierarchy, bool Assets, bool Inspector, bool Log, bool Backlog, bool SaveSlots, bool Issues,
        int Tab, bool NodeGraph, bool Preview, bool EnginePreview, int Split)
    {
        public static LegacyFlags Default() => new(true, true, true, true, false, false, false, 0, true, false, false, 0);

        public LegacyFlags HandlePanelAction(string panelName, int actualSplit)
        {
            bool h = Hierarchy, a = Assets, ins = Inspector, log = Log, back = Backlog, slots = SaveSlots, issues = Issues;
            int tab = Tab, split = Split;
            bool graph = NodeGraph, prev = Preview, eng = EnginePreview;
            switch (panelName)
            {
                case "Hierarchy": h = !h; break;
                case "Assets": ToggleBottomTab(ref a, ref tab, 0); break;
                case "Inspector": ins = !ins; break;
                case "Log": ToggleBottomTab(ref log, ref tab, 1); break;
                case "Backlog": ToggleBottomTab(ref back, ref tab, 2); break;
                case "SaveSlots":
                    slots = !slots;
                    if (slots) tab = 3;
                    break;
                case "ProjectIssues":
                    issues = !issues;
                    if (issues) tab = 4;
                    break;
                // Faz 10: merkez görünümler bağımsız aç-kapa; son açık merkez kapanmaz.
                case "NodeGraph": ToggleCenter(ref graph, prev || eng); split = -1; break;
                case "Preview": ToggleCenter(ref prev, graph || eng); split = -1; break;
                case "EnginePreview": ToggleCenter(ref eng, graph || prev); split = -1; break;
                case "SplitScreen":
                    split = ((split < 0 ? actualSplit : split) + 1) % 3;
                    graph = true; eng = false; prev = split > 0; // bölünmüş ekran = Node + Sahne Düzenle
                    break;
            }
            bool[] open = { a, log, back, slots, issues };
            if (!(tab >= 0 && tab < 5 && open[tab]))
                tab = Array.IndexOf(open, true) is int first and >= 0 ? first : 0;
            tab = Math.Clamp(tab, 0, 4);
            return new(h, a, ins, log, back, slots, issues, tab, graph, prev, eng, split);
        }

        private static void ToggleCenter(ref bool open, bool anotherCenterOpen)
        {
            if (!open)
                open = true;
            else if (anotherCenterOpen)
                open = false;
        }

        public LegacyFlags ApplyPreset(string preset)
        {
            LegacyFlags next = preset switch
            {
                "Varsayılan" => new(true, true, true, true, false, false, false, 0, true, false, false, 0),
                "Sahne Odağı" => new(false, true, false, false, false, false, false, 0, true, false, false, 0),
                "Oyun Testi" => new(false, false, true, false, false, false, false, 0, false, false, true, 0),
                _ => this,
            };
            return next with { Tab = Math.Clamp(next.Tab, 0, 4) };
        }

        private static void ToggleBottomTab(ref bool visible, ref int activeTab, int target)
        {
            if (visible && activeTab == target)
                visible = false;
            else
            {
                visible = true;
                activeTab = target;
            }
        }
    }
}

/// <summary>
/// Faz 5 D2 — the view model's panel flags, widths and split-screen state are
/// derived from <see cref="MainWindowViewModel.Workspace"/>; setters and
/// commands only replace that one value.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorWorkspaceViewModelTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorWorkspaceViewModelTests()
    {
        // The view model creates Dispatcher timers: built on an xUnit worker thread before the headless
        // runtime exists, it would become Avalonia's UI thread and break later headless UI tests
        // (order dependent). Headless runtime first, view model on its UI thread (as WorkspaceHostTests).
        RowlEngine.Editor.Tools.RowlEye.RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlWorkspace_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("WorkspaceTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Avalonia.Threading.Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
        // KÖK GERİ VERİLMEZ: bkz. EditorAssetGridVirtualizationTests.
    }

    public void Dispose()
    {
        try { Avalonia.Threading.Dispatcher.UIThread.Invoke(() => _vm.Dispose()); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); }
        catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    private List<string> Record()
    {
        var names = new List<string>();
        _vm.PropertyChanged += (_, e) => names.Add(e.PropertyName ?? "");
        return names;
    }

    [Fact]
    public void StartsWithDefaultPresetTree()
    {
        Assert.Equal(EditorWorkspaceState.Default, _vm.Workspace);
        Assert.Same(_vm.Workspace.Layout, _vm.WorkspaceLayout);
        // D3: pixel widths are drawn by WorkspaceHost from the tree's ratios (WorkspaceHostTests).
        Assert.True(_vm.IsHierarchyPanelVisible && _vm.IsInspectorPanelVisible && _vm.IsBottomPanelVisible);
        Assert.True(_vm.IsNodeGraphActive && _vm.IsSplitScreenOff);
    }

    [Fact]
    public void Flags_AreReadFromTheTree()
    {
        List<string> changed = Record();
        _vm.Workspace = EditorWorkspaceLayouts.Preset("Oyun Testi")!;

        Assert.False(_vm.IsHierarchyPanelVisible);
        Assert.True(_vm.IsInspectorPanelVisible);
        Assert.False(_vm.IsBottomPanelVisible);
        Assert.True(_vm.IsEnginePreviewActive);
        Assert.False(_vm.IsNodeGraphActive);
        Assert.NotEqual("Transparent", _vm.GameTabBrush);
        Assert.Equal("Transparent", _vm.SceneTabBrush);

        Assert.Contains(nameof(MainWindowViewModel.Workspace), changed);
        Assert.Contains(nameof(MainWindowViewModel.WorkspaceLayout), changed);
        foreach (string name in new[]
        {
            "IsHierarchyPanelVisible",
            "IsAssetsPanelVisible", "IsLogPanelVisible", "IsBottomPanelVisible",
            "IsNodeGraphActive", "SceneTabBrush", "IsEnginePreviewActive", "GameTabBrush",
        })
        {
            Assert.Contains(name, changed);
        }
        // Unchanged values are not re-announced.
        foreach (string name in new[] { "IsInspectorPanelVisible", "SplitScreenMode", "IsPreviewActive", "BottomPanelActiveTab" })
            Assert.DoesNotContain(name, changed);
    }

    [Fact]
    public void Setters_AreTreeOperations_AndSameValueIsSilent()
    {
        _vm.IsLogPanelVisible = false;
        Assert.Contains(PanelIds.Bottom, _vm.WorkspaceLayout.Views); // Assets still open
        _vm.IsAssetsPanelVisible = false;
        Assert.DoesNotContain(PanelIds.Bottom, _vm.WorkspaceLayout.Views);
        Assert.False(_vm.IsBottomPanelVisible);

        _vm.IsHierarchyPanelVisible = false;
        Assert.DoesNotContain(PanelIds.Hierarchy, _vm.WorkspaceLayout.Views);
        _vm.IsProjectIssuesPanelVisible = true;
        _vm.BottomPanelActiveTab = 4;
        Assert.Contains(PanelIds.Bottom, _vm.WorkspaceLayout.Views);
        Assert.Equal(4, _vm.Workspace.Bottom.ActiveTab);
        Assert.Null(_vm.WorkspaceLayout.Validate());

        EditorWorkspaceState before = _vm.Workspace;
        List<string> changed = Record();
        _vm.IsProjectIssuesPanelVisible = true;
        _vm.BottomPanelActiveTab = 4;
        _vm.IsHierarchyPanelVisible = false;
        _vm.Workspace = before with { };
        Assert.Empty(changed);
        Assert.Same(before, _vm.Workspace);
    }

    [Fact]
    public void ShowPanelAndPresets_ReplaceTheTree()
    {
        _vm.ShowPanel("SplitScreen");
        Assert.Equal(1, _vm.SplitScreenMode);
        Assert.Equal("x(hierarchy,x(y(y(node,edit),bottom),inspector))", EditorWorkspaceStateTests.Shape(_vm.WorkspaceLayout.Tree));
        Assert.Equal("Bölünmüş: H", _vm.SplitScreenButtonText);

        _vm.SelectedLayoutPreset = "Sahne Odağı";
        Assert.Equal(EditorWorkspaceLayouts.Preset("Sahne Odağı"), _vm.Workspace);
        Assert.Equal("y(node,bottom)", EditorWorkspaceStateTests.Shape(_vm.WorkspaceLayout.Tree));
        Assert.True(_vm.IsSplitScreenOff);

        _vm.SelectLayoutPresetCommand.Execute("Bilinmeyen");
        Assert.Equal(EditorWorkspaceLayouts.Preset("Sahne Odağı"), _vm.Workspace);
    }
}
