using System;
using System.IO;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Unity kromu Dilim A kilidi (VM yanı): Duraklat/Adım durum makinesi
/// mevcut standalone oynatmaya bağlıdır — yeni motor işi yok, pencere
/// açılmaz. Krom yanı (küme konumu/bağlantılar) Test 35'in içindedir;
/// orası suite önyüklemesiyle pencere kurabilen tek yerdir.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorTransportClusterTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorTransportClusterTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlTransport_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("TransportTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
        // KÖK GERİ VERİLMEZ: bkz. EditorAssetGridVirtualizationTests.
    }

    public void Dispose()
    {
        try { _vm.Dispose(); } catch (Exception) { }
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
    public void PauseAndStep_FollowPlayStateWithoutEngine()
    {
        // Başlangıç: kapalı.
        Assert.False(_vm.CanPauseStandalone);
        Assert.False(_vm.CanStepStandalone);

        // Oynamıyorken Duraklat no-op.
        _vm.PauseStandaloneCommand.Execute(null);
        Assert.False(_vm.IsPlayPaused);

        // Oynuyor simülasyonu (motor bağlı değil; native çağrılar no-op).
        _vm.IsPlayingStandalone = true;
        Assert.True(_vm.CanPauseStandalone);
        Assert.False(_vm.CanStepStandalone);

        _vm.PauseStandaloneCommand.Execute(null);
        Assert.True(_vm.IsPlayPaused);
        Assert.True(_vm.CanStepStandalone);

        // Adım yalnızca duraklıyken geçer (motorsuz no-op, çökmez).
        _vm.StepStandaloneCommand.Execute(null);

        _vm.PauseStandaloneCommand.Execute(null);
        Assert.False(_vm.IsPlayPaused);
        Assert.False(_vm.CanStepStandalone);

        // Oynamıyorken Pause no-op'tur (bayrak korunur, çökmez).
        _vm.IsPlayingStandalone = false;
        _vm.PauseStandaloneCommand.Execute(null);
        Assert.False(_vm.IsPlayPaused);
        Assert.False(_vm.CanPauseStandalone);
    }
}
