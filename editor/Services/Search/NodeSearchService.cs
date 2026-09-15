using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services.Search;

/// <summary>
/// Faz 4 Dilim 2 — owns the <see cref="NodeSearchIndex"/> for one graph and
/// keeps it fresh without full rebuilds. Collection add/remove is
/// incremental (O(1) per node); content edits mark that node dirty through
/// node + component subscriptions and refresh lazily on the next query.
/// Visual-only node properties (position, selection, highlight, dimming)
/// never dirty the index. No MainWindow/EngineHost types: attach any live
/// node/connection collections.
/// </summary>
public sealed class NodeSearchService
{
    private static readonly HashSet<string> VisualOnlyProperties = new(StringComparer.Ordinal)
    {
        nameof(NodeViewModel.X),
        nameof(NodeViewModel.Y),
        nameof(NodeViewModel.IsSelected),
        nameof(NodeViewModel.IsStartNode),
        nameof(NodeViewModel.BorderColor),
        nameof(NodeViewModel.FilterOpacity),
        nameof(NodeViewModel.IsSearchHighlighted),
        nameof(NodeViewModel.ColorTagBrush),
        nameof(NodeViewModel.HasColorTag),
        nameof(NodeViewModel.IsSubgraphBoundary),
        nameof(NodeViewModel.SubgraphBadge),
        nameof(NodeViewModel.NodeCardHeight),
        nameof(NodeViewModel.ComponentSummary),
        nameof(NodeViewModel.ChoiceDataChanged),
    };

    private readonly NodeSearchIndex _index = new();
    private readonly HashSet<NodeViewModel> _dirty = new();
    private readonly Dictionary<NodeViewModel, List<NodeComponentViewModel>> _trackedComponents = new();
    private readonly Dictionary<ulong, int> _outgoingCounts = new();

    private ObservableCollection<NodeViewModel>? _nodes;
    private ObservableCollection<ConnectionViewModel>? _connections;
    private bool _connectionsDirty;

    public int IndexedNodeCount => _index.Count;

    public void Attach(
        ObservableCollection<NodeViewModel> nodes,
        ObservableCollection<ConnectionViewModel>? connections = null)
    {
        Detach();
        _nodes = nodes ?? throw new ArgumentNullException(nameof(nodes));
        _nodes.CollectionChanged += OnNodesChanged;
        foreach (NodeViewModel node in _nodes)
            TrackNode(node);
        _connections = connections;
        if (_connections is not null)
            _connections.CollectionChanged += OnConnectionsChanged;
        RecomputeOutgoing();
        Rebuild();
    }

    public void Detach()
    {
        if (_nodes is not null)
            _nodes.CollectionChanged -= OnNodesChanged;
        if (_connections is not null)
            _connections.CollectionChanged -= OnConnectionsChanged;
        foreach (NodeViewModel node in _trackedComponents.Keys.ToList())
            UntrackNode(node);
        _nodes = null;
        _connections = null;
        _dirty.Clear();
        _outgoingCounts.Clear();
        _connectionsDirty = false;
        _index.Clear();
    }

    /// <summary>Full snapshot rebuild (load / reset paths).</summary>
    public void Rebuild()
    {
        _index.Clear();
        _dirty.Clear();
        RecomputeOutgoing();
        _connectionsDirty = false;
        if (_nodes is null)
            return;
        foreach (NodeViewModel node in _nodes)
        {
            TrackNode(node);
            _index.Upsert(NodeSearchDocument.Build(node, OutgoingOf(node)));
        }
    }

    /// <summary>Refreshes dirty documents, then runs the query.</summary>
    public NodeSearchResult Search(string? query, NodeSearchFilter? filter = null)
    {
        EnsureFresh();
        return _index.Search(query, filter);
    }

    /// <summary>Distinct speakers across the fresh index (filter bar).</summary>
    public IReadOnlyList<string> DistinctSpeakers()
    {
        EnsureFresh();
        var speakers = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        // Documents are internal; re-derive cheaply from tracked snapshots
        // through a filter-only search is wasteful, so walk live nodes once.
        if (_nodes is null)
            return Array.Empty<string>();
        foreach (NodeViewModel node in _nodes)
        {
            foreach (NodeComponentViewModel component in node.AllComponents)
            {
                if (component is DialogueComponentViewModel dialogue &&
                    !string.IsNullOrWhiteSpace(dialogue.Speaker))
                    speakers.Add(dialogue.Speaker.Trim());
            }
        }
        return speakers.ToList();
    }

    /// <summary>Distinct color tags in use (palette order first, then rest).</summary>
    public IReadOnlyList<string> DistinctColorTags()
    {
        EnsureFresh();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var ordered = new List<string>();
        if (_nodes is not null)
        {
            foreach (NodeViewModel node in _nodes)
            {
                if (!string.IsNullOrEmpty(node.ColorTag) && seen.Add(node.ColorTag))
                    ordered.Add(node.ColorTag);
                foreach (string tag in node.Tags)
                {
                    string? normalized = NodeColorTags.NormalizeListTag(tag);
                    if (normalized is not null && seen.Add(normalized))
                        ordered.Add(normalized);
                }
            }
        }
        ordered.Sort((a, b) =>
        {
            int pa = PaletteOrder(a);
            int pb = PaletteOrder(b);
            int byPalette = pa.CompareTo(pb);
            return byPalette != 0 ? byPalette : string.Compare(a, b, StringComparison.OrdinalIgnoreCase);
        });
        return ordered;
    }

