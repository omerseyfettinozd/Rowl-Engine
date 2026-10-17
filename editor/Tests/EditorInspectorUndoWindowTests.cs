using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 9 E3 pencere bağı: Denetçi'deki başlık kutusunda yapılan düzenleme, odak tuvale
/// geçince tek geri alma adımı olur; Ctrl+Z eski başlığı geri getirir. Tuvalde düğüm
/// taşımak (Denetçi dışı) Denetçi kaydı üretmez.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorInspectorUndoWindowTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorInspectorUndoWindowTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlInspUndo_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("InspUndoTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
    }

    public void Dispose()
    {
        try { Dispatcher.UIThread.Invoke(() => _vm.Dispose()); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); } catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
        UndoRedoService.Instance.Clear();
    }

    [Fact]
    public void TitleEdit_InInspector_UndoesAsOneStep_AfterFocusLeaves()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                var node = _vm.Nodes.First();
                _vm.SelectedNode = node;
                Dispatcher.UIThread.RunJobs();
                UndoRedoService.Instance.Clear();
                string original = node.Title;

                var inspector = window.GetVisualDescendants().OfType<NodeInspectorView>().First();
                var titleBox = inspector.GetVisualDescendants().OfType<TextBox>()
                    .First(t => ReferenceEquals(t.DataContext, _vm.InspectorScene) && t.Text == original); // Faz 10: yeni Denetçi başlığı
                titleBox.Focus();
                Dispatcher.UIThread.RunJobs();
                titleBox.Text = original + " A";
                titleBox.Text = original + " AB";
                Dispatcher.UIThread.RunJobs();
                Assert.Equal(original + " AB", node.Title);

                window.GetVisualDescendants().OfType<NodeGraphView>().First().Focus();
                Dispatcher.UIThread.RunJobs();
                Assert.True(UndoRedoService.Instance.CanUndo);

                UndoRedoService.Instance.Undo();
                Assert.Equal(original, node.Title);
                UndoRedoService.Instance.Redo();
                Assert.Equal(original + " AB", node.Title);
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void CanvasMove_WhileInspectorInputFocused_IsNotRecordedAsInspectorEdit()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                var node = _vm.Nodes.First();
                _vm.SelectedNode = node;
                Dispatcher.UIThread.RunJobs();
                UndoRedoService.Instance.Clear();

                var inspector = window.GetVisualDescendants().OfType<NodeInspectorView>().First();
                var titleBox = inspector.GetVisualDescendants().OfType<TextBox>()
                    .First(t => ReferenceEquals(t.DataContext, _vm.InspectorScene) && t.Text == node.Title);
                titleBox.Focus();
                Dispatcher.UIThread.RunJobs();
                node.X += 120; // tuval sürüklemesi gibi Denetçi dışı değişim
                window.GetVisualDescendants().OfType<NodeGraphView>().First().Focus();
                Dispatcher.UIThread.RunJobs();

                Assert.False(UndoRedoService.Instance.CanUndo);
            }
            finally
            {
                window.Close();
            }
        });
    }
}
