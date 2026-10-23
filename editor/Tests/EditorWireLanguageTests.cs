using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 9 E4: ret dili + boş tuvale bırakınca düğüm oluştur.</summary>
[Collection("StaticRootSequential")]
public sealed class EditorWireLanguageTests
{
    [Fact]
    public void WireConnect_Rejections_SelfDuplicateNone()
    {
        UndoRedoService.Instance.Clear();
        try
        {
            var nodes = new ObservableCollection<NodeViewModel>();
            var conns = new ObservableCollection<ConnectionViewModel>();
            var a = new NodeViewModel(1, "A", 100, 100, bare: true);
            var b = new NodeViewModel(2, "B", 600, 100, bare: true);
            nodes.Add(a);
            nodes.Add(b);

            // Kendine: red + bağlantı yok.
            var selfPos = new Point(a.X + 10, a.Y + 60);
            var r1 = StoryGraphCanvasService.TryConnectWire(a, selfPos, nodes, conns, out var reason1);
            Assert.Null(r1);
            Assert.Equal(WireConnectRejection.SelfConnection, reason1);
            Assert.Empty(conns);

            // Geçerli: None + bağlantı var.
            var validPos = new Point(b.X + 10, b.Y + 60);
            var r2 = StoryGraphCanvasService.TryConnectWire(a, validPos, nodes, conns, out var reason2);
            Assert.NotNull(r2);
            Assert.Equal(WireConnectRejection.None, reason2);
            Assert.Single(conns);

            // Kopya: ikinci deneme Duplicate.
            var r3 = StoryGraphCanvasService.TryConnectWire(a, validPos, nodes, conns, out var reason3);
            Assert.Null(r3);
            Assert.Equal(WireConnectRejection.Duplicate, reason3);
            Assert.Single(conns);
        }
        finally
        {
            UndoRedoService.Instance.Clear();
        }
    }

    [Fact]
    public void DragSession_SelfDrop_NotifiesOnce()
    {
        UndoRedoService.Instance.Clear();
        try
        {
            var nodes = new ObservableCollection<NodeViewModel>();
            var conns = new ObservableCollection<ConnectionViewModel>();
            var src = new NodeViewModel(11, "A", 100, 100, bare: true);
            nodes.Add(src);
            var session = new StoryGraphDragSession();
            session.StartWireDrag(src);
            int count = 0;
            string last = string.Empty;
            session.EndWireDrag(
                new Point(src.X + 10, src.Y + 60), nodes, conns,
                null, _ => { }, () => { },
                notify: msg => { count++; last = msg; });
            Assert.Equal(1, count);
            Assert.False(string.IsNullOrWhiteSpace(last));
            Assert.Empty(conns);
        }
        finally
        {
            UndoRedoService.Instance.Clear();
        }
    }

    [Fact]
    public void Vm_EmptyDrop_CreatesNodeAndSingleUndo()
    {
        UndoRedoService.Instance.Clear();
        string prevRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlWireLang_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("WireLangTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        string root = created.Info.Path;
        MainWindowViewModel? vm = null;
        try
        {
            vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(root, connectEngine: false));
            Dispatcher.UIThread.Invoke(() =>
            {
                vm.Nodes.Clear();
                vm.Connections.Clear();
                UndoRedoService.Instance.Clear();
                var src = vm.AddNodeAt(100, 100);
                UndoRedoService.Instance.Clear();
                vm.StartWireDrag(src, new Point(src.X + 200, src.Y + 60));
                vm.UpdateWireDrag(new Point(800, 500));
                vm.EndWireDrag(new Point(800, 500));
                Assert.Equal(2, vm.Nodes.Count);
                Assert.Single(vm.Connections);
                var fresh = vm.Nodes.FirstOrDefault(n => n != src);
                Assert.NotNull(fresh);
                Assert.Equal(fresh, vm.SelectedNode);
                Assert.True(UndoRedoService.Instance.CanUndo);
                UndoRedoService.Instance.Undo();
                Assert.Single(vm.Nodes);
                Assert.Empty(vm.Connections);
                UndoRedoService.Instance.Redo();
                Assert.Equal(2, vm.Nodes.Count);
                Assert.Single(vm.Connections);
            });
        }
        finally
        {
            try { if (vm != null) Dispatcher.UIThread.Invoke(() => vm.Dispose()); } catch (Exception) { }
            try { Directory.Delete(parent, recursive: true); } catch (Exception) { }
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = prevRoot;
            }
            catch (Exception) { }
            UndoRedoService.Instance.Clear();
        }
    }
}
