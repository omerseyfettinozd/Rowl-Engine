using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services.Search;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// Faz 4 Dilim 2 — global search + filter bar owner. Wraps
    /// <see cref="NodeSearchService"/> (attached to the main node/connection
    /// collections) and adds result ranking display, kind/speaker/color-tag
    /// filters, jump-to-node focus (select + PanTo + temporary highlight)
    /// and canvas dimming for filtered-out nodes. Kept out of
    /// <see cref="MainWindowViewModel"/> and <c>EngineHost</c> by design:
    /// the main VM only delegates its search box text here.
    /// </summary>
    public partial class SearchViewModel : ObservableObject
    {
        public const string AllLabel = "Tümü";

        /// <summary>Dimmed opacity for nodes excluded by the active filter.</summary>
        public const double FilteredOutOpacity = 0.25;

        private static readonly IReadOnlyList<string> KindOptions = new[]
        {
            AllLabel, "dialogue", "choice", "condition", "variable",
            "script", "transition", "camera", "audio", "jump",
        };

        private readonly MainWindowViewModel _main;
        private readonly NodeSearchService _service = new();

        private string _query = string.Empty;
        private bool _suppressRefresh;
        private bool _suppressJump;
        private NodeViewModel? _highlightedNode;
        private int _highlightGeneration;

        public SearchViewModel(MainWindowViewModel main)
        {
            _main = main ?? throw new ArgumentNullException(nameof(main));
            _service.Attach(main.Nodes, main.Connections);
            AvailableNodeKinds = new ObservableCollection<string>(KindOptions);
            AvailableSpeakers = new ObservableCollection<string>(new[] { AllLabel });
            AvailableColorTags = new ObservableCollection<string>(new[] { AllLabel });
            Results = new ObservableCollection<NodeSearchHit>();
        }

        public ObservableCollection<NodeSearchHit> Results { get; }

        public ObservableCollection<string> AvailableNodeKinds { get; }

        public ObservableCollection<string> AvailableSpeakers { get; }

        public ObservableCollection<string> AvailableColorTags { get; }

        /// <summary>Inspector assignment dropdown (none + palette).</summary>
        public IReadOnlyList<ColorTagOption> ColorTagAssignmentOptions { get; } =
            NodeColorTags.AssignmentOptions;

        [ObservableProperty]
        private NodeSearchHit? _selectedResult;

        partial void OnSelectedResultChanged(NodeSearchHit? value)
        {
            if (_suppressJump || value is null)
                return;
            JumpTo(value.Node);
        }

        [ObservableProperty]
        private string _selectedNodeKind = AllLabel;

        partial void OnSelectedNodeKindChanged(string value) => Refresh();

        [ObservableProperty]
        private string _selectedSpeaker = AllLabel;

        partial void OnSelectedSpeakerChanged(string value) => Refresh();

        [ObservableProperty]
        private string _selectedColorTag = AllLabel;

        partial void OnSelectedColorTagChanged(string value) => Refresh();

        [ObservableProperty]
        private string _statusText = string.Empty;

        [ObservableProperty]
        private double _lastQueryMs;

        [ObservableProperty]
        private int _totalCount;

        /// <summary>Live query text mirrored from the main search box.</summary>
        public string Query => _query;

        /// <summary>Search index size (tests + status).</summary>
        public int IndexedNodeCount => _service.IndexedNodeCount;

        public void SetQuery(string? query)
        {
            _query = query ?? string.Empty;
            Refresh();
        }

        public void RebuildIndex() => _service.Rebuild();

        /// <summary>Re-runs the query + filters and refreshes results, options and dimming.</summary>
        public void Refresh()
        {
            if (_suppressRefresh)
                return;
            var filter = new NodeSearchFilter
            {
                NodeKind = SelectedNodeKind == AllLabel ? null : SelectedNodeKind,
                Speaker = SelectedSpeaker == AllLabel ? null : SelectedSpeaker,
                ColorTag = SelectedColorTag == AllLabel ? null : SelectedColorTag,
            };
            NodeSearchResult result = _service.Search(_query, filter);
            LastQueryMs = result.ElapsedMs;
            TotalCount = result.TotalCount;

            NodeViewModel? keep = SelectedResult?.Node;
            _suppressJump = true;
            try
            {
                Results.Clear();
                foreach (NodeSearchHit hit in result.Hits)
                    Results.Add(hit);
                SelectedResult = keep is not null && result.MatchingNodeIds.Contains(keep.Id)
                    ? Results.FirstOrDefault(h => ReferenceEquals(h.Node, keep))
                    : null;
            }
            finally
            {
                _suppressJump = false;
            }

            ApplyDimming(result.MatchingNodeIds, !filter.IsEmpty || !string.IsNullOrWhiteSpace(_query));
            RefreshOptions();

            StatusText = result.TotalCount == 0 && (filter.IsEmpty && string.IsNullOrWhiteSpace(_query))
                ? string.Empty
                : result.TotalCount == 0
                    ? "Sonuç bulunamadı"
                    : $"{result.TotalCount} sonuç · {result.ElapsedMs:F1} ms";
        }

        /// <summary>Resets query, filters, selection, highlight and dimming.</summary>
        public void Clear()
        {
            _suppressRefresh = true;
            try
            {
                _query = string.Empty;
                SelectedNodeKind = AllLabel;
                SelectedSpeaker = AllLabel;
                SelectedColorTag = AllLabel;
            }
            finally
            {
                _suppressRefresh = false;
            }
            ClearHighlight();
            Refresh();
        }

        public void ClearHighlight()
        {
            _highlightGeneration++;
            if (_highlightedNode is not null)
            {
                _highlightedNode.IsSearchHighlighted = false;
                _highlightedNode = null;
            }
        }

        /// <summary>Moves the result selection (search-box Up/Down keys).</summary>
        public void MoveSelection(int delta)
        {
            if (Results.Count == 0)
                return;
            int at = SelectedResult is null ? -1 : Results.IndexOf(SelectedResult);
            int next = at < 0
                ? (delta > 0 ? 0 : Results.Count - 1)
                : (at + delta + Results.Count) % Results.Count;
            SelectedResult = Results[next];
        }

        [RelayCommand]
        public void JumpToSelected()
        {
            if (SelectedResult is not null)
                JumpTo(SelectedResult.Node);
            else if (Results.Count > 0)
                JumpTo(Results[0].Node);
        }

        /// <summary>
        /// Focuses a node: quiet-selects it (no save storm), centers the
        /// canvas on the card via the shared PanTo path and applies the
        /// temporary amber highlight.
        /// </summary>
        public void JumpTo(NodeViewModel? node)
        {
            if (node is null || !_main.Nodes.Contains(node))
                return;
            _main.SelectNodeQuiet(node);
            var (x, y, w, h) = NodeGraphViewModel.NodeBounds(node);
            _main.NodeGraphViewModel.PanTo(x + w / 2, y + h / 2);
            ApplyHighlight(node);
        }

        public void JumpTo(NodeSearchHit? hit) => JumpTo(hit?.Node);

        private void ApplyHighlight(NodeViewModel node)
        {
            if (!ReferenceEquals(_highlightedNode, node))
            {
                ClearHighlight();
                _highlightedNode = node;
                node.IsSearchHighlighted = true;
            }
            int generation = ++_highlightGeneration;
            _ = Task.Delay(1500).ContinueWith(_ =>
            {
                if (generation != _highlightGeneration || !ReferenceEquals(_highlightedNode, node))
                    return;
                try
                {
                    var dispatcher = Avalonia.Threading.Dispatcher.UIThread;
                    if (dispatcher.CheckAccess())
                        ClearHighlight();
                    else
                        dispatcher.Post(ClearHighlight);
                }
                catch
                {
                    ClearHighlight();
                }
            }, TaskScheduler.Default);
        }

        private void ApplyDimming(HashSet<ulong> matchingIds, bool filteringActive)
        {
            foreach (NodeViewModel node in _main.Nodes)
            {
                double wanted = !filteringActive || matchingIds.Contains(node.Id)
                    ? 1.0 : FilteredOutOpacity;
                if (Math.Abs(node.FilterOpacity - wanted) > 0.001)
                    node.FilterOpacity = wanted;
            }
        }

        private void RefreshOptions()
        {
            SyncOptions(AvailableSpeakers, _service.DistinctSpeakers(), SelectedSpeaker);
            var tags = new List<string> { AllLabel };
            tags.AddRange(_service.DistinctColorTags());
            foreach (string palette in NodeColorTags.PaletteNames)
            {
                if (!tags.Contains(palette))
                    tags.Add(palette);
            }
            // Keep the selected tag even when no node uses it anymore.
            if (SelectedColorTag != AllLabel && !tags.Contains(SelectedColorTag))
                tags.Add(SelectedColorTag);
            SyncOptions(AvailableColorTags, tags, SelectedColorTag);
        }

        private static void SyncOptions(
            ObservableCollection<string> target, IEnumerable<string> desired, string selected)
        {
            var wanted = desired.ToList();
            var wantedSet = new HashSet<string>(wanted);
            for (int i = target.Count - 1; i >= 0; i--)
            {
                if (!wantedSet.Contains(target[i]) && target[i] != selected)
                    target.RemoveAt(i);
            }
            foreach (string item in wanted)
            {
                if (!target.Contains(item))
                    target.Add(item);
            }
        }
    }
}