    private static int PaletteOrder(string tag)
    {
        var names = NodeColorTags.PaletteNames;
        for (int i = 0; i < names.Count; i++)
        {
            if (string.Equals(names[i], tag, StringComparison.OrdinalIgnoreCase))
                return i;
        }
        return int.MaxValue;
    }

    private void EnsureFresh()
    {
        if (_connectionsDirty)
        {
            var before = new Dictionary<ulong, int>(_outgoingCounts);
            RecomputeOutgoing();
            _connectionsDirty = false;
            if (_nodes is not null)
            {
                foreach (NodeViewModel node in _nodes)
                {
                    before.TryGetValue(node.Id, out int oldCount);
                    if (oldCount != OutgoingOf(node))
                        _dirty.Add(node);
                }
            }
        }
        if (_dirty.Count == 0)
            return;
        foreach (NodeViewModel node in _dirty.ToList())
        {
            SyncComponentSubscriptions(node);
            _index.Upsert(NodeSearchDocument.Build(node, OutgoingOf(node)));
        }
        _dirty.Clear();
    }

    private int OutgoingOf(NodeViewModel node) =>
        _outgoingCounts.TryGetValue(node.Id, out int count) ? count : 0;

    private void RecomputeOutgoing()
    {
        _outgoingCounts.Clear();
        if (_connections is null)
            return;
        foreach (ConnectionViewModel connection in _connections)
        {
            if (connection.SourceNode is null || connection.TargetNode is null)
                continue;
            _outgoingCounts.TryGetValue(connection.SourceNode.Id, out int count);
            _outgoingCounts[connection.SourceNode.Id] = count + 1;
        }
    }

    private void TrackNode(NodeViewModel node)
    {
        if (node is null || _trackedComponents.ContainsKey(node))
            return;
        _trackedComponents[node] = new List<NodeComponentViewModel>();
        node.PropertyChanged += OnNodePropertyChanged;
        node.Tags.CollectionChanged += OnTagsChanged;
        SyncComponentSubscriptions(node);
    }

    private void UntrackNode(NodeViewModel node)
    {
        if (node is null || !_trackedComponents.TryGetValue(node, out var components))
            return;
        node.PropertyChanged -= OnNodePropertyChanged;
        node.Tags.CollectionChanged -= OnTagsChanged;
        foreach (NodeComponentViewModel component in components)
            component.PropertyChanged -= OnComponentPropertyChanged;
        _trackedComponents.Remove(node);
        _dirty.Remove(node);
        _index.Remove(node);
    }

    private void SyncComponentSubscriptions(NodeViewModel node)
    {
        if (!_trackedComponents.TryGetValue(node, out var tracked))
            return;
        var current = new HashSet<NodeComponentViewModel>(node.AllComponents);
        for (int i = tracked.Count - 1; i >= 0; i--)
        {
            if (!current.Contains(tracked[i]))
            {
                tracked[i].PropertyChanged -= OnComponentPropertyChanged;
                tracked.RemoveAt(i);
            }
        }
        foreach (NodeComponentViewModel component in current)
        {
            if (!tracked.Contains(component))
            {
                tracked.Add(component);
                component.PropertyChanged += OnComponentPropertyChanged;
            }
        }
    }

    private void OnNodesChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.Action == NotifyCollectionChangedAction.Reset)
        {
            foreach (NodeViewModel node in _trackedComponents.Keys.ToList())
                UntrackNode(node);
            if (sender is ObservableCollection<NodeViewModel> nodes)
            {
                foreach (NodeViewModel node in nodes)
                {
                    TrackNode(node);
                    _index.Upsert(NodeSearchDocument.Build(node, OutgoingOf(node)));
                }
            }
            _dirty.Clear();
            return;
        }
        if (e.OldItems is not null)
        {
            foreach (NodeViewModel node in e.OldItems)
                UntrackNode(node);
        }
        if (e.NewItems is not null)
        {
            foreach (NodeViewModel node in e.NewItems)
            {
                TrackNode(node);
                _index.Upsert(NodeSearchDocument.Build(node, OutgoingOf(node)));
            }
        }
    }

    private void OnConnectionsChanged(object? sender, NotifyCollectionChangedEventArgs e) =>
        _connectionsDirty = true; // recomputed lazily; jump kinds refresh then.

    private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is NodeViewModel node &&
            (string.IsNullOrEmpty(e.PropertyName) || !VisualOnlyProperties.Contains(e.PropertyName)))
            _dirty.Add(node);
    }

    private void OnComponentPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (sender is NodeComponentViewModel component && component.Node is not null)
            _dirty.Add(component.Node);
    }

    private void OnTagsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (sender is null)
            return;
        // Tags collections are per-node; find the owner through tracking.
        foreach (var entry in _trackedComponents)
        {
            if (ReferenceEquals(entry.Key.Tags, sender))
            {
                _dirty.Add(entry.Key);
                break;
            }
        }
    }
}
