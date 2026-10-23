using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Headless;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 9 E1: tuval klavye + zoom ergonomisi.</summary>
[Collection("StaticRootSequential")]
public sealed class EditorCanvasErgonomicsTests : IDisposable
{
    private readonly string _previousRoot;
    private string? _root;
    private MainWindowViewModel? _vm;
    private Window? _window;

    public EditorCanvasErgonomicsTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
    }

    public void Dispose()
    {
        try
        {
            if (_window != null)
                Dispatcher.UIThread.Invoke(() => _window.Close());
        }
        catch (Exception) { }
        _window = null;
        try
        {
            if (_vm != null)
                Dispatcher.UIThread.Invoke(() => _vm.Dispose());
        }
        catch (Exception) { }
        _vm = null;
        try
        {
            if (_root != null && Directory.Exists(Path.GetDirectoryName(_root)!))
                Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true);
        }
        catch (Exception) { }
        try
        {
            if (_root != null && string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
        _root = null;
    }

    private MainWindowViewModel CreateVm()
    {
        string parent = Path.Combine(Path.GetTempPath(), "RowlErgo_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("ErgoTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory failed: " + created.Error);
        _root = created.Info.Path;
        var vm = Dispatcher.UIThread.Invoke(() =>
        {
            var created = new MainWindowViewModel(_root, connectEngine: false);
            // Yeni proje bir başlangıç düğümüyle gelir; testler boş tuvalden başlar.
            created.Nodes.Clear();
            created.Connections.Clear();
            created.SelectedNodes.Clear();
            UndoRedoService.Instance.Clear();
            return created;
        });
        _vm = vm;
        return vm;
    }

    private static ConnectionViewModel CreateConnection(NodeViewModel src, NodeViewModel dst) => new(src, dst);

    [Fact]
    public void FitHelper_TwoFarNodes_FitsWithMarginAndZoomLessThanOne()
    {
        var rects = new List<Rect>
        {
            new Rect(0, 0, 308, 220),
            new Rect(2000, 0, 308, 220),
        };
        var viewport = new Size(1280, 800);
        const double margin = 48.0;
        var (zoom, panX, panY) = NodeGraphViewModel.ComputeFitView(rects, viewport, margin);
        Assert.True(zoom < 1.0);
        var view = new Rect(-panX / zoom, -panY / zoom, viewport.Width / zoom, viewport.Height / zoom);
        foreach (var r in rects)
        {
            var expanded = new Rect(r.X - margin, r.Y - margin, r.Width + margin * 2, r.Height + margin * 2);
            Assert.True(view.X <= expanded.X + 0.5);
            Assert.True(view.Y <= expanded.Y + 0.5);
            Assert.True(view.X + view.Width >= expanded.X + expanded.Width - 0.5);
            Assert.True(view.Y + view.Height >= expanded.Y + expanded.Height - 0.5);
        }
    }

    [Fact]
    public void FitHelper_SingleSmallNode_DoesNotExceedOne()
    {
        var rects = new List<Rect> { new Rect(0, 0, 308, 220) };
        var (zoom, _, _) = NodeGraphViewModel.ComputeFitView(rects, new Size(1280, 800), 48.0);
        Assert.True(zoom <= 1.0 + 1e-9);
    }

    [Fact]
    public void FitHelper_HugeArea_ClampsToMinZoom()
    {
        var rects = new List<Rect> { new Rect(0, 0, 20000, 20000) };
        var (zoom, _, _) = NodeGraphViewModel.ComputeFitView(rects, new Size(800, 600), 48.0);
        Assert.Equal(NodeGraphViewModel.MinZoom, zoom, precision: 6);
    }

    [Fact]
    public void ZoomStep_KeepsViewCenterWorldStable()
    {
        var vm = CreateVm();
        Dispatcher.UIThread.Invoke(() =>
        {
            var graph = vm.NodeGraphViewModel;
            graph.ViewportWidth = 1280;
            graph.ViewportHeight = 800;
            vm.PanX = 0; vm.PanY = 0;
            vm.TargetPanX = 0; vm.TargetPanY = 0;
            vm.ZoomScale = 1.0; vm.TargetZoom = 1.0;
            double beforeX = (graph.ViewportWidth / 2 - vm.TargetPanX) / vm.TargetZoom;
            double beforeY = (graph.ViewportHeight / 2 - vm.TargetPanY) / vm.TargetZoom;
            vm.ZoomInViewCommand.Execute(null);
            double afterX = (graph.ViewportWidth / 2 - vm.TargetPanX) / vm.TargetZoom;
            double afterY = (graph.ViewportHeight / 2 - vm.TargetPanY) / vm.TargetZoom;
            Assert.True(Math.Abs(afterX - beforeX) <= 0.5);
            Assert.True(Math.Abs(afterY - beforeY) <= 0.5);
        });
    }

    [Fact]
    public void DeleteSelectedNodeCommand_MultiSelection_SingleUndoRestores()
    {
        var vm = CreateVm();
        Dispatcher.UIThread.Invoke(() =>
        {
            vm.AddNodeCommand.Execute(null);
            vm.AddNodeCommand.Execute(null);
            vm.AddNodeCommand.Execute(null);
            Assert.Equal(3, vm.Nodes.Count);
            var n1 = vm.Nodes[0];
            var n2 = vm.Nodes[1];
            var n3 = vm.Nodes[2];
            var conn = CreateConnection(n1, n2);
            vm.Connections.Add(conn);
            int connCount = vm.Connections.Count;
            vm.SelectedNodes.Clear();
            foreach (var n in vm.Nodes)
            {
                n.IsSelected = true;
                vm.SelectedNodes.Add(n);
            }
            vm.SelectedNode = n1;
            vm.DeleteSelectedNodeCommand.Execute(null);
            Assert.Empty(vm.Nodes);
            vm.UndoCommand.Execute(null);
            Assert.Equal(3, vm.Nodes.Count);
            Assert.Equal(connCount, vm.Connections.Count);
        });
    }

    [Fact]
    public void Window_DeleteInTextBox_DoesNotDelete()
    {
        var vm = CreateVm();
        Dispatcher.UIThread.Invoke(() =>
        {
            vm.AddNodeCommand.Execute(null);
            Assert.Equal(1, vm.Nodes.Count);
            var first = vm.Nodes[0];
            vm.SelectedNodes.Clear();
            first.IsSelected = true;
            vm.SelectedNodes.Add(first);
            vm.SelectedNode = first;
            var window = new MainWindow(vm) { Width = 1440, Height = 900 };
            _window = window;
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var textBox = window.GetVisualDescendants().OfType<TextBox>().FirstOrDefault();
            Assert.NotNull(textBox);
            textBox.Focus();
            Dispatcher.UIThread.RunJobs();
            window.KeyPressQwerty(PhysicalKey.Delete, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(1, vm.Nodes.Count);
            window.Close();
            _window = null;
        });
    }

    [Fact]
    public void Window_TabMovesSelectionToNextNode()
    {
        var vm = CreateVm();
        Dispatcher.UIThread.Invoke(() =>
        {
            vm.AddNodeCommand.Execute(null);
            vm.AddNodeCommand.Execute(null);
            vm.AddNodeCommand.Execute(null);
            Assert.Equal(3, vm.Nodes.Count);
            var window = new MainWindow(vm) { Width = 1440, Height = 900 };
            _window = window;
            window.Show();
            Dispatcher.UIThread.RunJobs();
            var graph = window.GetVisualDescendants().OfType<NodeGraphView>().FirstOrDefault();
            Assert.NotNull(graph);
            // İlk düğümü seç, tuvale odaklan.
            var first = vm.Nodes[0];
            foreach (var n in vm.Nodes)
                n.IsSelected = n == first;
            vm.SelectedNodes.Clear();
            vm.SelectedNodes.Add(first);
            vm.SelectedNode = first;
            graph.Focus(NavigationMethod.Pointer);
            Dispatcher.UIThread.RunJobs();
            window.KeyPressQwerty(PhysicalKey.Tab, RawInputModifiers.None);
            Dispatcher.UIThread.RunJobs();
            Assert.Equal(vm.Nodes[1], vm.SelectedNode);
            window.Close();
            _window = null;
        });
    }
}
