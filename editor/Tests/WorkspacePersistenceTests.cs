using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 D5 — the workspace layout (tree, bottom-strip tabs, pinned panels,
/// remembered side-panel sizes) survives a restart: versioned JSON in the
/// machine-local editor profile, a silent fall back to the default workspace
/// for anything unreadable, and one coalesced write per burst of changes.
/// </summary>
public sealed class WorkspaceStateJsonTests
{
    private static readonly EditorWorkspaceState Default = EditorWorkspaceState.Default;

    /// <summary>A workspace with every persisted feature in use: moved panel, pin, resized split, remembered share.</summary>
    internal static EditorWorkspaceState Customised()
    {
        EditorWorkspaceState state = Default
            .HandlePanelAction("SaveSlots")                       // extra bottom tab, selected
            .Dock(PanelIds.Inspector, PanelIds.Node, DockEdge.Top)
            .TogglePin(PanelIds.Node);
        string hierarchySplit = ((SplitNode)state.Layout.Tree!).Id;
        state = state.WithSplitRatio(hierarchySplit, 0.31);
        return state.ClosePanel(PanelIds.Hierarchy);              // remembers the hierarchy share
    }

    public static IEnumerable<object[]> States() => new[]
    {
        new object[] { "Varsayılan", EditorWorkspaceLayouts.Preset("Varsayılan")! },
        new object[] { "Sahne Odağı", EditorWorkspaceLayouts.Preset("Sahne Odağı")! },
        new object[] { "Oyun Testi", EditorWorkspaceLayouts.Preset("Oyun Testi")! },
        new object[] { "Özel", Customised() },
        new object[] { "Bölünmüş ekran", Default.WithSplitScreen(2).TogglePin(PanelIds.Edit) },
    };

    [Theory]
    [MemberData(nameof(States))]
    public void RoundTrip_SameState_AndSameBytes(string name, EditorWorkspaceState state)
    {
        string json = WorkspaceStateJson.Serialize(state);
        WorkspaceStateLoadResult loaded = WorkspaceStateJson.Parse(json);

        Assert.True(loaded.IsValid, name + ": " + loaded.Error);
        Assert.Equal(state, loaded.State);
        Assert.Equal(state.Layout, loaded.State!.Layout);
        Assert.Equal(state.Bottom, loaded.State.Bottom);
        Assert.Equal(state.Pinned, loaded.State.Pinned);
        Assert.Equal(state.Shares, loaded.State.Shares);
        Assert.Equal(json, WorkspaceStateJson.Serialize(loaded.State));
    }

    [Fact]
    public void Customised_ReallyUsesEveryPersistedPart()
    {
        EditorWorkspaceState state = Customised();
        Assert.True(state.Bottom.IsOpen(BottomStripState.SaveSlotsTab));
        Assert.Equal(BottomStripState.SaveSlotsTab, state.Bottom.ActiveTab);
        Assert.True(state.IsPinned(PanelIds.Node));
        Assert.False(state.IsHierarchyVisible);
        Assert.True(state.Shares.TryGet(PanelIds.Hierarchy, out double share));
        Assert.Equal(0.31, share, 10);
        Assert.NotEqual(Default.Layout, state.Layout);
    }

    [Fact]
    public void DefaultWorkspace_BytesAreFrozen()
    {
        // The on-disk format is a contract: this text changing means a version bump + migration.
        Assert.Equal(
            "{\"version\":1,\"layout\":{\"version\":1,\"lastSplitId\":3,\"tree\":{\"type\":\"split\",\"id\":\"split-3\",\"axis\":\"x\",\"ratio\":0.14,"
            + "\"a\":{\"type\":\"leaf\",\"view\":\"hierarchy\"},\"b\":{\"type\":\"split\",\"id\":\"split-2\",\"axis\":\"x\",\"ratio\":0.772,"
            + "\"a\":{\"type\":\"split\",\"id\":\"split-1\",\"axis\":\"y\",\"ratio\":0.78,\"a\":{\"type\":\"leaf\",\"view\":\"node\"},\"b\":{\"type\":\"leaf\",\"view\":\"bottom\"}},"
            + "\"b\":{\"type\":\"leaf\",\"view\":\"inspector\"}}}},\"bottom\":{\"open\":[0,1],\"active\":0},\"pinned\":[],\"shares\":{}}",
            WorkspaceStateJson.Serialize(Default));
    }

    private static string Doc(string layout, string bottom = "{\"open\":[0,1],\"active\":0}", string pinned = "[]", string shares = "{}", string version = "1") =>
        "{\"version\":" + version + ",\"layout\":" + layout + ",\"bottom\":" + bottom + ",\"pinned\":" + pinned + ",\"shares\":" + shares + "}";

    private static string DefaultLayoutJson => WorkspaceLayoutJson.Serialize(Default.Layout);

    private static string LayoutOf(LayoutNode? tree, int last = 5) => WorkspaceLayoutJson.Serialize(new WorkspaceLayout(tree, last));

    public static IEnumerable<object?[]> InvalidDocuments()
    {
        string layout = DefaultLayoutJson;
        yield return new object?[] { "null", null };
        yield return new object?[] { "empty", "" };
        yield return new object?[] { "whitespace", "  \n" };
        yield return new object?[] { "not json", "{" };
        yield return new object?[] { "array root", "[]" };
        yield return new object?[] { "number root", "123" };
        yield return new object?[] { "null root", "null" };
        yield return new object?[] { "truncated", Doc(layout)[..^7] };
        yield return new object?[] { "trailing garbage", Doc(layout) + " x" };
        yield return new object?[] { "old version 0", Doc(layout, version: "0") };
        yield return new object?[] { "future version 2", Doc(layout, version: "2") };
        yield return new object?[] { "version string", Doc(layout, version: "\"1\"") };
        yield return new object?[] { "version fraction", Doc(layout, version: "1.5") };
        yield return new object?[] { "unknown property", Doc(layout).Replace("\"pinned\"", "\"extra\":1,\"pinned\"") };
        yield return new object?[] { "duplicate property", Doc(layout).Replace("\"pinned\":[]", "\"pinned\":[],\"pinned\":[]") };
        yield return new object?[] { "missing version", Doc(layout).Replace("\"version\":1,", "") };
        yield return new object?[] { "missing layout", "{\"version\":1,\"bottom\":{\"open\":[],\"active\":0},\"pinned\":[],\"shares\":{}}" };
        yield return new object?[] { "missing bottom", Doc(layout).Replace(",\"bottom\":{\"open\":[0,1],\"active\":0}", "") };
        yield return new object?[] { "missing pinned", Doc(layout).Replace(",\"pinned\":[]", "") };
        yield return new object?[] { "missing shares", Doc(layout).Replace(",\"shares\":{}", "") };
        yield return new object?[] { "layout is a string", Doc("\"x\"") };
        yield return new object?[] { "layout null", Doc("null") };
        yield return new object?[] { "layout old version", Doc(layout.Replace("\"version\":1", "\"version\":0")) };
        yield return new object?[] { "layout empty tree", Doc(LayoutOf(null), "{\"open\":[],\"active\":0}") };
        yield return new object?[] { "all unknown panels", Doc(LayoutOf(new SplitNode("split-1", SplitAxis.Horizontal, 0.5, new LeafNode("ghost-a"), new LeafNode("ghost-b")), 1), "{\"open\":[],\"active\":0}") };
        // Faz 8: Library, Lua and Store are real panels now; "console" (a bottom-strip tab) is a prototype-only leaf id.
        yield return new object?[] { "all prototype-only panels", Doc(LayoutOf(new SplitNode("split-1", SplitAxis.Horizontal, 0.5, new LeafNode("console"), new LeafNode("ghost-c")), 1), "{\"open\":[],\"active\":0}") };
        yield return new object?[] { "duplicate panel", Doc(LayoutOf(new SplitNode("split-1", SplitAxis.Horizontal, 0.5, new LeafNode("node"), new LeafNode("node")), 1), "{\"open\":[],\"active\":0}") };
        yield return new object?[] { "ratio zero", Doc(layout.Replace("0.14", "0")) };
        yield return new object?[] { "ratio one", Doc(layout.Replace("0.14", "1")) };
        yield return new object?[] { "bottom leaf without tabs", Doc(layout, "{\"open\":[],\"active\":0}") };
        yield return new object?[] { "tabs without bottom leaf", Doc(LayoutOf(new LeafNode("node"), 0), "{\"open\":[0],\"active\":0}") };
        yield return new object?[] { "tab index too big", Doc(layout, "{\"open\":[0,5],\"active\":0}") };
        yield return new object?[] { "tab index negative", Doc(layout, "{\"open\":[-1],\"active\":0}") };
        yield return new object?[] { "tab repeated", Doc(layout, "{\"open\":[0,0],\"active\":0}") };
        yield return new object?[] { "tab not a number", Doc(layout, "{\"open\":[\"a\"],\"active\":0}") };
        yield return new object?[] { "active out of range", Doc(layout, "{\"open\":[0,1],\"active\":7}") };
        yield return new object?[] { "active missing", Doc(layout, "{\"open\":[0,1]}") };
        yield return new object?[] { "open not array", Doc(layout, "{\"open\":1,\"active\":0}") };
        yield return new object?[] { "bottom unknown property", Doc(layout, "{\"open\":[0],\"active\":0,\"x\":1}") };
        yield return new object?[] { "bottom not object", Doc(layout, "[]") };
        yield return new object?[] { "pinned not in tree", Doc(layout, pinned: "[\"game\"]") };
        yield return new object?[] { "pinned unknown", Doc(layout, pinned: "[\"ghost\"]") };
        yield return new object?[] { "pinned repeated", Doc(layout, pinned: "[\"node\",\"node\"]") };
        yield return new object?[] { "pinned number", Doc(layout, pinned: "[1]") };
        yield return new object?[] { "pinned not array", Doc(layout, pinned: "{}") };
        yield return new object?[] { "share unknown panel", Doc(layout, shares: "{\"node\":0.5}") };
        yield return new object?[] { "share zero", Doc(layout, shares: "{\"hierarchy\":0}") };
        yield return new object?[] { "share too big", Doc(layout, shares: "{\"hierarchy\":0.99}") };
        yield return new object?[] { "share not number", Doc(layout, shares: "{\"hierarchy\":\"x\"}") };
        yield return new object?[] { "share repeated", Doc(layout, shares: "{\"hierarchy\":0.2,\"hierarchy\":0.3}") };
        yield return new object?[] { "shares not object", Doc(layout, shares: "[]") };
        yield return new object?[] { "depth bomb", new string('[', 5000) };
        yield return new object?[] { "deep tree", Doc(string.Concat(Enumerable.Repeat("{\"type\":\"split\",\"id\":\"s\",\"axis\":\"x\",\"ratio\":0.5,\"a\":", 200)) + "null") };
    }

    [Theory]
    [MemberData(nameof(InvalidDocuments))]
    public void InvalidDocument_IsRejected_WithoutThrowing(string name, string? json)
    {
        WorkspaceStateLoadResult result = WorkspaceStateJson.Parse(json);

        Assert.False(result.IsValid, name);
        Assert.Null(result.State);
        Assert.False(string.IsNullOrEmpty(result.Error), name);
    }

    [Fact]
    public void UnknownPanelId_PrunesLeafAndKeepsValidSiblings()
    {
        string layout = LayoutOf(
            new SplitNode(
                "split-2",
                SplitAxis.Horizontal,
                0.25,
                new LeafNode("ghost"),
                new SplitNode("split-1", SplitAxis.Vertical, 0.6, new LeafNode("game"), new LeafNode("edit"))),
            2);
        WorkspaceStateLoadResult result = WorkspaceStateJson.Parse(
            Doc(layout, "{\"open\":[],\"active\":0}", "[\"edit\",\"ghost\"]", "{\"inspector\":0.25}"));

        Assert.True(result.IsValid, result.Error);
        Assert.Equal(new[] { "game", "edit" }, result.State!.Layout.Views);
        SplitNode survivingRoot = Assert.IsType<SplitNode>(result.State.Layout.Tree);
        Assert.Equal("split-1", survivingRoot.Id);
        Assert.Equal(0.6, survivingRoot.Ratio);
        Assert.True(result.State.IsPinned(PanelIds.Edit));
        Assert.False(result.State.IsPinned("ghost"));

        // Prototype-only leaf ("console") is also pruned while hosted sibling ("node") survives.
        string withStore = LayoutOf(new SplitNode("split-1", SplitAxis.Horizontal, 0.5, new LeafNode("node"), new LeafNode("console")), 1);
        WorkspaceStateLoadResult storeResult = WorkspaceStateJson.Parse(Doc(withStore, "{\"open\":[],\"active\":0}"));
        Assert.True(storeResult.IsValid, storeResult.Error);
        Assert.Equal(new[] { PanelIds.Node }, storeResult.State!.Layout.Views);

        // Faz 7: a saved Lua editor / Node library leaf is a real panel and survives.
        string withLua = LayoutOf(new SplitNode("split-1", SplitAxis.Horizontal, 0.5, new LeafNode("node"), new LeafNode("lua")), 1);
        WorkspaceStateLoadResult luaResult = WorkspaceStateJson.Parse(Doc(withLua, "{\"open\":[],\"active\":0}"));
        Assert.True(luaResult.IsValid, luaResult.Error);
        Assert.Equal(new[] { PanelIds.Node, PanelIds.Lua }, luaResult.State!.Layout.Views);
    }

    [Fact]
    public void ValidHandWrittenDocument_Loads()
    {
        string layout = LayoutOf(new SplitNode("split-1", SplitAxis.Vertical, 0.6, new LeafNode("game"), new LeafNode("edit")), 1);
        WorkspaceStateLoadResult result = WorkspaceStateJson.Parse(
            Doc(layout, "{\"open\":[],\"active\":3}", "[\"edit\"]", "{\"inspector\":0.25}"));

        Assert.True(result.IsValid, result.Error);
        Assert.Equal(new[] { "game", "edit" }, result.State!.Layout.Views);
        Assert.True(result.State.IsPinned(PanelIds.Edit));
        Assert.False(result.State.Bottom.AnyOpen);
        Assert.True(result.State.Shares.TryGet(PanelIds.Inspector, out double s) && s == 0.25);
    }

    [Fact]
    public void SelectedTabThatIsNotOpen_FallsBackToTheFirstOpenTab()
    {
        WorkspaceStateLoadResult result = WorkspaceStateJson.Parse(Doc(DefaultLayoutJson, "{\"open\":[1,3],\"active\":4}"));

        Assert.True(result.IsValid, result.Error);
        Assert.Equal(BottomTabs.Log | BottomTabs.SaveSlots, result.State!.Bottom.Open);
        Assert.Equal(BottomStripState.LogTab, result.State.Bottom.ActiveTab);
    }

    [Fact]
    public void Parse_IsCultureIndependent()
    {
        string json = WorkspaceStateJson.Serialize(Customised());
        System.Globalization.CultureInfo previous = System.Globalization.CultureInfo.CurrentCulture;
        try
        {
            foreach (string culture in new[] { "tr-TR", "de-DE", "ar-SA" })
            {
                System.Globalization.CultureInfo.CurrentCulture = new System.Globalization.CultureInfo(culture);
                Assert.Equal(json, WorkspaceStateJson.Serialize(Customised()));
                WorkspaceStateLoadResult loaded = WorkspaceStateJson.Parse(json);
                Assert.True(loaded.IsValid, culture + ": " + loaded.Error);
                Assert.Equal(Customised(), loaded.State);
            }
        }
        finally { System.Globalization.CultureInfo.CurrentCulture = previous; }
    }

    [Fact]
    public void RandomWorkspaces_AlwaysRoundTrip()
    {
        var random = new Random(20261016);
        string[] actions = { "Hierarchy", "Inspector", "Log", "Assets", "Backlog", "SaveSlots", "ProjectIssues", "NodeGraph", "Preview", "EnginePreview", "SplitScreen" };
        EditorWorkspaceState state = Default;
        for (int i = 0; i < 600; i++)
        {
            switch (random.Next(5))
            {
                case 0:
                    state = state.HandlePanelAction(actions[random.Next(actions.Length)]);
                    break;
                case 1:
                    string[] views = state.Layout.Views.ToArray();
                    state = state.Dock(views[random.Next(views.Length)], views[random.Next(views.Length)], (DockEdge)random.Next(4));
                    break;
                case 2:
                    views = state.Layout.Views.ToArray();
                    state = state.TogglePin(views[random.Next(views.Length)]);
                    break;
                case 3:
                    if (state.Layout.Tree is SplitNode split)
                        state = state.WithSplitRatio(split.Id, random.NextDouble());
                    break;
                default:
                    state = state.ApplyPreset(EditorWorkspaceLayouts.PresetNames[random.Next(3)]);
                    break;
            }

            string json = WorkspaceStateJson.Serialize(state);
            WorkspaceStateLoadResult loaded = WorkspaceStateJson.Parse(json);
            Assert.True(loaded.IsValid, $"step {i}: {loaded.Error}\n{json}");
            // The one deliberate normalisation: a selected tab that is not open is moved to an open one.
            EditorWorkspaceState expected = state.Bottom.AnyOpen && !state.Bottom.IsOpen(state.Bottom.ActiveTab)
                ? state.WithActiveBottomTab(state.Bottom.FirstOpenTab())
                : state;
            Assert.Equal(expected, loaded.State);
        }
    }
}

/// <summary>Faz 5 D5 — closed side panels reopen at the size the user had dragged them to.</summary>
public sealed class WorkspaceRememberedSizeTests
{
    private static double ShareOf(EditorWorkspaceState state, string panel) =>
        EditorWorkspaceLayouts.ShareOf(state.Layout, panel) ?? throw new Xunit.Sdk.XunitException(panel + " has no canonical split");

    private static string SplitOf(EditorWorkspaceState state, string panel)
    {
        string? found = null;
        void Walk(LayoutNode? node)
        {
            if (node is SplitNode s)
            {
                if ((s.A is LeafNode { } a && a.View == panel) || (s.B is LeafNode { } b && b.View == panel))
                    found ??= s.Id;
                Walk(s.A);
                Walk(s.B);
            }
        }
        Walk(state.Layout.Tree);
        return found ?? throw new Xunit.Sdk.XunitException(panel + " not in a split");
    }

    [Fact]
    public void Hierarchy_ReopensAtTheDraggedShare()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Hierarchy), 0.33);

        EditorWorkspaceState closed = state.WithPanel(PanelIds.Hierarchy, false);
        Assert.False(closed.IsHierarchyVisible);
        EditorWorkspaceState reopened = closed.WithPanel(PanelIds.Hierarchy, true);

        Assert.Equal(0.33, ShareOf(reopened, PanelIds.Hierarchy), 10);
    }

    [Fact]
    public void Inspector_ReopensAtTheDraggedShare()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Inspector), 0.6);          // inspector 40 %

        EditorWorkspaceState reopened = state.ClosePanel(PanelIds.Inspector).WithPanel(PanelIds.Inspector, true);

        Assert.Equal(0.4, ShareOf(reopened, PanelIds.Inspector), 10);
    }

    [Fact]
    public void BottomStrip_ReopensAtTheDraggedShare_ViaTabsAndViaClose()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Bottom), 0.55);            // strip 45 %

        // Close with the header ×.
        EditorWorkspaceState viaClose = state.ClosePanel(PanelIds.Bottom).WithBottomTab(BottomStripState.LogTab, true);
        Assert.Equal(0.45, ShareOf(viaClose, PanelIds.Bottom), 10);

        // Close by closing the last open tab (shortcut / menu).
        EditorWorkspaceState viaTabs = state.WithBottomTab(0, false).WithBottomTab(1, false);
        Assert.False(viaTabs.IsBottomVisible);
        Assert.Equal(0.45, ShareOf(viaTabs.WithBottomTab(BottomStripState.IssuesTab, true), PanelIds.Bottom), 10);
    }

    [Fact]
    public void NeverClosedPanels_AndFirstOpen_UseTheDefaultRatios()
    {
        EditorWorkspaceState fresh = EditorWorkspaceState.Default.WithPanel(PanelIds.Hierarchy, false).WithPanel(PanelIds.Hierarchy, true);
        Assert.Equal(EditorWorkspaceLayouts.HierarchyRatio, ShareOf(fresh, PanelIds.Hierarchy), 10);

        // Closing one panel remembers only that panel.
        EditorWorkspaceState closedInspector = EditorWorkspaceState.Default.WithPanel(PanelIds.Inspector, false);
        Assert.Equal(new[] { PanelIds.Inspector }, closedInspector.Shares.Select(s => s.Key).ToArray());
    }

    [Fact]
    public void ExtremeRatios_AreClamped_AndStayValid()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Inspector), 0.0001); // clamps to MinRatio => inspector share 0.92
        EditorWorkspaceState reopened = state.ClosePanel(PanelIds.Inspector).WithPanel(PanelIds.Inspector, true);

        Assert.Null(reopened.Layout.Validate());
        Assert.True(WorkspaceStateJson.Parse(WorkspaceStateJson.Serialize(state.ClosePanel(PanelIds.Inspector))).IsValid);
    }

    [Fact]
    public void PanelMovedAwayFromItsSlot_KeepsThePreviousRememberedShare()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Hierarchy), 0.3).WithPanel(PanelIds.Hierarchy, false);
        Assert.True(state.Shares.TryGet(PanelIds.Hierarchy, out double before));

        // Hierarchy reopens, is dragged under the Node (not a column any more), closes again.
        EditorWorkspaceState moved = state.WithPanel(PanelIds.Hierarchy, true)
            .Dock(PanelIds.Hierarchy, PanelIds.Node, DockEdge.Bottom)
            .WithPanel(PanelIds.Hierarchy, false);

        Assert.True(moved.Shares.TryGet(PanelIds.Hierarchy, out double after));
        Assert.Equal(before, after);
    }

    [Fact]
    public void LayoutPreset_ResetsTheLayout_IncludingRememberedSizes()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Hierarchy), 0.3).WithPanel(PanelIds.Hierarchy, false);
        Assert.NotEmpty(state.Shares);

        EditorWorkspaceState reset = state.ApplyPreset(EditorWorkspaceLayouts.DefaultPreset);

        Assert.Equal(EditorWorkspaceState.Default.Layout, reset.Layout);
        Assert.Empty(reset.Shares);
        Assert.Equal(EditorWorkspaceLayouts.HierarchyRatio, ShareOf(reset, PanelIds.Hierarchy), 10);
    }

    [Fact]
    public void RememberedShares_SurviveSaveAndLoad()
    {
        EditorWorkspaceState state = EditorWorkspaceState.Default;
        state = state.WithSplitRatio(SplitOf(state, PanelIds.Hierarchy), 0.27).WithPanel(PanelIds.Hierarchy, false);

        EditorWorkspaceState loaded = WorkspaceStateJson.Parse(WorkspaceStateJson.Serialize(state)).State!;

        Assert.Equal(0.27, ShareOf(loaded.WithPanel(PanelIds.Hierarchy, true), PanelIds.Hierarchy), 10);
    }
}

