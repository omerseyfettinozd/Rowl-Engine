using System;
using System.Collections.ObjectModel;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests
{
    // Grup undo/redo (saf servis + gerçek UndoRedoService).
    [Collection("StaticRootSequential")]
    public sealed class EditorGroupUndoTests
    {
        [Fact]
        public void Group_Create_UndoRedo_RestoresSameIdAndMembers()
        {
            UndoRedoService.Instance.Clear();
            try
            {
                var nodes = new ObservableCollection<NodeViewModel>
                {
                    BareNode(1, "A", 0, 0),
                    BareNode(2, "B", 400, 0),
                };
                var service = new GroupService();
                service.Attach(nodes);
                var group = service.CreateFromNodes("G", "#3B82F6", new[] { nodes[0], nodes[1] });
                string id = group.GroupId;
                var members = group.MemberNodeIds.ToList();
                UndoRedoService.Instance.RecordAction(new GroupCreateAction(service, group));

                Assert.Single(service.Groups);
                UndoRedoService.Instance.Undo();
                Assert.Empty(service.Groups);
                UndoRedoService.Instance.Redo();
                Assert.Single(service.Groups);
                Assert.Equal(id, service.Groups[0].GroupId);
                Assert.Same(group, service.Groups[0]);
                Assert.Equal(members, service.Groups[0].MemberNodeIds.ToList());
            }
            finally
            {
                UndoRedoService.Instance.Clear();
            }
        }

        [Fact]
        public void Group_Delete_Undo_RestoresSameIndex()
        {
            UndoRedoService.Instance.Clear();
            try
            {
                var nodes = new ObservableCollection<NodeViewModel>();
                var service = new GroupService();
                service.Attach(nodes);
                var g1 = service.Create("G1", "#fff", 0, 0, 200, 200);
                var g2 = service.Create("G2", "#fff", 300, 0, 200, 200);
                var g3 = service.Create("G3", "#fff", 600, 0, 200, 200);
                int index = service.IndexOf(g2.GroupId);
                Assert.Equal(1, index);

                Assert.True(service.Delete(g2.GroupId));
                UndoRedoService.Instance.RecordAction(new GroupDeleteAction(service, g2, index));
                Assert.Equal(2, service.Groups.Count);

                UndoRedoService.Instance.Undo();
                Assert.Equal(3, service.Groups.Count);
                Assert.Same(g2, service.Groups[1]);
                Assert.Same(g1, service.Groups[0]);
                Assert.Same(g3, service.Groups[2]);

                UndoRedoService.Instance.Redo();
                Assert.Equal(2, service.Groups.Count);
                Assert.DoesNotContain(service.Groups, g => g.GroupId == g2.GroupId);
            }
            finally
            {
                UndoRedoService.Instance.Clear();
            }
        }

        [Fact]
        public void Group_Move_UndoRedo_MovesMembersTogether()
        {
            UndoRedoService.Instance.Clear();
            try
            {
                var nodes = new ObservableCollection<NodeViewModel>
                {
                    BareNode(1, "A", 100, 100),
                    BareNode(2, "B", 500, 100),
                    BareNode(3, "Out", 2000, 2000),
                };
                var service = new GroupService();
                service.Attach(nodes);
                var group = service.CreateFromNodes("M", "#3B82F6", new[] { nodes[0], nodes[1] });
                double gx = group.X, gy = group.Y;
                double ax = nodes[0].X, ay = nodes[0].Y;
                double bx = nodes[1].X, by = nodes[1].Y;

                service.MoveGroup(group.GroupId, 40, -10);
                UndoRedoService.Instance.RecordAction(new GroupMoveAction(service, group.GroupId, 40, -10));

                Assert.Equal(gx + 40, group.X);
                Assert.Equal(gy - 10, group.Y);
                Assert.Equal(ax + 40, nodes[0].X);
                Assert.Equal(bx + 40, nodes[1].X);
                Assert.Equal(2000, nodes[2].X);

                UndoRedoService.Instance.Undo();
                Assert.Equal(gx, group.X);
                Assert.Equal(gy, group.Y);
                Assert.Equal(ax, nodes[0].X);
                Assert.Equal(ay, nodes[0].Y);
                Assert.Equal(bx, nodes[1].X);
                Assert.Equal(by, nodes[1].Y);

                UndoRedoService.Instance.Redo();
                Assert.Equal(gx + 40, group.X);
                Assert.Equal(gy - 10, group.Y);
                Assert.Equal(ax + 40, nodes[0].X);
                Assert.Equal(ay - 10, nodes[0].Y);
            }
            finally
            {
                UndoRedoService.Instance.Clear();
            }
        }

        [Fact]
        public void Group_Resize_Undo_RestoresSize()
        {
            UndoRedoService.Instance.Clear();
            try
            {
                var nodes = new ObservableCollection<NodeViewModel>();
                var service = new GroupService();
                service.Attach(nodes);
                var group = service.Create("R", "#fff", 0, 0, 320, 240);
                double bw = group.Width, bh = group.Height;

                Assert.True(service.ResizeGroup(group.GroupId, bw + 120, bh + 60));
                double aw = group.Width, ah = group.Height;
                UndoRedoService.Instance.RecordAction(new GroupResizeAction(service, group.GroupId, bw, bh, aw, ah));

                UndoRedoService.Instance.Undo();
                Assert.Equal(bw, group.Width);
                Assert.Equal(bh, group.Height);

                UndoRedoService.Instance.Redo();
                Assert.Equal(aw, group.Width);
                Assert.Equal(ah, group.Height);
            }
            finally
            {
                UndoRedoService.Instance.Clear();
            }
        }

        private static NodeViewModel BareNode(ulong id, string name, double x, double y) =>
            new(id, name, x, y, bare: true);
    }
}
