using Avalonia.Controls;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Workspace;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 6 görsel uyum kilidi: sessiz panel başlığı. Kapat hep görünür; Yerleşim ve
/// Sabitle yalnız panel işaretçi altındayken / klavye odağı içindeyken ya da panel
/// sabitken (yalnız Sabitle) görünür. Düğmeler hep yerleşimde kalır (IsVisible).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorQuietHeaderTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorQuietHeaderTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlQuietHeader_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("QuietHeaderTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
        // KÖK GERİ VERİLMEZ: bkz. EditorAssetGridVirtualizationTests.
    }

    public void Dispose()
    {
        try { Dispatcher.UIThread.Invoke(() => _vm.Dispose()); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); }
        catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    [Fact]
    public void HeaderActions_RevealOnFocusAndKeepPinVisibleWhenPinned()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                var host = window.FindControl<WorkspaceHost>("WorkspaceHost")
                    ?? throw new Exception("WorkspaceHost not found");
                WorkspacePanel node = host.FindPanel(PanelIds.Node)!;
                PanelHeaderActions actions = node.TemplateHeaderActions!;
                Assert.True(actions.IsVisible);

                // Dinlenme: yalnız Kapat.
                Assert.Equal(0, actions.LayoutButton.Opacity);
                Assert.Equal(0, actions.PinButton.Opacity);
                Assert.Equal(1, actions.CloseButton.Opacity);
                Assert.True(actions.LayoutButton.IsVisible && actions.PinButton.IsVisible);

                // Klavye odağı panelde → Yerleşim + Sabitle görünür.
                Assert.True(node.HostHeader!.Focus());
                Dispatcher.UIThread.RunJobs();
                Assert.True(node.IsKeyboardFocusWithin);
                Assert.Equal(1, actions.LayoutButton.Opacity);
                Assert.Equal(1, actions.PinButton.Opacity);

                // Odak panelden çıkınca → yeniden sakin.
                window.FocusManager!.ClearFocus();
                Dispatcher.UIThread.RunJobs();
                Assert.False(node.IsKeyboardFocusWithin);
                Assert.Equal(0, actions.LayoutButton.Opacity);

                // Sabit panel: Sabitle durumu hep görünür, Yerleşim değil.
                node.IsPinned = true;
                Dispatcher.UIThread.RunJobs();
                Assert.Equal(1, actions.PinButton.Opacity);
                Assert.Equal(0, actions.LayoutButton.Opacity);
            }
            finally
            {
                window.Close();
            }
        });
    }
}
