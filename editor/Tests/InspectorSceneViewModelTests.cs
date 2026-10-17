using System;
using System.IO;
using System.Linq;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.ViewModels.Inspector;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class InspectorSceneViewModelTests
{
    // Geçici proje kabuğu.
    private sealed class Scope : IDisposable
    {
        public string PrevRoot { get; }
        public string ParentDir { get; }
        public string Root { get; }
        public MainWindowViewModel Vm { get; }

        public Scope(string name)
        {
            PrevRoot = MainWindowViewModel.ProjectRoot;
            ParentDir = Path.Combine(Path.GetTempPath(), "RowlScene_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(ParentDir);
            var created = ProjectFactory.CreateNewProject(name, ParentDir);
            if (!created.Success || created.Info == null)
                throw new Exception("CreateNewProject failed: " + created.Error);
            Root = created.Info.Path;
            Vm = Dispatcher.UIThread.Invoke(() =>
            {
                var vm = new MainWindowViewModel(Root, connectEngine: false);
                vm.Nodes.Clear();
                vm.Connections.Clear();
                return vm;
            });
            // ProjectRoot test boyunca geçici projede kalır: Dispose bekleyen kaydı statik köke
            // yazar; erken geri yüklemek deponun örnek projesini (Assets/json) ezerdi.
        }

        public void Dispose()
        {
            try { Dispatcher.UIThread.Invoke(() => Vm.Dispose()); } catch { }
            try { Directory.Delete(ParentDir, recursive: true); } catch { }
            try
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, Root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = PrevRoot;
            }
            catch { }
        }
    }

    [Fact]
    public void Mode_EmptySingleMulti()
    {
        using var s = new Scope("Mode");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            vm.SelectedNode = null;
            vm.SelectedNodes.Clear();
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                Assert.Equal(InspectorMode.Empty, insp.Mode);
                var n1 = new NodeViewModel(1, "A", 0, 0, bare: true);
                var n2 = new NodeViewModel(2, "B", 0, 0, bare: true);
                vm.Nodes.Add(n1);
                vm.Nodes.Add(n2);
                vm.SelectedNode = n1;
                Assert.Equal(InspectorMode.Single, insp.Mode);
                vm.SelectedNodes.Clear();
                vm.SelectedNodes.Add(n1);
                vm.SelectedNodes.Add(n2);
                Assert.Equal(InspectorMode.Multi, insp.Mode);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void Placement_RightAndSetLeft()
    {
        using var s = new Scope("Placement");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            vm.Nodes.Add(node);
            var ch = node.CreateObject("X").AddComponent<CharacterComponentViewModel>();
            ch.X = 1500; ch.Width = 200;
            vm.SelectedNode = node;
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                Assert.Equal(StagePlacement.Right, insp.Placement);
                insp.Placement = StagePlacement.Left;
                Assert.Equal(480 - 100, ch.X, precision: 5);
                Assert.Equal("Left", ch.Position);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void Pace_Mapping()
    {
        using var s = new Scope("Pace");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            vm.Nodes.Add(node);
            var d = node.CreateObject("X").AddComponent<DialogueComponentViewModel>();
            d.TypewriterEnabled = true;
            d.TextSpeed = 60;
            vm.SelectedNode = node;
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                Assert.Equal(TextPace.Slow, insp.Pace);
                insp.Pace = TextPace.Instant;
                Assert.False(d.TypewriterEnabled);
                insp.Pace = TextPace.Fast;
                Assert.True(d.TypewriterEnabled);
                Assert.Equal(15, d.TextSpeed);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void Fit_FillSet()
    {
        using var s = new Scope("Fit");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            vm.Nodes.Add(node);
            var bg = node.CreateObject("X").AddComponent<BackgroundComponentViewModel>();
            vm.SelectedNode = node;
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                insp.Fit = BackgroundFit.Fill;
                Assert.Equal(0, bg.X);
                Assert.Equal(0, bg.Y);
                Assert.Equal(1920, bg.Width);
                Assert.Equal(1080, bg.Height);
                Assert.Equal(BackgroundFit.Fill, insp.Fit);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void ChoiceRows_TargetMissingAfterDelete()
    {
        using var s = new Scope("Choice");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var n1 = new NodeViewModel(1, "Bir", 0, 0, bare: true);
            var n2 = new NodeViewModel(2, "Iki", 0, 0, bare: true);
            vm.Nodes.Add(n1);
            vm.Nodes.Add(n2);
            var choice = n1.CreateObject("X").AddComponent<ChoiceComponentViewModel>();
            choice.Options.Clear(); // bileşen varsayılan bir seçenekle gelir
            var opt = new ChoiceOptionViewModel { Text = "Git" };
            opt.TargetNodeId = 2;
            choice.Options.Add(opt);
            vm.SelectedNode = n1;
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                Assert.Single(insp.ChoiceRows);
                Assert.Equal("Iki", insp.ChoiceRows[0].TargetTitle);
                vm.Nodes.Remove(n2);
                Assert.Single(insp.ChoiceRows);
                Assert.True(insp.ChoiceRows[0].IsTargetMissing);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void MultiChapter_SingleUndo()
    {
        using var s = new Scope("Multi");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var n1 = new NodeViewModel(1, "A", 0, 0, bare: true);
            var n2 = new NodeViewModel(2, "B", 0, 0, bare: true);
            n1.ChapterId = "a";
            n2.ChapterId = "b";
            vm.Nodes.Add(n1);
            vm.Nodes.Add(n2);
            vm.SelectedNodes.Clear();
            vm.SelectedNodes.Add(n1);
            vm.SelectedNodes.Add(n2);
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                Assert.Null(insp.MultiChapter);
                insp.MultiChapter = "c1";
                Assert.Equal("c1", n1.ChapterId);
                Assert.Equal("c1", n2.ChapterId);
                UndoRedoService.Instance.Undo();
                Assert.Equal("a", n1.ChapterId);
                Assert.Equal("b", n2.ChapterId);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void AddChoice_CreatesComponentAndRow()
    {
        using var s = new Scope("AddChoice");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            vm.Nodes.Add(node);
            vm.SelectedNode = node;
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                Assert.False(insp.HasChoice);
                insp.AddChoiceCommand.Execute(null);
                Assert.True(insp.HasChoice);
                Assert.Single(insp.ChoiceRows);
            }
            finally { insp.Dispose(); }
        });
    }

    [Fact]
    public void BackgroundLabel_HashRule()
    {
        using var s = new Scope("BgLabel");
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = s.Vm;
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            vm.Nodes.Add(node);
            var bg = node.CreateObject("X").AddComponent<BackgroundComponentViewModel>();
            vm.SelectedNode = node;
            var insp = new InspectorSceneViewModel(vm);
            try
            {
                bg.Texture = "3a72957d667c9f393097f09b90a4f59f.jpg";
                Assert.Equal(EditorStrings.T("Inspector.Scene.ImportedImage"), insp.BackgroundLabel);
                bg.Texture = "oda_gece.png";
                Assert.Equal("oda_gece", insp.BackgroundLabel);
            }
            finally { insp.Dispose(); }
        });
    }
}