/// <summary>Faz 5 D5 — the profile file: load fallbacks, shared with the other editor settings.</summary>
public sealed class WorkspaceStatePersistenceTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "RowlWorkspacePersist_" + Guid.NewGuid().ToString("N"));
    private readonly List<string> _warnings = new();

    public WorkspaceStatePersistenceTests()
    {
        // SettingsViewModel / EngineHost are Avalonia-side objects: see WorkspacePersistenceViewModelTests
        // for why the headless runtime must exist before anything can touch Dispatcher.UIThread.
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Directory.CreateDirectory(_dir);
    }

    public void Dispose()
    {
        try { Directory.Delete(_dir, recursive: true); } catch (Exception) { }
    }

    private string Profile => Path.Combine(_dir, "editor-settings.json");

    private EditorWorkspaceState Load() => WorkspaceStatePersistence.Load(Profile, _warnings.Add);

    [Fact]
    public void TestFiles_AreIsolated_FromTheRealUserProfile()
    {
        string realProfile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "RowlEngine", "editor-settings.json");
        Assert.NotEqual(Path.GetFullPath(realProfile), Path.GetFullPath(Profile));
        Assert.StartsWith(Path.GetFullPath(Path.GetTempPath()), Path.GetFullPath(Profile));
    }

    [Fact]
    public void SaveThenLoad_GivesTheSameState()
    {
        EditorWorkspaceState state = WorkspaceStateJsonTests.Customised();

        WorkspaceStatePersistence.Save(Profile, state);
        EditorWorkspaceState loaded = Load();

        Assert.Equal(state, loaded);
        Assert.Equal(state.Layout, loaded.Layout);
        Assert.Equal(state.Bottom, loaded.Bottom);
        Assert.Equal(state.Pinned, loaded.Pinned);
        Assert.Empty(_warnings);
    }

    [Fact]
    public void MissingFile_AndProfileWithoutLayout_GiveTheDefault_Silently()
    {
        Assert.Equal(EditorWorkspaceState.Default, Load());

        new EditorSettingsProfile { AutoSaveIntervalSeconds = 90 }.Save(Profile);   // a profile from before D5
        Assert.Equal(EditorWorkspaceState.Default, Load());
        Assert.Empty(_warnings);
    }

    [Fact]
    public void OldProfileJson_WithoutTheField_StillLoadsItsOtherSettings()
    {
        File.WriteAllText(Profile, "{\"AutoSaveEnabled\":false,\"AutoSaveIntervalSeconds\":120,\"AssetGridItemSize\":100,\"EditorLanguage\":\"en\"}");

        EditorSettingsProfile profile = EditorSettingsProfile.Load(Profile);

        Assert.False(profile.AutoSaveEnabled);
        Assert.Equal(120, profile.AutoSaveIntervalSeconds);
        Assert.Null(profile.WorkspaceState);
        Assert.Equal(EditorWorkspaceState.Default, Load());
    }

    public static IEnumerable<object[]> BadWorkspaceTexts() => new[]
    {
        new object[] { "garbage", "this is not json" },
        new object[] { "truncated", "{\"version\":1,\"layout\":{\"ver" },
        new object[] { "empty", "" },
        new object[] { "old version", "{\"version\":0,\"layout\":{},\"bottom\":{},\"pinned\":[],\"shares\":{}}" },
        new object[] { "future version", WorkspaceStateJson.Serialize(EditorWorkspaceState.Default).Replace("\"version\":1,\"layout\"", "\"version\":9,\"layout\"") },
        new object[]
        {
            "all unknown panels",
            WorkspaceStateJson.Serialize(EditorWorkspaceState.Default)
                .Replace("\"hierarchy\"", "\"removed-1\"")
                .Replace("\"node\"", "\"removed-2\"")
                .Replace("\"bottom\"", "\"removed-3\"")
                .Replace("\"inspector\"", "\"removed-4\"")
        },
        new object[] { "layout from D1 only", WorkspaceLayoutJson.Serialize(EditorWorkspaceState.Default.Layout) },
    };

    [Theory]
    [MemberData(nameof(BadWorkspaceTexts))]
    public void UnusableWorkspaceText_FallsBackToTheDefault_WithADevelopmentWarning(string name, string text)
    {
        new EditorSettingsProfile { AutoSaveIntervalSeconds = 90, WorkspaceState = text }.Save(Profile);

        EditorWorkspaceState loaded = Load();

        Assert.Equal(EditorWorkspaceState.Default, loaded);
        Assert.Single(_warnings);
        Assert.Contains("workspace", _warnings[0], StringComparison.OrdinalIgnoreCase);
        // The rest of the profile is untouched by the failed layout.
        Assert.Equal(90, EditorSettingsProfile.Load(Profile).AutoSaveIntervalSeconds);
        Assert.False(string.IsNullOrEmpty(name));
    }

    [Fact]
    public void UnknownPanelId_PrunesLeafAndKeepsValidSiblings_OnLoad()
    {
        string withRetiredInspector = WorkspaceStateJson.Serialize(EditorWorkspaceState.Default)
            .Replace("\"inspector\"", "\"removed-panel\"");
        new EditorSettingsProfile { AutoSaveIntervalSeconds = 90, WorkspaceState = withRetiredInspector }.Save(Profile);

        EditorWorkspaceState loaded = Load();

        Assert.Empty(_warnings);
        Assert.True(loaded.IsHierarchyVisible);
        Assert.True(loaded.IsNodeGraphActive);
        Assert.True(loaded.IsBottomVisible);
        Assert.False(loaded.IsInspectorVisible);
        Assert.Equal(EditorWorkspaceState.Default.Layout.RemoveView(PanelIds.Inspector), loaded.Layout);
    }

    [Fact]
    public void CorruptProfileFile_GivesTheDefault_WithAWarning_AndNoCrash()
    {
        File.WriteAllText(Profile, "{ this is { not json");

        Assert.Equal(EditorWorkspaceState.Default, Load());
        Assert.NotEmpty(_warnings);
    }

    [Fact]
    public void UnreadableProfilePath_GivesTheDefault_WithoutThrowing()
    {
        Directory.CreateDirectory(Profile);   // a directory where the file should be

        Assert.Equal(EditorWorkspaceState.Default, Load());
    }

    [Fact]
    public void WorkspaceSave_KeepsTheOtherSettings_AndSettingsSave_KeepsTheWorkspace()
    {
        new EditorSettingsProfile { AutoSaveEnabled = false, AutoSaveIntervalSeconds = 120, AssetGridItemSize = 100 }.Save(Profile);
        EditorWorkspaceState state = WorkspaceStateJsonTests.Customised();

        WorkspaceStatePersistence.Save(Profile, state);
        EditorSettingsProfile afterWorkspaceSave = EditorSettingsProfile.Load(Profile);
        Assert.False(afterWorkspaceSave.AutoSaveEnabled);
        Assert.Equal(120, afterWorkspaceSave.AutoSaveIntervalSeconds);
        Assert.Equal(100, afterWorkspaceSave.AssetGridItemSize);

        // The settings dialog writes through the sync service: the workspace must survive it.
        var settings = new SettingsViewModel();
        EditorSettingsSyncService.LoadEditorSettings(Profile, settings);
        settings.AssetGridItemSize = 64;
        using var host = new RowlEngine.Editor.Native.EngineHost();
        EditorSettingsSyncService.HandleSettingsPropertyChanged(
            nameof(SettingsViewModel.AssetGridItemSize), settings, Profile, Path.Combine(_dir, "player.json"), _dir,
            host, () => new ProjectRuntimeSettings(), _ => { }, () => { });

        Assert.Equal(64, EditorSettingsProfile.Load(Profile).AssetGridItemSize);
        Assert.Equal(120, EditorSettingsProfile.Load(Profile).AutoSaveIntervalSeconds);
        Assert.Equal(state, Load());
    }

    [Fact]
    public void Save_IsAtomic_LeavesNoTemporaryFiles()
    {
        for (int i = 0; i < 5; i++)
            WorkspaceStatePersistence.Save(Profile, i % 2 == 0 ? EditorWorkspaceState.Default : WorkspaceStateJsonTests.Customised());

        Assert.Equal(new[] { "editor-settings.json" }, Directory.GetFileSystemEntries(_dir).Select(Path.GetFileName).ToArray());
    }
}

