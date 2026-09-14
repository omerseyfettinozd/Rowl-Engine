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

        /// <summary>Union of all node bounds in canvas coordinates.</summary>
        public Rect WorldBounds { get; private set; }

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

        /// <summary>Rebuilds the whole index (load / reset paths).</summary>
        public void RebuildIndex()
        {
            _index.Clear();
            _nodesByKey.Clear();
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
            }
            var visible = new HashSet<ulong>(_index.Query(
                view.X - CullMargin, view.Y - CullMargin,
                view.Width + CullMargin * 2, view.Height + CullMargin * 2));
            SyncCollection(VisibleNodes,
                MainViewModel.Nodes.Where(node =>
                    _keys.TryGetValue(node, out ulong key) && visible.Contains(key)));
            var visibleEdges = new HashSet<ConnectionViewModel>();
            foreach (ulong key in visible)
            {
                if (_incident.TryGetValue(key, out var edges))
                    foreach (var edge in edges)
                        visibleEdges.Add(edge);
            }
            SyncCollection(VisibleConnections,
                MainViewModel.Connections.Where(visibleEdges.Contains));
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
            _incident.Remove(key);
            _index.Remove(key);
            node.PropertyChanged -= OnNodePropertyChanged;
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
