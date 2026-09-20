using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Search;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 4 Dilim 3 — visual groups, subgraph navigation and chapter
/// boundaries: frame math, header-drag member translation, metadata-only
/// persistence, group culling parity + scope gating, depth stack +
/// breadcrumbs, root boundary rule + wire gating, port helpers, badges,
/// chapter split/merge round-trip + duplicate rejection, v4 backward
/// compat, snapshot v5 and MainVM end-to-end delegation. Headless.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorGroupsSubgraphsChaptersSlice3Tests
{
    private sealed class SliceShell : IDisposable
    {
        public string PreviousRoot { get; }

        public MainWindowViewModel Vm { get; }

        public string Root { get; }

        public SliceShell()
        {
            PreviousRoot = MainWindowViewModel.ProjectRoot;
            string parent = Path.Combine(Path.GetTempPath(), "RowlSlice3_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(parent);
            var created = ProjectFactory.CreateNewProject("Slice3Test", parent);
            if (!created.Success || created.Info == null)
                throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
            Root = created.Info.Path;
            Vm = new MainWindowViewModel(Root, connectEngine: false);
            Vm.Nodes.Clear();
            Vm.Connections.Clear();
            Vm.Groups.Clear();
            Vm.Subgraphs.Clear();
            Vm.Chapters.Clear();
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }

        public void Dispose()
        {
            try { Directory.Delete(Path.GetDirectoryName(Root)!, recursive: true); }
            catch (Exception) { }
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PreviousRoot;
            }
            catch (Exception) { }
        }
    }

    private static NodeViewModel BareNode(ulong id, string title, double x = 0, double y = 0) =>
        new(id, title, x, y, bare: true);

    // ── Group frame math ─────────────────────────────────────────

    [Fact]
    public void Group_FrameAroundNodes_TightBoundsPlusPadding()
    {
        var a = BareNode(1, "A", 100, 200);
        var b = BareNode(2, "B", 500, 300);
        // Bare card: (X-8, Y, 308, 120).
        var (x, y, w, h) = CanvasGroupViewModel.FrameAroundBounds(
            new[] { a, b }.Select(n =>
                (n.X - 8.0, n.Y, NodeGraphViewModel.NodeCardWidth, n.NodeCardHeight)),
            padding: 48);
        Assert.Equal(100 - 8 - 48, x);
        Assert.Equal(200 - 48, y);
        Assert.Equal((500 - 8 + 308 - (100 - 8)) + 96, w);
        Assert.Equal((300 + 120 - 200) + 96, h);
    }

    [Fact]
    public void Group_FrameAroundNodes_EmptyAndClamped()
    {
        var (x, y, w, h) = CanvasGroupViewModel.FrameAroundBounds(
            Enumerable.Empty<(double, double, double, double)>());
        Assert.Equal(0, x);
        Assert.Equal(0, y);
        Assert.Equal(CanvasGroupViewModel.MinWidth, w);
        Assert.Equal(CanvasGroupViewModel.MinHeight, h);

        var group = new CanvasGroupViewModel("g1", "T", "#fff", 0, 0, 10, 5);
        Assert.Equal(CanvasGroupViewModel.MinWidth, group.Width);
        Assert.Equal(CanvasGroupViewModel.MinHeight, group.Height);
        Assert.True(group.Contains(5, 5));
        Assert.False(group.Contains(5000, 5));
        Assert.True(group.Intersects(-1000, -1000, 2000, 2000));
        Assert.False(group.Intersects(5000, 5000, 100, 100));
    }

    [Fact]
    public void Group_MoveGroup_TranslatesMembersOnly()
    {
        var nodes = new ObservableCollection<NodeViewModel>
        {
            BareNode(1, "A", 0, 0),
            BareNode(2, "B", 400, 0),
            BareNode(3, "Outsider", 2000, 2000),
        };
        var service = new GroupService();
        service.Attach(nodes);
        var group = service.CreateFromNodes("Act", "#3B82F6",
            new[] { nodes[0], nodes[1] });

        int moved = service.MoveGroup(group.GroupId, 50, -25);

        Assert.Equal(2, moved);
        Assert.Equal(50, nodes[0].X);
        Assert.Equal(-25, nodes[0].Y);
        Assert.Equal(450, nodes[1].X);
        Assert.Equal(2000, nodes[2].X);
        Assert.Equal(2000, nodes[2].Y);
        Assert.Equal(0, service.MoveGroup("missing", 10, 10));
        Assert.True(service.ResizeGroup(group.GroupId, 10, 10));
        Assert.Equal(CanvasGroupViewModel.MinWidth, group.Width);
        Assert.Equal(CanvasGroupViewModel.MinHeight, group.Height);
    }

    [Fact]
    public void Group_FrameToMembers_RetightensAroundLiveMembers()
    {
        var nodes = new ObservableCollection<NodeViewModel>
        {
            BareNode(1, "A", 0, 0),
            BareNode(2, "B", 1000, 1000),
        };
        var service = new GroupService();
        service.Attach(nodes);
        var group = service.Create("Loose", "#fff", -5000, -5000, 200, 200);
        group.MemberNodeIds.Add(1);
        group.MemberNodeIds.Add(2);
        group.MemberNodeIds.Add(999); // dangling id is ignored, not fatal

        Assert.True(service.FrameToMembers(group.GroupId));
        Assert.Equal(0 - 8 - 48, group.X);
        Assert.True(group.Width >= 1000);
        Assert.Single(service.GroupsOf(1));
        Assert.Empty(service.GroupsOf(7712));
        Assert.Empty(service.MembersOf("missing"));
        Assert.True(service.Delete(group.GroupId));
        Assert.False(service.Delete(group.GroupId));
    }

    [Fact]
    public void Group_MetadataOnly_SaveKeepsRuntimeSemantics()
    {
        var a = BareNode(1, "A", 0, 0);
        var b = BareNode(2, "B", 400, 0);
        var nodes = new[] { a, b };
        var conns = new[] { new ConnectionViewModel(a, b) };

        string plain = StoryGraphSerializer.SerializeFullStoryGraph(nodes, conns, 1);
        var structure = new GraphStructureDocument();
        structure.Groups.Add(new CanvasGroup("g1", "Act", "#3B82F6", -56, -48, 712, 264,
            new List<ulong> { 1, 2 }));
        string grouped = StoryGraphSerializer.SerializeFullStoryGraph(nodes, conns, 1, structure);

        using var plainDoc = JsonDocument.Parse(plain);
        using var groupedDoc = JsonDocument.Parse(grouped);
        Assert.Equal(4, plainDoc.RootElement.GetProperty("format_version").GetInt32());
        Assert.Equal(5, groupedDoc.RootElement.GetProperty("format_version").GetInt32());
        // Runtime payload (node ids + edges) is identical with/without groups.
        Assert.Equal(
            plainDoc.RootElement.GetProperty("nodes").GetArrayLength(),
            groupedDoc.RootElement.GetProperty("nodes").GetArrayLength());
        Assert.Equal(
            plainDoc.RootElement.GetProperty("nodes")[0].GetProperty("next_nodes").ToString(),
            groupedDoc.RootElement.GetProperty("nodes")[0].GetProperty("next_nodes").ToString());
        Assert.False(plainDoc.RootElement.TryGetProperty("groups", out _));
        Assert.Equal("g1", groupedDoc.RootElement.GetProperty("groups")[0].GetProperty("id").GetString());
    }

    // ── Group culling + scope ────────────────────────────────────

    [Fact]
    public void Group_Culling_ParityWithBruteForce()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        for (ulong i = 1; i <= 40; i++)
            vm.Nodes.Add(BareNode(i, "N" + i, (i % 8) * 400, (i / 8) * 300));
        vm.Groups.CreateFromNodes("Row0", "#3B82F6", vm.Nodes.Take(8));
        vm.Groups.CreateFromNodes("Row4", "#10B981", vm.Nodes.Skip(32));
        vm.Groups.Create("Far", "#fff", 50000, 50000, 400, 300);

        var graph = vm.NodeGraphViewModel;
        graph.ViewportWidth = 1280;
        graph.ViewportHeight = 800;
        var pans = new (double x, double y, double zoom)[]
        {
            (0, 0, 1.0), (-800, -300, 1.0), (-2000, -900, 0.5), (0, 0, 2.0),
        };
        foreach (var (px, py, zoom) in pans)
        {
            vm.PanX = px;
            vm.PanY = py;
            vm.ZoomScale = zoom;
            graph.RefreshVisible();
            var view = graph.ViewportRect;
            double ex = view.X - NodeGraphViewModel.CullMargin;
            double ey = view.Y - NodeGraphViewModel.CullMargin;
            double ew = view.Width + NodeGraphViewModel.CullMargin * 2;
            double eh = view.Height + NodeGraphViewModel.CullMargin * 2;
            var expected = vm.Groups.Groups
                .Where(g => g.Intersects(ex, ey, ew, eh))
                .Select(g => g.GroupId)
                .ToList();
            Assert.Equal(expected, graph.VisibleGroups.Select(g => g.GroupId).ToList());
        }
    }

    [Fact]
    public void Group_ScopeGating_HidesFullyScopedOutFrames()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        var a = BareNode(1, "A", 0, 0);
        var b = BareNode(2, "B", 400, 0);
        vm.Nodes.Add(a);
        vm.Nodes.Add(b);
        a.ChapterId = "ch1";
        b.ChapterId = "ch2";
        vm.Groups.CreateFromNodes("Ch2Frame", "#fff", new[] { b });
        vm.Groups.Create("Empty", "#fff", 0, 0, 200, 200);
        var graph = vm.NodeGraphViewModel;
        graph.ViewportWidth = 4000;
        graph.ViewportHeight = 4000;
        graph.RefreshVisible();
        Assert.Equal(2, graph.VisibleGroups.Count);

        Assert.True(vm.Subgraphs.SetChapterFilter("ch1",
            new List<ChapterDefinition> { new("ch1", "One", 0, string.Empty, null) }));
        graph.RefreshVisible();
        // Ch2Frame's only live member is scoped out; the empty frame stays.
        Assert.Single(graph.VisibleGroups);
        Assert.Equal("Empty", graph.VisibleGroups[0].Title);
        vm.Subgraphs.GoToRoot();
    }

    // ── Subgraph navigation ──────────────────────────────────────

    private static SubgraphDefinition TrialDef() => new(
        "sg1", "Trial", 1, new List<ulong> { 3 }, new List<ulong> { 1, 2, 3 });

    [Fact]
    public void Subgraph_DepthStack_AndBreadcrumbs()
    {
        var nav = new SubgraphNavigationService();
        nav.LoadDefinitions(new[]
        {
            TrialDef(),
            new SubgraphDefinition("sg2", "Inner", 2, new List<ulong> { 2 }, new List<ulong> { 2 }),
        });
        Assert.False(nav.Enter("missing"));
        Assert.True(nav.Enter("sg1"));
        Assert.True(nav.Enter("sg2"));
        Assert.Equal(2, nav.Depth);
        Assert.Equal(
            new[] { "Root", "Trial", "Inner" },
            nav.Crumbs.Select(c => c.Title).ToList());
        Assert.True(nav.GoToDepth(1));
        Assert.Equal("sg1", nav.CurrentSubgraphId);
        Assert.False(nav.GoToDepth(9));
        Assert.True(nav.Exit());
        Assert.Null(nav.CurrentSubgraphId);
        Assert.False(nav.Exit());
        Assert.True(nav.TryEnterForNode(3));
        Assert.Equal("sg1", nav.CurrentSubgraphId);
        Assert.False(nav.TryEnterForNode(4242));
        Assert.True(nav.TryGetEntry("sg1", out ulong entry) && entry == 1);
        Assert.Equal(new[] { 3UL }, nav.GetExits("sg1"));
        Assert.False(nav.TryGetEntry("missing", out _));
        Assert.Empty(nav.GetExits("missing"));
    }

    [Fact]
    public void Subgraph_Breadcrumb_IncludesChapterSegment()
    {
        var nav = new SubgraphNavigationService();
        nav.LoadDefinitions(new[] { TrialDef() });
        nav.SetAvailableChapters(new[]
        {
            new ChapterDefinition("ch1", "Chapter 1", 0, string.Empty, null),
        });
        Assert.True(nav.SetChapterFilter("ch1",
            new List<ChapterDefinition> { new("ch1", "Chapter 1", 0, string.Empty, null) }));
        Assert.True(nav.Enter("sg1"));
        Assert.Equal(
            new[] { "Root", "Chapter 1", "Trial" },
            nav.Crumbs.Select(c => c.Title).ToList());
        // Chapter segment reuses stack depth 0 (filter kept, depth cleared).
        Assert.Equal(
            new[] { "root", "chapter", "subgraph" },
            nav.Crumbs.Select(c => c.Kind).ToList());
        Assert.Equal(new[] { 0, 0, 1 }, nav.Crumbs.Select(c => c.Depth).ToList());
        nav.GoToRoot();
        Assert.Single(nav.Crumbs);
        Assert.Equal("Root", nav.Crumbs[0].Title);
    }

    [Fact]
    public void Subgraph_RootScope_HidesInteriorKeepsBoundaryAndWires()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        // sg1 = {1(entry), 2(interior), 3(exit)}; 4 is outside.
        foreach (ulong i in new ulong[] { 1, 2, 3, 4 })
            vm.Nodes.Add(BareNode(i, "N" + i, (i - 1) * 350, 0));
        var byId = vm.Nodes.ToDictionary(n => n.Id);
        vm.Connections.Add(new ConnectionViewModel(byId[1], byId[2]));
        vm.Connections.Add(new ConnectionViewModel(byId[2], byId[3]));
        vm.Connections.Add(new ConnectionViewModel(byId[3], byId[4]));
        vm.Subgraphs.LoadDefinitions(new[] { TrialDef() });
        var graph = vm.NodeGraphViewModel;
        graph.ViewportWidth = 4000;
        graph.ViewportHeight = 4000;
        graph.RefreshVisible();

        Assert.Equal(new[] { 1UL, 3UL, 4UL }, graph.VisibleNodes.Select(n => n.Id).ToList());
        // Only the boundary-crossing exit→outside wire survives at root.
        Assert.Single(graph.VisibleConnections);
        Assert.Equal(3UL, graph.VisibleConnections[0].SourceNode!.Id);
        Assert.Equal(4UL, graph.VisibleConnections[0].TargetNode!.Id);

        Assert.True(vm.EnterSubgraphForNode(2));
        graph.RefreshVisible();
        Assert.Equal(new[] { 1UL, 2UL, 3UL }, graph.VisibleNodes.Select(n => n.Id).ToList());
        Assert.Equal(2, graph.VisibleConnections.Count);
        vm.Subgraphs.GoToRoot();
        graph.RefreshVisible();
        // Back at root the interior member hides again; boundary + outside stay.
        Assert.Equal(new[] { 1UL, 3UL, 4UL }, graph.VisibleNodes.Select(n => n.Id).ToList());
    }

    [Fact]
    public void Subgraph_PortDiscipline_InvalidBoundaryEdgesFailValidation()
    {
        // sg1 = {1(entry), 2(interior), 3(exit)}; 4 is an outsider.
        var nodes = new List<NodeViewModel>
        {
            BareNode(1, "A"), BareNode(2, "B"), BareNode(3, "C"), BareNode(4, "D"),
        };
        var byId = nodes.ToDictionary(n => n.Id);
        var structure = new GraphStructureDocument();
        structure.Subgraphs.Add(TrialDef());

        // Illegal: outsider 4 enters the member set at non-entry node 2.
        var illegal = new List<ConnectionViewModel> { new(byId[4], byId[2]) };
        var issues = GraphStructureValidator.Validate(nodes, illegal, structure);
        Assert.Contains(issues, i => i.IsError && i.Message.Contains("non-entry"));

        // Legal: outside→entry, internal chain, exit→outside (exit reachable).
        var legal = new List<ConnectionViewModel>
        {
            new(byId[4], byId[1]), new(byId[1], byId[2]),
            new(byId[2], byId[3]), new(byId[3], byId[4]),
        };
        var clean = GraphStructureValidator.Validate(nodes, legal, structure);
        Assert.DoesNotContain(clean, i => i.IsError);
    }

    [Fact]
    public void Subgraph_Badges_SetOnNodesWithoutDirtyingSearch()
    {
        using var shell = new SliceShell();
        var vm = shell.Vm;
        var a = BareNode(1, "Gate", 0, 0);
        vm.Nodes.Add(a);
        vm.Nodes.Add(BareNode(2, "Mid", 400, 0));
        vm.Nodes.Add(BareNode(3, "Out", 800, 0));
        var search = new NodeSearchService();
        search.Attach(vm.Nodes);
        Assert.Single(search.Search("gate").MatchingNodeIds);

        vm.Subgraphs.LoadDefinitions(new[] { TrialDef() });

        Assert.True(a.IsSubgraphBoundary);
        Assert.Equal("Trial", a.SubgraphBadge);
        Assert.Equal("Trial", vm.Nodes[2].SubgraphBadge);
        Assert.False(vm.Nodes[1].IsSubgraphBoundary);
        Assert.Equal(string.Empty, vm.Nodes[1].SubgraphBadge);
        // Badge-only props never dirty the index: the hit survives untouched.
        Assert.Single(search.Search("gate").MatchingNodeIds);
    }

    // ── Chapters ─────────────────────────────────────────────────

    private const string TwoChapterGraph = """
        {
          "format_version": 5,
          "start_node_id": 1,
          "nodes": [
            {"id": 1, "title": "A", "editor_x": 0, "editor_y": 0, "objects": [], "next_nodes": [{"id": 3, "label": "", "option_id": ""}], "chapter_id": "ch1"},
            {"id": 2, "title": "B", "editor_x": 400, "editor_y": 0, "objects": [], "next_nodes": [], "chapter_id": "ch1"},
            {"id": 3, "title": "C", "editor_x": 800, "editor_y": 0, "objects": [], "next_nodes": [], "chapter_id": "ch2"}
          ],
          "groups": [{"id": "g1", "title": "Act", "color": "#3B82F6", "x": -56, "y": -48, "width": 900, "height": 264, "node_ids": [1, 2]}],
          "subgraphs": [{"id": "sg1", "title": "Trial", "entry_node_id": 1, "exit_node_ids": [2], "node_ids": [1, 2]}],
          "chapters": [
            {"id": "ch1", "title": "Arrivals", "order": 0, "summary": "Meet.", "start_node_id": 1},
            {"id": "ch2", "title": "Departure", "order": 1, "summary": "Leave."}
          ]
        }
        """;

    [Fact]
    public void Chapter_SplitMerge_RoundTrip()
    {
        string dir = Path.Combine(Path.GetTempPath(), "RowlCh_" + Guid.NewGuid().ToString("N"));
        try
        {
            var written = ChapterStorageService.Split(TwoChapterGraph, dir);
            Assert.Equal(new[] { "ch1", "ch2" }, written.ToList());
            Assert.True(File.Exists(Path.Combine(dir, "chapter_index.json")));
            Assert.True(File.Exists(Path.Combine(dir, "ch1.json")));
            Assert.True(File.Exists(Path.Combine(dir, "ch2.json")));

            string merged = ChapterStorageService.Merge(dir);
            using var before = JsonDocument.Parse(TwoChapterGraph);
            using var after = JsonDocument.Parse(merged);
            Assert.Equal(5, after.RootElement.GetProperty("format_version").GetInt32());
            Assert.Equal(1, after.RootElement.GetProperty("start_node_id").GetInt32());
            Assert.Equal(
                before.RootElement.GetProperty("nodes").EnumerateArray().Select(n => n.GetProperty("id").GetUInt64()).ToList(),
                after.RootElement.GetProperty("nodes").EnumerateArray().Select(n => n.GetProperty("id").GetUInt64()).ToList());
            // Cross-chapter edge 1→3 survives the file boundary.
            Assert.Contains(
                after.RootElement.GetProperty("nodes").EnumerateArray(),
                n => n.GetProperty("id").GetUInt64() == 1 &&
                     n.GetProperty("next_nodes").EnumerateArray().Any(e => e.GetProperty("id").GetUInt64() == 3));
            Assert.Equal(2, after.RootElement.GetProperty("chapters").GetArrayLength());
            Assert.Equal("g1", after.RootElement.GetProperty("groups")[0].GetProperty("id").GetString());
            Assert.Equal("sg1", after.RootElement.GetProperty("subgraphs")[0].GetProperty("id").GetString());
            Assert.Equal(
                before.RootElement.GetProperty("nodes")[0].GetProperty("chapter_id").GetString(),
                after.RootElement.GetProperty("nodes")[0].GetProperty("chapter_id").GetString());

            var (chapterId, payload) = ChapterStorageService.LoadChapterFile(Path.Combine(dir, "ch2.json"));
            Assert.Equal("ch2", chapterId);
            Assert.Equal(3UL, payload[0]!.AsObject()["id"]!.GetValue<ulong>());
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
        }
    }

    [Fact]
    public void Chapter_Split_PromotesLegacyV4ToDefaultChapter()
    {
        const string v4 = """
            {"format_version": 4, "start_node_id": 7,
             "nodes": [{"id": 7, "title": "Solo", "editor_x": 0, "editor_y": 0, "objects": [], "next_nodes": []}]}
            """;
        string dir = Path.Combine(Path.GetTempPath(), "RowlChV4_" + Guid.NewGuid().ToString("N"));
        try
        {
            var written = ChapterStorageService.Split(v4, dir);
            Assert.Equal(new[] { ChapterStorageService.DefaultChapterId }, written.ToList());
            string merged = ChapterStorageService.Merge(dir);
            using var doc = JsonDocument.Parse(merged);
            Assert.Equal(5, doc.RootElement.GetProperty("format_version").GetInt32());
            Assert.Equal(ChapterStorageService.DefaultChapterId,
                doc.RootElement.GetProperty("chapters")[0].GetProperty("id").GetString());
            Assert.Single(doc.RootElement.GetProperty("nodes").EnumerateArray());

            // …while a pure legacy file still loads as one implicit chapter.
            var service = new ChapterStorageService();
            var effective = service.EffectiveChapters(new string?[] { null, "" });
            Assert.Single(effective);
            Assert.Equal(ChapterStorageService.DefaultChapterId, effective[0].Id);
            Assert.Equal(
                new ulong[] { 7 },
                ChapterStorageService.NodeIdsOfChapter(
                    new List<(ulong, string)> { (7, string.Empty) }, string.Empty));
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
        }
    }

    [Fact]
    public void Chapter_Merge_RejectsDuplicateNodeIds()
    {
        string dir = Path.Combine(Path.GetTempPath(), "RowlChDup_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "chapter_index.json"),
                """{"format_version":5,"node_order":[1],"chapters":[{"id":"a","title":"A","order":0,"summary":""}]}""");
            File.WriteAllText(Path.Combine(dir, "a.json"),
                """{"format_version":5,"chapter_id":"a","nodes":[{"id":1,"title":"X"}]}""");
            File.WriteAllText(Path.Combine(dir, "b.json"),
                """{"format_version":5,"chapter_id":"b","nodes":[{"id":1,"title":"Y"}]}""");
            Assert.Throws<InvalidOperationException>(() => ChapterStorageService.Merge(dir));
            // A missing chapter file surfaces the IO error, never an empty chapter.
            Assert.ThrowsAny<IOException>(() =>
                ChapterStorageService.LoadChapterFile(Path.Combine(dir, "missing.json")));
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch (Exception) { }
        }
    }

    // ── Snapshot persistence + end-to-end ────────────────────────

    [Fact]
    public void Snapshot_PersistsChapterTagsAndStructure()
    {
        var a = BareNode(1, "A", 0, 0);
        a.ChapterId = "ch1";
        a.ColorTag = "red";
        a.Tags.Add("finale");
        var b = BareNode(2, "B", 400, 0);
        var nodes = new ObservableCollection<NodeViewModel> { a, b };
        var conns = new List<ConnectionViewModel> { new(a, b) };
        var structure = new GraphStructureDocument();
        structure.Groups.Add(new CanvasGroup("g1", "Act", "#fff", 0, 0, 800, 300,
            new List<ulong> { 1, 2 }));
        structure.Chapters.Add(new ChapterDefinition("ch1", "One", 0, string.Empty, 1));

        var snapshot = StoryGraphSaveService.Capture(nodes, conns, 1, a, structure);
        using var doc = JsonDocument.Parse(StoryGraphSaveService.SerializeFullGraph(snapshot));
        var root = doc.RootElement;
        Assert.Equal(5, root.GetProperty("format_version").GetInt32());
        Assert.Equal("ch1", root.GetProperty("nodes")[0].GetProperty("chapter_id").GetString());
        Assert.Equal("red", root.GetProperty("nodes")[0].GetProperty("metadata").GetProperty("color_tag").GetString());
        Assert.Equal("g1", root.GetProperty("groups")[0].GetProperty("id").GetString());
        Assert.Equal("ch1", root.GetProperty("chapters")[0].GetProperty("id").GetString());
        Assert.False(root.GetProperty("nodes")[1].TryGetProperty("chapter_id", out _));
        Assert.False(root.GetProperty("nodes")[1].TryGetProperty("metadata", out _));

        // Untagged/unassigned graphs stay byte-stable v4.
        var plain = StoryGraphSaveService.Capture(
            new ObservableCollection<NodeViewModel> { BareNode(9, "P") },
            new List<ConnectionViewModel>(), 9, null);
        using var plainDoc = JsonDocument.Parse(StoryGraphSaveService.SerializeFullGraph(plain));
        Assert.Equal(4, plainDoc.RootElement.GetProperty("format_version").GetInt32());
        Assert.False(plainDoc.RootElement.TryGetProperty("groups", out _));
    }

    [Fact]
    public void MainVM_SaveLoad_RestoresGroupsSubgraphsChaptersAndScope()
    {
        // NOTE: file IO uses explicit temp dirs (never the static project
        // root): other test collections mutate that root in parallel.
        string root = Path.Combine(Path.GetTempPath(), "RowlSlice3IO_" + Guid.NewGuid().ToString("N"));
        string assetsDir = Path.Combine(root, "Assets");
        string jsonDir = Path.Combine(assetsDir, "json");
        try
        {
            using var shell = new SliceShell();
            var vm = shell.Vm;
            var a = BareNode(1, "A", 0, 0);
            var b = BareNode(2, "B", 400, 0);
            a.ChapterId = "ch1";
            b.ChapterId = "ch1";
            vm.Nodes.Add(a);
            vm.Nodes.Add(b);
            vm.Connections.Add(new ConnectionViewModel(a, b));
            vm.Groups.CreateFromNodes("Act", "#3B82F6", vm.Nodes);
            vm.Subgraphs.LoadDefinitions(new[]
            {
                new SubgraphDefinition("sg1", "Trial", 1, new List<ulong> { 2 }, new List<ulong> { 1, 2 }),
            });
            vm.Chapters.LoadDefinitions(new[]
            {
                new ChapterDefinition("ch1", "Arrivals", 0, string.Empty, 1),
            });

            Assert.True(StoryGraphDocumentWriter.SaveFullStoryGraph(
                assetsDir, jsonDir, vm.Nodes, vm.Connections, 1, null, vm.CurrentStructure()));
            Assert.True(StoryGraphDocumentReader.TryRead(
                assetsDir, jsonDir, out var document, out _, out var readError),
                readError ?? "read failed");
            StoryGraphLoadResult loaded;
            using (document!)
                loaded = StoryGraphLoaderService.Load(document);
            Assert.True(loaded.Success, loaded.ErrorMessage ?? "load failed");

            vm.Nodes.Clear();
            vm.Connections.Clear();
            vm.Groups.Clear();
            vm.Subgraphs.Clear();
            vm.Chapters.Clear();
            foreach (var node in loaded.Nodes)
                vm.Nodes.Add(node);
            foreach (var connection in loaded.Connections)
                vm.Connections.Add(connection);
            vm.ApplyLoadedStructure(loaded.Structure);

            Assert.Equal(2, vm.Nodes.Count);
        Assert.Single(vm.Groups.Groups);
        Assert.Equal("Act", vm.Groups.Groups[0].Title);
        Assert.Equal(2, vm.Groups.Groups[0].MemberNodeIds.Count);
        Assert.Single(vm.Subgraphs.Definitions);
        Assert.Single(vm.Chapters.Definitions);
        // No filter active: crumbs show root only; the chapter is offered.
        Assert.Equal(new[] { "Root" },
            vm.Subgraphs.Crumbs.Select(c => c.Title).ToList());
        Assert.Contains("Arrivals", vm.Subgraphs.AvailableChapterOptions);

            // Scope + culling cooperate end to end: inside sg1 both members show.
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 4000;
            graph.ViewportHeight = 4000;
            Assert.True(vm.EnterSubgraphForNode(2));
            graph.RefreshVisible();
            Assert.Equal(2, graph.VisibleNodes.Count);
            Assert.Single(graph.VisibleConnections);
            Assert.Single(graph.VisibleGroups);
        }
        finally
        {
            try { Directory.Delete(root, true); } catch (Exception) { }
        }
    }
}