/// <summary>Faz 5 D5 — writes are coalesced: a burst of changes is one write, never one per event.</summary>
public sealed class WorkspaceAutosaveTests
{
    /// <summary>Manual clock: nothing fires until <see cref="Elapse"/>.</summary>
    private sealed class ManualTimers
    {
        public sealed class Timer : IDebounceTimer
        {
            private readonly Action _elapsed;
            public bool Running;
            public int Restarts;
            public bool Disposed;
            public Timer(Action elapsed) => _elapsed = elapsed;
            public void Restart() { Running = true; Restarts++; }
            public void Stop() => Running = false;
            public void Dispose() { Running = false; Disposed = true; }
            public void Fire()
            {
                if (!Running) return;
                Running = false;
                _elapsed();
            }
        }

        public Timer? Current;
        public TimeSpan? Delay;
        public IDebounceTimer Create(TimeSpan delay, Action elapsed)
        {
            Delay = delay;
            return Current = new Timer(elapsed);
        }
        public void Elapse() => Current!.Fire();
    }

    private static readonly EditorWorkspaceState Start = EditorWorkspaceState.Default;
    private static EditorWorkspaceState Step(int n) => Start.WithSplitRatio("split-3", 0.2 + 0.01 * n);

    private static (WorkspaceAutosave save, ManualTimers timers, List<EditorWorkspaceState> writes, List<string> warnings) Create(Action<EditorWorkspaceState>? onWrite = null)
    {
        var timers = new ManualTimers();
        var writes = new List<EditorWorkspaceState>();
        var warnings = new List<string>();
        var save = new WorkspaceAutosave(s => { onWrite?.Invoke(s); writes.Add(s); }, timers.Create, TimeSpan.FromMilliseconds(500), Start, warnings.Add);
        return (save, timers, writes, warnings);
    }

