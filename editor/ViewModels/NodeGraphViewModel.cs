using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// Faz 4 Dilim 1 — viewport culling owner. Indexes node bounds in a
    /// <see cref="CanvasSpatialIndex"/> and exposes diff-synced
    /// <see cref="VisibleNodes"/> / <see cref="VisibleConnections"/> so the
    /// canvas materializes Avalonia controls only for the visible rect.
    /// Pan/zoom/selection state stays on <see cref="MainWindowViewModel"/>
    /// (untouched); this type only observes it.
    /// </summary>
    public partial class NodeGraphViewModel : ViewModelBase
    {
        /// <summary>Node card width in canvas pixels (see NodeControl).</summary>
        public const double NodeCardWidth = 308.0;

        /// <summary>Viewport query margin: wire overshoot + pin handles.</summary>
        public const double CullMargin = 128.0;

        public const double MinZoom = 0.15;
        public const double MaxZoom = 4.0;

        public MainWindowViewModel MainViewModel { get; }

        private int _suspendCount;

        private readonly CanvasSpatialIndex _index = new();
        private readonly Dictionary<NodeViewModel, ulong> _keys = new();
        private readonly Dictionary<ulong, NodeViewModel> _nodesByKey = new();
        private readonly Dictionary<ulong, List<ConnectionViewModel>> _incident = new();
        private ulong _nextKey = 1;

        // ── Faz 4 Dilim 3 — groups & navigation scope ──
        private readonly CanvasSpatialIndex _groupIndex = new();
        private readonly Dictionary<CanvasGroupViewModel, ulong> _groupKeys = new();
        private readonly Dictionary<ulong, CanvasGroupViewModel> _groupsByKey = new();
        private ulong _nextGroupKey = 1;
        private ObservableCollection<CanvasGroupViewModel>? _groups;
        private readonly Dictionary<ulong, NodeViewModel> _nodeById = new();

        public NodeGraphViewModel(MainWindowViewModel main)
        {
            MainViewModel = main ?? throw new ArgumentNullException(nameof(main));
            MainViewModel.PropertyChanged += OnMainPropertyChanged;
            MainViewModel.Nodes.CollectionChanged += OnNodesChanged;
            MainViewModel.Connections.CollectionChanged += OnConnectionsChanged;
            foreach (var node in MainViewModel.Nodes)
                TrackNode(node);
            RebuildIncidentMap();
            RebuildIndex();
        }

        public ObservableCollection<NodeViewModel> Nodes => MainViewModel.Nodes;
        public ObservableCollection<ConnectionViewModel> Connections => MainViewModel.Connections;

        /// <summary>Culled projections bound by the canvas (diff-synced).</summary>
        public ObservableCollection<NodeViewModel> VisibleNodes { get; } = new();
        public ObservableCollection<ConnectionViewModel> VisibleConnections { get; } = new();

        /// <summary>
        /// Faz 4 Dilim 3 — culled group frames bound by the canvas layer
        /// behind the nodes (diff-synced, master order).
        /// </summary>
        public ObservableCollection<CanvasGroupViewModel> VisibleGroups { get; } = new();

        /// <summary>
        /// Faz 4 Dilim 3 — navigation scope predicate (subgraph depth +
        /// chapter filter). Null means every node passes (root, unfiltered).
        /// Owned by <c>SubgraphNavigationService</c>; this view model only
        /// applies it before culling.
        /// </summary>
        public Func<NodeViewModel, bool>? ScopePredicate { get; set; }

        /// <summary>Re-applies scope + culling (navigation changed depth).</summary>
        public void RefreshScope() => RefreshVisible();

        /// <summary>
        /// Attaches the session group frames for culling. Safe to call once;
        /// re-attaching swaps the subscription.
        /// </summary>
        public void AttachGroups(ObservableCollection<CanvasGroupViewModel> groups)
        {
            if (_groups is not null)
                _groups.CollectionChanged -= OnGroupsChanged;
            foreach (var tracked in _groupKeys.Keys.ToList())
                tracked.PropertyChanged -= OnGroupPropertyChanged;
            _groupKeys.Clear();
            _groupsByKey.Clear();
            _groupIndex.Clear();
            _groups = groups;
            if (_groups is not null)
            {
                _groups.CollectionChanged += OnGroupsChanged;
                foreach (var group in _groups)
                    TrackGroup(group);
            }
            if (_suspendCount == 0)
                RefreshVisible();
        }

        private double _viewportWidth = 1280.0;
        private double _viewportHeight = 800.0;

        /// <summary>Viewport size in screen pixels (pushed by the view).</summary>
        public double ViewportWidth
        {
            get => _viewportWidth;
            set
            {
                if (Math.Abs(_viewportWidth - value) < 0.5) return;
                _viewportWidth = value;
                RefreshVisible();
            }
        }

        /// <summary>Viewport size in screen pixels (pushed by the view).</summary>
        public double ViewportHeight
        {
            get => _viewportHeight;
            set
            {
                if (Math.Abs(_viewportHeight - value) < 0.5) return;
                _viewportHeight = value;
                RefreshVisible();
            }
        }

        /// <summary>Visible rect in canvas coordinates (minimap + tests).</summary>
        public Rect ViewportRect { get; private set; }

        /// <summary>
        /// Faz 6 Dilim 4: düğüm birleşimi + görünüm birleşimi (+64px pay).
        /// Görünüm de dünyaya dahil olduğu için uzaklaştırınca/pan yapınca
        /// minimap çerçeveyi içeride tutar. Boş grafta dejenere (0,0,0,0)
        /// korunur.
        /// </summary>
        public Rect WorldBounds { get; private set; }

        /// <summary>Düğüm taramasından gelen ham birleşim (görünümsüz).</summary>
        private Rect _nodeBounds;

        public static Rect ComputeViewportRect(
            double panX, double panY, double zoom, double viewportWidth, double viewportHeight)
        {
            double safeZoom = zoom > 0 ? zoom : 1.0;
            double width = viewportWidth > 0 ? viewportWidth / safeZoom : 0;
            double height = viewportHeight > 0 ? viewportHeight / safeZoom : 0;
            return new Rect(-panX / safeZoom, -panY / safeZoom, width, height);
        }

        /// <summary>
        /// Suspends index/visible refreshes across bulk loads. Tracking
        /// still runs (O(1) per item); one rebuild happens at the matching
        /// <see cref="EndBulkUpdate"/>. Nestable.
        /// </summary>
        public void BeginBulkUpdate() => _suspendCount++;

        public void EndBulkUpdate()
        {
            if (_suspendCount > 0 && --_suspendCount == 0)
                RebuildIndex();
        }

        /// <summary>
        /// Faz 6 Dilim 4: dünyayı düğümler + görünüm birleşimine genişletir
        /// (+pay). Düğüm yoksa dejenerelik aynen döner (ZoomToFit no-op'u
        /// ve boş-minimap erken çıkışı korunur).
        /// </summary>
        public static Rect UnionWorldWithViewport(Rect nodeBounds, Rect viewport, double margin = 64)
        {
            if (nodeBounds.Width <= 0 || nodeBounds.Height <= 0)
                return nodeBounds;
            double x1 = nodeBounds.X, y1 = nodeBounds.Y;
            double x2 = nodeBounds.X + nodeBounds.Width, y2 = nodeBounds.Y + nodeBounds.Height;
            if (viewport.Width > 0 && viewport.Height > 0)
            {
                x1 = Math.Min(x1, viewport.X);
                y1 = Math.Min(y1, viewport.Y);
                x2 = Math.Max(x2, viewport.X + viewport.Width);
                y2 = Math.Max(y2, viewport.Y + viewport.Height);
            }
            return new Rect(x1 - margin, y1 - margin,
                (x2 - x1) + margin * 2, (y2 - y1) + margin * 2);
        }

        private void UpdateWorldBounds()
        {
            Rect united = UnionWorldWithViewport(_nodeBounds, ViewportRect);
            if (united != WorldBounds)
            {
                WorldBounds = united;
                OnPropertyChanged(nameof(WorldBounds));
            }
        }
        /// <summary>Rebuilds the whole index (load / reset paths).</summary>
        public void RebuildIndex()
        {
            _index.Clear();
            _nodesByKey.Clear();
            _nodeById.Clear();
            double minX = double.PositiveInfinity, minY = double.PositiveInfinity;
            double maxX = double.NegativeInfinity, maxY = double.NegativeInfinity;
            foreach (var node in MainViewModel.Nodes)
            {
                if (!_keys.TryGetValue(node, out ulong key))
                {
                    key = _nextKey++;
                    _keys[node] = key;
                }
                _nodesByKey[key] = node;
                _nodeById[node.Id] = node;
                var (x, y, w, h) = NodeBounds(node);
                _index.Add(key, x, y, w, h);
                minX = Math.Min(minX, x);
                minY = Math.Min(minY, y);
                maxX = Math.Max(maxX, x + w);
                maxY = Math.Max(maxY, y + h);
            }
            WorldBounds = double.IsPositiveInfinity(minX)
                ? new Rect(0, 0, 0, 0)
                : new Rect(minX, minY, Math.Max(0, maxX - minX), Math.Max(0, maxY - minY));
            _nodeBounds = WorldBounds;
            UpdateWorldBounds();
            OnPropertyChanged(nameof(WorldBounds));
            RebuildIncidentMap();
            RefreshVisible();
        }

        /// <summary>Re-queries the index and diff-syncs the visible sets.</summary>
        public void RefreshVisible()
        {
            Rect view = ComputeViewportRect(
                MainViewModel.PanX, MainViewModel.PanY, MainViewModel.ZoomScale,
                ViewportWidth, ViewportHeight);
            if (view != ViewportRect)
            {
                ViewportRect = view;
                OnPropertyChanged(nameof(ViewportRect));
                // Faz 6 Dilim 4: pan/zoom ile kayan görünüm dünyayı da taşır.
                UpdateWorldBounds();
            }
            var visible = new HashSet<ulong>(_index.Query(
                view.X - CullMargin, view.Y - CullMargin,
                view.Width + CullMargin * 2, view.Height + CullMargin * 2));
            // Faz 4 Dilim 3 — navigation scope applies before culling: a node
            // outside the current subgraph depth / chapter filter never
            // materializes, no matter the viewport.
            var scope = ScopePredicate;
            var scopePass = scope is null
                ? null
                : new HashSet<NodeViewModel>(MainViewModel.Nodes.Where(scope));
            SyncCollection(VisibleNodes,
                MainViewModel.Nodes.Where(node =>
                    (scopePass is null || scopePass.Contains(node)) &&
                    _keys.TryGetValue(node, out ulong key) && visible.Contains(key)));
            var visibleEdges = new HashSet<ConnectionViewModel>();
            foreach (ulong key in visible)
            {
                if (!_incident.TryGetValue(key, out var edges))
                    continue;
                foreach (var edge in edges)
                {
                    // Scoped-out endpoints hide the wire even when the other
                    // end is visible; unscoped behavior is unchanged.
                    if (scopePass is not null &&
                        (edge.SourceNode is null || edge.TargetNode is null ||
                         !scopePass.Contains(edge.SourceNode) ||
                         !scopePass.Contains(edge.TargetNode)))
                        continue;
                    visibleEdges.Add(edge);
                }
            }
            SyncCollection(VisibleConnections,
                MainViewModel.Connections.Where(visibleEdges.Contains));
            RefreshVisibleGroups(view, scopePass);
        }

        private void RefreshVisibleGroups(Rect view, HashSet<NodeViewModel>? scopePass)
        {
            if (_groups is null)
            {
                if (VisibleGroups.Count > 0)
                    VisibleGroups.Clear();
                return;
            }
            var hit = new HashSet<ulong>(_groupIndex.Query(
                view.X - CullMargin, view.Y - CullMargin,
                view.Width + CullMargin * 2, view.Height + CullMargin * 2));
            SyncCollection(VisibleGroups,
                _groups.Where(group =>
                    _groupKeys.TryGetValue(group, out ulong key) &&
                    hit.Contains(key) &&
                    GroupPassesScope(group, scopePass)));
        }

        private bool GroupPassesScope(CanvasGroupViewModel group, HashSet<NodeViewModel>? scopePass)
        {
            // Groups are editor metadata: unscoped canvases always show them.
            // Under an active scope a group stays only while at least one live
            // member passes (empty frames stay so they remain editable).
            if (scopePass is null)
                return true;
            bool hasLiveMember = false;
            foreach (ulong memberId in group.MemberNodeIds)
            {
                if (!_nodeById.TryGetValue(memberId, out var node))
                    continue;
                hasLiveMember = true;
                if (scopePass.Contains(node))
                    return true;
            }
            return !hasLiveMember;
        }

        /// <summary>Centers the viewport on a canvas point (minimap drag).</summary>
        public void PanTo(double canvasX, double canvasY)
        {
            double zoom = MainViewModel.ZoomScale > 0 ? MainViewModel.ZoomScale : 1.0;
            MainViewModel.PanX = ViewportWidth / 2 - canvasX * zoom;
            MainViewModel.PanY = ViewportHeight / 2 - canvasY * zoom;
            MainViewModel.TargetPanX = MainViewModel.PanX;
            MainViewModel.TargetPanY = MainViewModel.PanY;
        }

        /// <summary>Focuses the selected node (F key). No-op without selection.</summary>
        [RelayCommand]
        public void FitToSelection()
        {
            var node = MainViewModel.SelectedNode;
            if (node is null)
                return;
            double zoom = MainViewModel.ZoomScale > 0 ? MainViewModel.ZoomScale : 1.0;
            var (x, y, w, h) = NodeBounds(node);
            MainViewModel.TargetPanX = ViewportWidth / 2 - (x + w / 2) * zoom;
            MainViewModel.TargetPanY = ViewportHeight / 2 - (y + h / 2) * zoom;
            MainViewModel.StartSmoothViewAnimation();
        }

        /// <summary>Fits the whole graph into the viewport. No-op when empty.</summary>
        [RelayCommand]
        public void ZoomToFit()
        {
            if (WorldBounds.Width <= 0 || WorldBounds.Height <= 0 ||
                ViewportWidth <= 0 || ViewportHeight <= 0)
                return;
            double zoom = Math.Clamp(
                Math.Min(ViewportWidth / WorldBounds.Width,
                         ViewportHeight / WorldBounds.Height),
                MinZoom, MaxZoom);
            MainViewModel.TargetZoom = zoom;
            MainViewModel.TargetPanX = ViewportWidth / 2 -
                (WorldBounds.X + WorldBounds.Width / 2) * zoom;
            MainViewModel.TargetPanY = ViewportHeight / 2 -
                (WorldBounds.Y + WorldBounds.Height / 2) * zoom;
            MainViewModel.StartSmoothViewAnimation();
        }

        public static (double x, double y, double w, double h) NodeBounds(NodeViewModel node)
        {
            double height = node.NodeCardHeight > 0 ? node.NodeCardHeight : 220.0;
            return (node.X - 8.0, node.Y, NodeCardWidth, height);
        }

        private void RebuildIncidentMap()
        {
            _incident.Clear();
            foreach (var connection in MainViewModel.Connections)
                IndexConnection(connection);
        }

        private void IndexConnection(ConnectionViewModel connection)
        {
            foreach (var endpoint in new[] { connection.SourceNode, connection.TargetNode })
            {
                if (endpoint is null || !_keys.TryGetValue(endpoint, out ulong key))
                    continue;
                if (!_incident.TryGetValue(key, out var edges))
                {
                    edges = new List<ConnectionViewModel>();
                    _incident[key] = edges;
                }
                if (!edges.Contains(connection))
                    edges.Add(connection);
            }
        }

        private void TrackNode(NodeViewModel node)
        {
            if (node is null || _keys.ContainsKey(node))
                return;
            ulong key = _nextKey++;
            _keys[node] = key;
            node.PropertyChanged += OnNodePropertyChanged;
        }

        private void UntrackNode(NodeViewModel node)
        {
            if (node is null || !_keys.TryGetValue(node, out ulong key))
                return;
            _keys.Remove(node);
            _nodesByKey.Remove(key);
            _nodeById.Remove(node.Id);
            _incident.Remove(key);
            _index.Remove(key);
            node.PropertyChanged -= OnNodePropertyChanged;
        }

        // ── Group frame tracking (same index machinery, own key space) ──

        public static (double x, double y, double w, double h) GroupBounds(CanvasGroupViewModel group) =>
            (group.X, group.Y,
             group.Width > 0 ? group.Width : CanvasGroupViewModel.MinWidth,
             group.Height > 0 ? group.Height : CanvasGroupViewModel.MinHeight);

        private void TrackGroup(CanvasGroupViewModel group)
        {
            if (group is null || _groupKeys.ContainsKey(group))
                return;
            ulong key = _nextGroupKey++;
            _groupKeys[group] = key;
            _groupsByKey[key] = group;
            var (x, y, w, h) = GroupBounds(group);
            _groupIndex.Add(key, x, y, w, h);
            group.PropertyChanged += OnGroupPropertyChanged;
        }

        private void UntrackGroup(CanvasGroupViewModel group)
        {
            if (group is null || !_groupKeys.TryGetValue(group, out ulong key))
                return;
            _groupKeys.Remove(group);
            _groupsByKey.Remove(key);
            _groupIndex.Remove(key);
            group.PropertyChanged -= OnGroupPropertyChanged;
        }

        private void OnGroupsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (e.OldItems is not null)
                foreach (CanvasGroupViewModel group in e.OldItems)
                    UntrackGroup(group);
            if (e.NewItems is not null)
                foreach (CanvasGroupViewModel group in e.NewItems)
                    TrackGroup(group);
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                foreach (var group in _groupKeys.Keys.ToList())
                    group.PropertyChanged -= OnGroupPropertyChanged;
                _groupKeys.Clear();
                _groupsByKey.Clear();
                _groupIndex.Clear();
                if (_groups is not null)
                    foreach (var group in _groups)
                        TrackGroup(group);
            }
            if (_suspendCount == 0)
                RefreshVisible();
        }

        private void OnGroupPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (sender is not CanvasGroupViewModel group)
                return;
            if ((e.PropertyName == nameof(CanvasGroupViewModel.X) ||
                e.PropertyName == nameof(CanvasGroupViewModel.Y) ||
                e.PropertyName == nameof(CanvasGroupViewModel.Width) ||
                e.PropertyName == nameof(CanvasGroupViewModel.Height)) &&
                _suspendCount == 0)
            {
                if (_groupKeys.TryGetValue(group, out ulong key))
                {
                    var (x, y, w, h) = GroupBounds(group);
                    _groupIndex.Move(key, x, y, w, h);
                }
                RefreshVisible();
            }
        }

        private static void SyncCollection<T>(
            ObservableCollection<T> target, IEnumerable<T> desired) where T : class
        {
            var wanted = desired.ToList();
            var wantedSet = new HashSet<T>(wanted);
            for (int i = target.Count - 1; i >= 0; i--)
            {
                if (!wantedSet.Contains(target[i]))
                    target.RemoveAt(i);
            }
            int slot = 0;
            foreach (var item in wanted)
            {
                int at = target.IndexOf(item);
                if (at < 0)
                    target.Insert(Math.Min(slot, target.Count), item);
                else if (at != slot)
                    target.Move(at, slot);
                slot++;
            }
        }

        private void OnMainPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (_suspendCount > 0)
                return;
            if (e.PropertyName == nameof(MainWindowViewModel.PanX) ||
                e.PropertyName == nameof(MainWindowViewModel.PanY) ||
                e.PropertyName == nameof(MainWindowViewModel.ZoomScale))
                RefreshVisible();
        }

        private void OnNodesChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (e.OldItems is not null)
                foreach (NodeViewModel node in e.OldItems)
                    UntrackNode(node);
            if (e.NewItems is not null)
                foreach (NodeViewModel node in e.NewItems)
                    TrackNode(node);
            if (e.Action == NotifyCollectionChangedAction.Reset)
            {
                foreach (var node in _keys.Keys.ToList())
                    node.PropertyChanged -= OnNodePropertyChanged;
                _keys.Clear();
                foreach (var node in MainViewModel.Nodes)
                    TrackNode(node);
            }
            if (_suspendCount == 0)
                RebuildIndex();
        }

        private void OnConnectionsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (_suspendCount > 0)
                return;
            RebuildIncidentMap();
            RefreshVisible();
        }

        private void OnNodePropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            if (sender is not NodeViewModel node)
                return;
            if ((e.PropertyName == nameof(NodeViewModel.X) ||
                e.PropertyName == nameof(NodeViewModel.Y) ||
                e.PropertyName == nameof(NodeViewModel.NodeCardHeight)) &&
                _suspendCount == 0)
            {
                if (_keys.TryGetValue(node, out ulong key))
                {
                    var (x, y, w, h) = NodeBounds(node);
                    _index.Move(key, x, y, w, h);
                }
                RefreshVisible();
            }
        }
    }
}
