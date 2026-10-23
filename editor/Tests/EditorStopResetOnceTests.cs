using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Oyunu durdururken motor başlangıç düğümüne bir kez sıfırlanır. Önceden durdurma kodu,
/// önizlemenin "oynatma bitti" ve "oyun modu kapandı" tepkileri ayrı ayrı sıfırlıyordu
/// (günlükte art arda üç "Engine Reset to Start Node").
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorStopResetOnceTests
{
    [Fact]
    public void StoppingPlay_WithGamePreviewOpen_ResetsEngineOnce()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        NativeEnvironment.EnsureDisplayFreeDrivers();
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlStopReset_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        string root = ProjectFactory.CreateNewProject("StopReset", parent).Info!.Path;
        var vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(root, connectEngine: false));
        try
        {
            Dispatcher.UIThread.Invoke(() =>
            {
                var window = new MainWindow(vm) { Width = 1440, Height = 900 };
                window.Show();
                try
                {
                    vm.ShowPanel("EnginePreview"); // oyun önizlemesi açık: motoru kurar
                    Dispatcher.UIThread.RunJobs();
                    Assert.True(vm.EngineHost.IsInitialized);

                    vm.EngineHost.SetPlayState(true);
                    vm.IsPlayingStandalone = true;
                    Dispatcher.UIThread.RunJobs();
                    int before = vm.EngineHost.ResetToStartNodeCount;

                    vm.TogglePlayStandalone(); // oynarken = durdur
                    Dispatcher.UIThread.RunJobs();

                    Assert.False(vm.EngineHost.IsPlaying);
                    Assert.Equal(1, vm.EngineHost.ResetToStartNodeCount - before);
                }
                finally
                {
                    window.Close();
                }
            });
        }
        finally
        {
            Dispatcher.UIThread.Invoke(() => vm.Dispose());
            try { Directory.Delete(parent, true); } catch (Exception) { }
            if (string.Equals(MainWindowViewModel.ProjectRoot, root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = previousRoot;
        }
    }
}