    [Fact]
    public void ManyChanges_AreOneWrite_OfTheLastState()
    {
        var (save, timers, writes, _) = Create();

        for (int i = 1; i <= 25; i++)
            save.Notify(Step(i));
        Assert.Empty(writes);                 // nothing on disk during the burst
        Assert.True(timers.Current!.Running);
        Assert.Equal(25, timers.Current.Restarts);

        timers.Elapse();

        Assert.Single(writes);
        Assert.Equal(Step(25), writes[0]);
        Assert.False(save.HasPending);
        timers.Elapse();                      // a second tick has nothing left to do
        Assert.Single(writes);
    }

    [Fact]
    public void SeparateBursts_AreSeparateWrites()
    {
        var (save, timers, writes, _) = Create();

        save.Notify(Step(1));
        save.Notify(Step(2));
        timers.Elapse();
        save.Notify(Step(3));
        timers.Elapse();

        Assert.Equal(new[] { Step(2), Step(3) }, writes);
    }

    [Fact]
    public void Delay_IsHalfASecond()
    {
        var (_, timers, _, _) = Create();
        Assert.Equal(TimeSpan.FromMilliseconds(500), timers.Delay);
        Assert.Equal(TimeSpan.FromMilliseconds(500), WorkspaceAutosave.DefaultDelay);
    }

