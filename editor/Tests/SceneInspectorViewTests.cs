using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Inspector;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels.Inspector;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 10 D2 kilidi: yazar dilindeki Denetçi ana pencerede görünür, eski ayrıntılı düzenleyici
/// "Gelişmiş ayarlar" altında katlıdır, parçalı seçim ViewModel'e yazar, seçim kalkınca boş durum açılır.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class SceneInspectorViewTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public SceneInspectorViewTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlSceneView_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("SceneView", parent);
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
    public void SceneInspector_IsShown_AdvancedCollapsed_SegmentWritesViewModel_EmptyStateOnDeselect()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                var node = _vm.Nodes.First(n => n.AllComponents.Any(c => c is RowlEngine.Editor.ViewModels.Components.CharacterComponentViewModel));
                _vm.SelectedNode = node;
                Dispatcher.UIThread.RunJobs();

                var scene = window.GetVisualDescendants().OfType<SceneInspectorView>().Single();
                Assert.True(scene.IsEffectivelyVisible);
                Assert.Equal(InspectorMode.Single, _vm.InspectorScene.Mode);

                var advanced = window.GetVisualDescendants().OfType<Expander>().Single(e => e.Name == "AdvancedInspectorExpander");
                Assert.False(advanced.IsExpanded);

                var left = scene.GetVisualDescendants().OfType<RadioButton>()
                    .Single(r => r.GroupName == "Placement" && r.Content is string text && text == RowlEngine.Editor.Services.Strings.EditorStrings.T("Inspector.Scene.PlacementLeft"));
                left.IsChecked = true;
                Dispatcher.UIThread.RunJobs();
                Assert.Equal(StagePlacement.Left, _vm.InspectorScene.Placement);

                _vm.SelectedNodes.Clear();
                _vm.SelectedNode = null;
                Dispatcher.UIThread.RunJobs();
                Assert.Equal(InspectorMode.Empty, _vm.InspectorScene.Mode);
                Assert.False(advanced.IsEffectivelyVisible);
            }
            finally
            {
                window.Close();
            }
        });
    }
}
