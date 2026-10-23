using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Workspace;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 8 C2 + D1 ana pencere bağlantısı: yazılamayan kayıt hata kartı açar (önceden
/// sessizdi), sonraki başarılı kayıt kartı kapatır; F6 görünür paneller arasında odak gezdirir.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorOperationStatusWiringTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorOperationStatusWiringTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlOpStatus_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("OpStatusTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
    }

    public void Dispose()
    {
        try { File.SetUnixFileMode(MainWindowViewModel.AssetsJsonPath, (UnixFileMode)0b111_101_101); } catch (Exception) { }
        try { Dispatcher.UIThread.Invoke(() => _vm.Dispose()); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); } catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    [Fact]
    public void UnwritableSave_ShowsFailureCard_ThenSuccessClearsIt()
    {
        if (OperatingSystem.IsWindows() || Environment.UserName == "root")
            return; // salt okunur dizin kipi POSIX + root olmayan kullanıcı ister
        Dispatcher.UIThread.Invoke(() =>
        {
            string jsonDir = MainWindowViewModel.AssetsJsonPath;
            Assert.False(_vm.OperationStatus.IsVisible);

            File.SetUnixFileMode(jsonDir, UnixFileMode.UserRead | UnixFileMode.UserExecute);
            try
            {
                Assert.False(_vm.SaveProjectNow());
            }
            finally
            {
                File.SetUnixFileMode(jsonDir, (UnixFileMode)0b111_101_101);
            }
            Assert.True(_vm.OperationStatus.IsFailed);
            Assert.Equal(OperationKind.Save, _vm.OperationStatus.Kind);
            Assert.False(_vm.OperationStatus.HasOutputPath); // başarısızlık çıktı konumu göstermez

            Assert.True(_vm.SaveProjectNow());
            Assert.False(_vm.OperationStatus.IsVisible);
        });
    }

    [Fact]
    public void ShowLog_OpensAndSelectsConsoleTab()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            _vm.OperationStatus.Fail(OperationKind.Export, "x");
            _vm.IsLogPanelVisible = false;
            _vm.OperationStatus.ShowLogCommand.Execute(null);
            Assert.True(_vm.IsLogPanelVisible);
            Assert.Equal(RowlEngine.Editor.Services.Workspace.BottomStripState.LogTab, _vm.BottomPanelActiveTab);
        });
    }

    [Fact]
    public void F6_CyclesKeyboardFocusAcrossVisiblePanels()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                string? PanelOfFocus() =>
                    (window.FocusManager?.GetFocusedElement() as Visual)?
                        .GetSelfAndVisualAncestors().OfType<WorkspacePanel>().FirstOrDefault()?.PanelId;

                Assert.True(window.FocusAdjacentPanel(backwards: false));
                string? first = PanelOfFocus();
                Assert.NotNull(first);
                Assert.True(window.FocusAdjacentPanel(backwards: false));
                string? second = PanelOfFocus();
                Assert.NotNull(second);
                Assert.NotEqual(first, second);
                Assert.True(window.FocusAdjacentPanel(backwards: true));
                Assert.Equal(first, PanelOfFocus());
            }
            finally
            {
                window.Close();
            }
        });
    }
}