    [Fact]
    public void ChangeThatIsRevertedInsideTheQuietPeriod_WritesNothing()
    {
        var (save, timers, writes, _) = Create();

        save.Notify(Step(1));
        save.Notify(Start);                   // back to what is on disk

        Assert.False(save.HasPending);
        Assert.False(timers.Current!.Running);
        timers.Elapse();
        Assert.Empty(writes);
    }

    [Fact]
    public void NotifyingTheSavedBaseline_StartsNothing()
    {
        var (save, timers, writes, _) = Create();

        save.Notify(Start);

        Assert.False(timers.Current!.Running);
        Assert.Empty(writes);
    }

    [Fact]
    public void Flush_WritesThePendingStateAtOnce_AndStopsTheTimer()
    {
        var (save, timers, writes, _) = Create();
        save.Notify(Step(4));

        save.Flush();

        Assert.Equal(new[] { Step(4) }, writes);
        Assert.False(timers.Current!.Running);
        save.Flush();
        Assert.Single(writes);
    }

    [Fact]
    public void Dispose_FlushesLikeWindowClose_ThenIgnoresChanges()
    {
        var (save, timers, writes, _) = Create();
        save.Notify(Step(7));

        save.Dispose();

        Assert.Equal(new[] { Step(7) }, writes);
        Assert.True(timers.Current!.Disposed);
        save.Notify(Step(8));
        save.Flush();
        save.Dispose();
        Assert.Single(writes);
    }

