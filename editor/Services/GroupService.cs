using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Faz 4 Dilim 3 — visual canvas-group owner. Groups are editor-only
    /// metadata (title / color / frame / members): this service never touches
    /// runtime execution state, connections or save/load boundaries. Moving a
    /// group by its header translates every member node by the same delta so
    /// the frame and its content travel together on the canvas.
    /// </summary>
    public sealed class GroupService
    {
        private ObservableCollection<NodeViewModel>? _nodes;

        /// <summary>Live group frames bound by the canvas (behind the nodes).</summary>
        public ObservableCollection<CanvasGroupViewModel> Groups { get; } = new();

        /// <summary>Attaches the master node collection for member translation.</summary>
        public void Attach(ObservableCollection<NodeViewModel> nodes)
        {
            _nodes = nodes;
        }

        public CanvasGroupViewModel? Find(string groupId) =>
            Groups.FirstOrDefault(g =>
                string.Equals(g.GroupId, groupId, StringComparison.Ordinal));

        /// <summary>Creates an empty group with a unique id.</summary>
        public CanvasGroupViewModel Create(
            string title, string color, double x, double y, double w, double h)
        {
            string id = NewGroupId();
            var group = new CanvasGroupViewModel(id, title, color, x, y, w, h);
            Groups.Add(group);
            return group;
        }

        /// <summary>
        /// Creates a group framing the given nodes (tight bounds + padding)
        /// and registering them as members.
        /// </summary>
        public CanvasGroupViewModel CreateFromNodes(
            string title, string color, IEnumerable<NodeViewModel> nodes,
            double padding = CanvasGroupViewModel.DefaultFramePadding)
        {
            var list = nodes.ToList();
            var (x, y, w, h) = CanvasGroupViewModel.FrameAroundBounds(
                list.Select(NodeBounds), padding);
            var group = new CanvasGroupViewModel(
                NewGroupId(), title, color, x, y, w, h,
                list.Select(n => n.Id));
            Groups.Add(group);
            return group;
        }

        public bool Delete(string groupId)
        {
            var group = Find(groupId);
            if (group is null)
                return false;
            return Groups.Remove(group);
        }

        /// <summary>
        /// Drags a group by its header: the frame and every member node move
        /// by the same delta. Returns the number of member nodes translated.
        /// Non-member nodes never move.
        /// </summary>
        public int MoveGroup(string groupId, double deltaX, double deltaY)
        {
            var group = Find(groupId);
            if (group is null)
                return 0;
            group.X += deltaX;
            group.Y += deltaY;
            if (_nodes is null || group.MemberNodeIds.Count == 0)
                return 0;
            var byId = _nodes.ToDictionary(n => n.Id);
            int moved = 0;
            foreach (ulong memberId in group.MemberNodeIds)
            {
                if (byId.TryGetValue(memberId, out var node))
                {
                    node.X += deltaX;
                    node.Y += deltaY;
                    moved++;
                }
            }
            return moved;
        }

        /// <summary>Resizes a group frame (clamped to the minimum size).</summary>
        public bool ResizeGroup(string groupId, double width, double height)
        {
            var group = Find(groupId);
            if (group is null)
                return false;
            group.Width = Math.Max(CanvasGroupViewModel.MinWidth, width);
            group.Height = Math.Max(CanvasGroupViewModel.MinHeight, height);
            return true;
        }

        /// <summary>
        /// Re-frames an existing group tightly around its current members.
        /// Returns false when the group is missing or has no live members.
        /// </summary>
        public bool FrameToMembers(
            string groupId,
            double padding = CanvasGroupViewModel.DefaultFramePadding)
        {
            var group = Find(groupId);
            if (group is null || _nodes is null)
                return false;
            var byId = _nodes.ToDictionary(n => n.Id);
            var members = group.MemberNodeIds
                .Where(byId.ContainsKey)
                .Select(id => byId[id])
                .ToList();
            if (members.Count == 0)
                return false;
            var (x, y, w, h) = CanvasGroupViewModel.FrameAroundBounds(
                members.Select(NodeBounds), padding);
            group.X = x;
            group.Y = y;
            group.Width = w;
            group.Height = h;
            return true;
        }

        /// <summary>Member node ids of one group (empty when unknown).</summary>
        public IReadOnlyList<ulong> MembersOf(string groupId) =>
            Find(groupId) is { } group
                ? group.MemberNodeIds.ToList()
                : Array.Empty<ulong>();

        /// <summary>All groups containing the node (groups may overlap).</summary>
        public IReadOnlyList<CanvasGroupViewModel> GroupsOf(ulong nodeId) =>
            Groups.Where(g => g.MemberNodeIds.Contains(nodeId)).ToList();

        /// <summary>Replaces the session groups from parsed v5 records.</summary>
        public void LoadFrom(IEnumerable<CanvasGroup> records)
        {
            Groups.Clear();
            foreach (var record in records)
                Groups.Add(CanvasGroupViewModel.FromRecord(record));
        }

        public void Clear() => Groups.Clear();

        /// <summary>Session groups as v5 persistence records.</summary>
        public List<CanvasGroup> ToRecords() =>
            Groups.Select(g => g.ToRecord()).ToList();

        private string NewGroupId()
        {
            int n = Groups.Count + 1;
            string candidate = $"g{n}";
            while (Find(candidate) is not null)
            {
                n++;
                candidate = $"g{n}";
            }
            return candidate;
        }

        private static (double x, double y, double w, double h) NodeBounds(NodeViewModel node)
        {
            double height = node.NodeCardHeight > 0 ? node.NodeCardHeight : 220.0;
            return (node.X - 8.0, node.Y, NodeGraphViewModel.NodeCardWidth, height);
        }
    }
}
