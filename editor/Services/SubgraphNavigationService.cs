using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>One breadcrumb segment: root, chapter filter or subgraph depth.</summary>
    public sealed record NavCrumb(string Kind, string Id, string Title, int Depth);

    /// <summary>
    /// Faz 4 Dilim 3 — hierarchical subgraph navigation. Owns the subgraph
    /// definitions, the depth stack and the breadcrumb trail
    /// ("Root › Chapter › Subgraph…"), and computes the canvas scope filter:
    /// <list type="bullet">
    /// <item>At root every unsubgraphed node plus each subgraph's boundary
    /// (entry + exit) nodes are visible; interior-only members are hidden so
    /// each subgraph reads as a single entry/exit-pinned unit from outside.
    /// </item>
    /// <item>Inside a subgraph only its member set is visible.</item>
    /// <item>An optional chapter filter narrows any depth to one chapter.
    /// </item>
    /// </list>
    /// Navigation never mutates nodes, connections or runtime state; it only
    /// changes which subset the canvas materializes.
    /// </summary>
    public partial class SubgraphNavigationService : ObservableObject
    {
        public const string RootTitle = "Root";
        public const string AllChaptersLabel = "Tümü";

        private readonly List<SubgraphDefinition> _definitions = new();
        private readonly Dictionary<ulong, string> _memberToSubgraph = new();
        private readonly Dictionary<string, SubgraphDefinition> _byId = new(StringComparer.Ordinal);
        private readonly Dictionary<string, string> _chapterTitleToId = new(StringComparer.Ordinal);

        private ObservableCollection<NodeViewModel>? _nodes;

        /// <summary>Depth stack of entered subgraph ids (outermost first).</summary>
        public ObservableCollection<string> DepthStack { get; } = new();

        /// <summary>Breadcrumb trail bound by the canvas bar.</summary>
        public ObservableCollection<NavCrumb> Crumbs { get; } = new();

        /// <summary>Chapter filter option ("Tümü" = no filter).</summary>
        [ObservableProperty]
        private string _chapterFilterOption = AllChaptersLabel;

        /// <summary>Chapter options ("Tümü" + known titles), bound by the canvas bar.</summary>
        public ObservableCollection<string> AvailableChapterOptions { get; } =
            new(new[] { AllChaptersLabel });

        /// <summary>Raised whenever the visible scope changes (enter/exit/filter/load).</summary>
        public event EventHandler? ScopeChanged;

        /// <summary>Active chapter filter id (empty = all chapters).</summary>
        public string ChapterFilter { get; private set; } = string.Empty;

        /// <summary>Title of the active chapter filter (empty when unfiltered).</summary>
        public string ChapterFilterTitle { get; private set; } = string.Empty;

        public IReadOnlyList<SubgraphDefinition> Definitions => _definitions;

        public string? CurrentSubgraphId =>
            DepthStack.Count == 0 ? null : DepthStack[DepthStack.Count - 1];

        public int Depth => DepthStack.Count;

        public void Attach(ObservableCollection<NodeViewModel> nodes)
        {
            if (_nodes is not null)
                _nodes.CollectionChanged -= OnNodesChanged;
            _nodes = nodes;
            if (_nodes is not null)
                _nodes.CollectionChanged += OnNodesChanged;
            RefreshNodeBadges();
        }

        /// <summary>Replaces the known subgraph definitions (project load).</summary>
        public void LoadDefinitions(IEnumerable<SubgraphDefinition> definitions)
        {
            _definitions.Clear();
            _definitions.AddRange(definitions);
            RebuildMemberIndex();
            // A load invalidates the depth trail: stale depth is worse than
            // root. The chapter filter survives (chapter definitions arrive
            // through SetAvailableChapters on the same load path).
            DepthStack.Clear();
            RebuildCrumbs();
            RefreshNodeBadges();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
        }

        public void Clear()
        {
            _definitions.Clear();
            _memberToSubgraph.Clear();
            _byId.Clear();
            DepthStack.Clear();
            ChapterFilter = string.Empty;
            ChapterFilterTitle = string.Empty;
            ChapterFilterOption = AllChaptersLabel;
            RebuildCrumbs();
            RefreshNodeBadges();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>Known chapter titles for the filter dropdown.</summary>
        public void SetAvailableChapters(IEnumerable<ChapterDefinition> chapters)
        {
            var keep = ChapterFilterOption;
            AvailableChapterOptions.Clear();
            AvailableChapterOptions.Add(AllChaptersLabel);
            _chapterTitleToId.Clear();
            foreach (var chapter in chapters.OrderBy(c => c.Order).ThenBy(c => c.Id, StringComparer.Ordinal))
            {
                string label = chapter.Title.Length > 0 ? chapter.Title : chapter.Id;
                AvailableChapterOptions.Add(label);
                _chapterTitleToId[label] = chapter.Id;
            }
            if (!AvailableChapterOptions.Contains(keep))
            {
                ChapterFilterOption = AllChaptersLabel;
                ChapterFilter = string.Empty;
                ChapterFilterTitle = string.Empty;
            }
            RebuildCrumbs();
        }

        partial void OnChapterFilterOptionChanged(string value)
        {
            ApplyChapterOption(value);
        }

        /// <summary>Selects a chapter filter by id (empty clears). Unknown ids are ignored.</summary>
        public bool SetChapterFilter(string? chapterId, IReadOnlyList<ChapterDefinition>? known = null)
        {
            if (string.IsNullOrEmpty(chapterId))
            {
                ChapterFilter = string.Empty;
                ChapterFilterTitle = string.Empty;
                ChapterFilterOption = AllChaptersLabel;
                RebuildCrumbs();
                ScopeChanged?.Invoke(this, EventArgs.Empty);
                return true;
            }
            string? title = known?.FirstOrDefault(c =>
                string.Equals(c.Id, chapterId, StringComparison.Ordinal))?.Title;
            if (title is null)
                return false;
            ChapterFilter = chapterId;
            ChapterFilterTitle = title.Length > 0 ? title : chapterId;
            ChapterFilterOption = ChapterFilterTitle;
            if (!AvailableChapterOptions.Contains(ChapterFilterOption))
                AvailableChapterOptions.Add(ChapterFilterOption);
            RebuildCrumbs();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
            return true;
        }

        /// <summary>Enters a subgraph (pushes depth). Unknown ids are ignored.</summary>
        public bool Enter(string subgraphId)
        {
            if (!_byId.ContainsKey(subgraphId))
                return false;
            if (string.Equals(CurrentSubgraphId, subgraphId, StringComparison.Ordinal))
                return true;
            DepthStack.Add(subgraphId);
            RebuildCrumbs();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
            return true;
        }

        /// <summary>Enters the subgraph containing the node (any member, incl. entry).</summary>
        public bool TryEnterForNode(ulong nodeId)
        {
            if (_memberToSubgraph.TryGetValue(nodeId, out string? subgraphId))
                return Enter(subgraphId);
            return false;
        }

        /// <summary>Leaves the innermost subgraph. False at root.</summary>
        public bool Exit()
        {
            if (DepthStack.Count == 0)
                return false;
            DepthStack.RemoveAt(DepthStack.Count - 1);
            RebuildCrumbs();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
            return true;
        }

        public void GoToRoot()
        {
            if (DepthStack.Count == 0 && string.IsNullOrEmpty(ChapterFilter))
                return;
            DepthStack.Clear();
            ChapterFilter = string.Empty;
            ChapterFilterTitle = string.Empty;
            ChapterFilterOption = AllChaptersLabel;
            RebuildCrumbs();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>Navigates the breadcrumb to a depth (0 = root).</summary>
        public bool GoToDepth(int depth)
        {
            if (depth < 0 || depth > DepthStack.Count)
                return false;
            while (DepthStack.Count > depth)
                DepthStack.RemoveAt(DepthStack.Count - 1);
            RebuildCrumbs();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
            return true;
        }

        /// <summary>Subgraph owning the node (null when unsubgraphed).</summary>
        public SubgraphDefinition? OwnerOf(ulong nodeId) =>
            _memberToSubgraph.TryGetValue(nodeId, out string? id) &&
            _byId.TryGetValue(id, out var definition)
                ? definition
                : null;

        /// <summary>True for entry/exit (boundary) nodes of any subgraph.</summary>
        public bool IsBoundaryNode(ulong nodeId)
        {
            var owner = OwnerOf(nodeId);
            if (owner is null)
                return false;
            return owner.EntryNodeId == nodeId || owner.ExitNodeIds.Contains(nodeId);
        }

        /// <summary>Canvas badge for boundary nodes (null for interior/plain nodes).</summary>
        public string? GetBadge(ulong nodeId)
        {
            var owner = OwnerOf(nodeId);
            if (owner is null)
                return null;
            string title = owner.Title.Length > 0 ? owner.Title : owner.Id;
            if (owner.EntryNodeId == nodeId)
                return $"◧ {title}";
            if (owner.ExitNodeIds.Contains(nodeId))
                return $"{title} ⏏";
            return null;
        }

        public bool TryGetEntry(string subgraphId, out ulong entryNodeId)
        {
            entryNodeId = 0;
            if (!_byId.TryGetValue(subgraphId, out var definition))
                return false;
            entryNodeId = definition.EntryNodeId;
            return entryNodeId != 0;
        }

        public IReadOnlyList<ulong> GetExits(string subgraphId) =>
            _byId.TryGetValue(subgraphId, out var definition)
                ? definition.ExitNodeIds.ToList()
                : Array.Empty<ulong>();

        /// <summary>
        /// Scope predicate used by the culling layer: chapter filter first,
        /// then subgraph depth (root hides interior-only members).
        /// </summary>
        public bool IsNodeVisibleInScope(ulong nodeId, string chapterId)
        {
            if (!string.IsNullOrEmpty(ChapterFilter) &&
                !string.Equals(chapterId ?? string.Empty, ChapterFilter, StringComparison.Ordinal))
                return false;
            if (DepthStack.Count == 0)
            {
                // Root: plain nodes and boundary pins stay; interior hides.
                if (!_memberToSubgraph.ContainsKey(nodeId))
                    return true;
                return IsBoundaryNode(nodeId);
            }
            string current = DepthStack[DepthStack.Count - 1];
            return _memberToSubgraph.TryGetValue(nodeId, out string? owner) &&
                string.Equals(owner, current, StringComparison.Ordinal);
        }

        /// <summary>Refreshes the display-only subgraph props on every node.</summary>
        public void RefreshNodeBadges()
        {
            if (_nodes is null)
                return;
            foreach (var node in _nodes)
            {
                node.IsSubgraphBoundary = IsBoundaryNode(node.Id);
                node.SubgraphBadge = GetBadge(node.Id) ?? string.Empty;
            }
        }

        private void ApplyChapterOption(string option)
        {
            if (string.Equals(option, AllChaptersLabel, StringComparison.Ordinal) ||
                string.IsNullOrEmpty(option))
            {
                ChapterFilter = string.Empty;
                ChapterFilterTitle = string.Empty;
            }
            else if (_chapterTitleToId.TryGetValue(option, out string? mapped))
            {
                ChapterFilter = mapped;
                ChapterFilterTitle = option;
            }
            else
            {
                // No title mapping registered (tests / manual use): the option
                // doubles as the chapter id.
                ChapterFilter = option;
                ChapterFilterTitle = option;
            }
            RebuildCrumbs();
            ScopeChanged?.Invoke(this, EventArgs.Empty);
        }

        private void RebuildMemberIndex()
        {
            _memberToSubgraph.Clear();
            _byId.Clear();
            foreach (var definition in _definitions)
            {
                _byId[definition.Id] = definition;
                foreach (ulong member in definition.NodeIds)
                    _memberToSubgraph[member] = definition.Id;
            }
        }

        private void RebuildCrumbs()
        {
            // NavCrumb.Depth is always a SUBGRAPH stack depth for GoToDepth:
            // the optional chapter segment shares depth 0 (stack cleared,
            // filter kept); root additionally clears the chapter filter.
            Crumbs.Clear();
            Crumbs.Add(new NavCrumb("root", string.Empty, RootTitle, 0));
            if (!string.IsNullOrEmpty(ChapterFilter))
            {
                Crumbs.Add(new NavCrumb("chapter", ChapterFilter,
                    string.IsNullOrEmpty(ChapterFilterTitle) ? ChapterFilter : ChapterFilterTitle,
                    0));
            }
            int stackDepth = 0;
            foreach (string id in DepthStack)
            {
                stackDepth++;
                string title = _byId.TryGetValue(id, out var definition) &&
                    definition.Title.Length > 0
                        ? definition.Title
                        : id;
                Crumbs.Add(new NavCrumb("subgraph", id, title, stackDepth));
            }
            OnPropertyChanged(nameof(CurrentSubgraphId));
            OnPropertyChanged(nameof(Depth));
        }

        private void OnNodesChanged(object? sender,
            System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
        {
            RefreshNodeBadges();
        }
    }
}