    [Fact]
    public void FailedWrite_IsReported_NotThrown_AndRetriedByTheNextChange()
    {
        bool fail = true;
        var (save, timers, writes, warnings) = Create(_ => { if (fail) throw new IOException("disk full"); });

        save.Notify(Step(1));
        timers.Elapse();                      // must not throw
        Assert.Empty(writes);
        Assert.Single(warnings);
        Assert.Contains("disk full", warnings[0]);

        fail = false;
        save.Notify(Step(1));                 // same state again: it never reached the disk, so it is still a change
        timers.Elapse();
        Assert.Equal(new[] { Step(1) }, writes);
    }
}

/// <summary>
/// Faz 5 D5 — the view model end to end with a manual clock and a temporary
/// profile (never the user's): restore on start, one write per burst, flush on dispose,
/// default fallback, and no persistence unless it is attached.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class WorkspacePersistenceViewModelTests : IDisposable
{
    private sealed class ManualTimers
    {
        public sealed class Timer : IDebounceTimer
        {
            private readonly Action _elapsed;
            public bool Running;
            public Timer(Action elapsed) => _elapsed = elapsed;
            public void Restart() => Running = true;
            public void Stop() => Running = false;
            public void Dispose() => Running = false;
            public void Fire()
            {
                if (!Running) return;
                Running = false;
                _elapsed();
            }
        }

        public Timer? Current;
        public IDebounceTimer Create(TimeSpan delay, Action elapsed) => Current = new Timer(elapsed);
    }

    private readonly string _previousRoot;
    private readonly string _parent;
    private readonly string _root;
    private readonly string _profile;
    private readonly List<MainWindowViewModel> _vms = new();

    public WorkspacePersistenceViewModelTests()
    {
        // The view model owns Dispatcher timers: the FIRST thread to touch Dispatcher.UIThread becomes
        // Avalonia's UI thread for the rest of the process. Building it on an xUnit worker thread
        // before the headless runtime exists would hijack that role and break every later headless
        // UI test (order dependent). So: headless runtime first, view model built/used/disposed on its UI thread.
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        _parent = Path.Combine(Path.GetTempPath(), "RowlWorkspacePersistVm_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(_parent);
        var created = ProjectFactory.CreateNewProject("WorkspacePersist", _parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _profile = Path.Combine(_parent, "profile", "editor-settings.json");
    }

    private static void Ui(Action action) => Dispatcher.UIThread.Invoke(action);

    public void Dispose()
    {
        foreach (MainWindowViewModel vm in _vms)
        {
            try { Ui(vm.Dispose); } catch (Exception) { }
        }
        try { Directory.Delete(_parent, recursive: true); } catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    private MainWindowViewModel NewVm(ManualTimers? timers = null, bool attach = true)
    {
        MainWindowViewModel vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
        _vms.Add(vm);
        if (attach)
            vm.AttachWorkspacePersistence(_profile, (timers ?? new ManualTimers()).Create);
        return vm;
    }

    private string? SavedWorkspaceText() => File.Exists(_profile) ? EditorSettingsProfile.Load(_profile).WorkspaceState : null;

    [Fact]
    public void FirstRun_StartsWithTheDefault_AndWritesNothing()
    {
        Ui(() =>
        {
            var timers = new ManualTimers();
            MainWindowViewModel vm = NewVm(timers);

            Assert.Equal(EditorWorkspaceState.Default, vm.Workspace);
            Assert.False(File.Exists(_profile));
            Assert.False(timers.Current!.Running);
        });
    }

    [Fact]
    public void ChangesAreWrittenOnceAfterTheQuietPeriod_AndRestoredByTheNextStart()
    {
        Ui(() =>
        {
            var timers = new ManualTimers();
            MainWindowViewModel vm = NewVm(timers);

            // A burst of user actions: splitter drops, a panel closed, a pin, a drag-and-drop, a preset-less tab.
            string hierarchySplit = ((SplitNode)vm.Workspace.Layout.Tree!).Id;
            vm.ResizeWorkspaceSplit(hierarchySplit, 0.25);
            vm.ResizeWorkspaceSplit(hierarchySplit, 0.3);
            vm.ShowPanel("SaveSlots");
            vm.SetWorkspacePanelPinned(PanelIds.Node, true);
            vm.DockWorkspacePanel(PanelIds.Inspector, PanelIds.Node, DockEdge.Right);
            vm.CloseWorkspacePanel(PanelIds.Hierarchy);

            Assert.False(File.Exists(_profile));   // still inside the quiet period: no disk write per event
            Assert.True(timers.Current!.Running);
            timers.Current.Fire();

            string? text = SavedWorkspaceText();
            Assert.NotNull(text);
            Assert.Equal(WorkspaceStateJson.Serialize(vm.Workspace), text);

            // "Restart the editor".
            MainWindowViewModel next = NewVm();
            Assert.Equal(vm.Workspace, next.Workspace);
            Assert.False(next.IsHierarchyPanelVisible);
            Assert.True(next.IsSaveSlotsPanelVisible);
            Assert.True(next.WorkspacePinnedPanels.Contains(PanelIds.Node));
        });
    }

    [Fact]
    public void ClosingTheEditor_FlushesAPendingChange()
    {
        Ui(() =>
        {
            var timers = new ManualTimers();
            MainWindowViewModel vm = NewVm(timers);
            vm.CloseWorkspacePanel(PanelIds.Inspector);
            Assert.False(File.Exists(_profile));

            vm.Dispose();

            Assert.NotNull(SavedWorkspaceText());
            Assert.False(WorkspaceStateJson.Parse(SavedWorkspaceText()).State!.IsInspectorVisible);
        });
    }

    [Fact]
    public void ReopenedPanel_ReturnsToItsLastSize_AcrossARestart()
    {
        Ui(() =>
        {
            var timers = new ManualTimers();
            MainWindowViewModel vm = NewVm(timers);
            string hierarchySplit = ((SplitNode)vm.Workspace.Layout.Tree!).Id;
            vm.ResizeWorkspaceSplit(hierarchySplit, 0.28);
            vm.CloseWorkspacePanel(PanelIds.Hierarchy);
            timers.Current!.Fire();

            MainWindowViewModel next = NewVm();
            Assert.False(next.IsHierarchyPanelVisible);
            next.ShowPanel("Hierarchy");

            Assert.Equal(0.28, ((SplitNode)next.Workspace.Layout.Tree!).Ratio, 10);
        });
    }

    [Fact]
    public void LayoutPreset_IsTheResetPath_AndIsSavedToo()
    {
        Ui(() =>
        {
            var timers = new ManualTimers();
            MainWindowViewModel vm = NewVm(timers);
            vm.ResizeWorkspaceSplit(((SplitNode)vm.Workspace.Layout.Tree!).Id, 0.4);
            vm.CloseWorkspacePanel(PanelIds.Inspector);
            timers.Current!.Fire();

            // The label is still "Varsayılan" (manual changes keep it): choosing it again must still reset.
            Assert.Equal(EditorWorkspaceLayouts.DefaultPreset, vm.SelectedLayoutPreset);
            vm.SelectLayoutPresetCommand.Execute(EditorWorkspaceLayouts.DefaultPreset);
            timers.Current.Fire();

            Assert.Equal(EditorWorkspaceLayouts.Preset(EditorWorkspaceLayouts.DefaultPreset)!.Layout, NewVm().Workspace.Layout);
        });
    }

    [Fact]
    public void BrokenSavedLayout_StartsTheDefault_AndKeepsWorking()
    {
        Ui(() =>
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_profile)!);
            new EditorSettingsProfile { WorkspaceState = "{\"version\":1,\"layout\":{\"tree\":null}}" }.Save(_profile);
            var timers = new ManualTimers();

            MainWindowViewModel vm = NewVm(timers);

            Assert.Equal(EditorWorkspaceState.Default, vm.Workspace);
            vm.CloseWorkspacePanel(PanelIds.Inspector);   // and saving over it works
            timers.Current!.Fire();
            Assert.False(NewVm().IsInspectorPanelVisible);
        });
    }

    [Fact]
    public void SavedLayout_WithAPanelThatNoLongerExists_PrunesLeafAndKeepsValidSiblings()
    {
        Ui(() =>
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_profile)!);
            string withGhost = WorkspaceStateJson.Serialize(EditorWorkspaceState.Default).Replace("\"hierarchy\"", "\"retired-panel\"");
            new EditorSettingsProfile { WorkspaceState = withGhost }.Save(_profile);

            MainWindowViewModel vm = NewVm();
            Assert.False(vm.IsHierarchyPanelVisible);
            Assert.True(vm.IsInspectorPanelVisible);
            Assert.Equal(EditorWorkspaceState.Default.Layout.RemoveView(PanelIds.Hierarchy), vm.Workspace.Layout);
        });
    }

    [Fact]
    public void SavedLayout_WithAPanelThatNoLongerExists_FallsBackToTheDefault()
    {
        Ui(() =>
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_profile)!);
            string allRetired = WorkspaceStateJson.Serialize(EditorWorkspaceState.Default)
                .Replace("\"hierarchy\"", "\"retired-1\"")
                .Replace("\"node\"", "\"retired-2\"")
                .Replace("\"bottom\"", "\"retired-3\"")
                .Replace("\"inspector\"", "\"retired-4\"");
            new EditorSettingsProfile { WorkspaceState = allRetired }.Save(_profile);

            Assert.Equal(EditorWorkspaceState.Default, NewVm().Workspace);
        });
    }

    [Fact]
    public void WithoutAttach_NothingIsReadOrWritten()
    {
        Ui(() =>
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_profile)!);
            WorkspaceStatePersistence.Save(_profile, EditorWorkspaceLayouts.Preset("Oyun Testi")!);
            byte[] before = File.ReadAllBytes(_profile);

            MainWindowViewModel vm = NewVm(attach: false);
            Assert.Equal(EditorWorkspaceState.Default, vm.Workspace);   // the profile is not read
            vm.CloseWorkspacePanel(PanelIds.Inspector);
            vm.Dispose();

            Assert.Equal(before, File.ReadAllBytes(_profile));           // and not written
        });
    }

    [Fact]
    public void AttachingTwice_ReplacesTheEarlierAutosave()
    {
        Ui(() =>
        {
            var first = new ManualTimers();
            MainWindowViewModel vm = NewVm(first);
            var second = new ManualTimers();
            vm.AttachWorkspacePersistence(_profile, second.Create);

            vm.CloseWorkspacePanel(PanelIds.Inspector);

            Assert.False(first.Current!.Running);
            Assert.True(second.Current!.Running);
        });
    }
}
