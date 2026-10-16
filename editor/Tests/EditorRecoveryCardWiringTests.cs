using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 8 B1 (yol haritası B04): bozuk proje açılınca kurtarma teklifi kart olarak gelir
/// (önceden yalnız günlüğe "RestoreFromRecovery ile geri yükleyin" yazılıyordu).
/// Geri yükleme iki adımlıdır ve grafiği yükler; yoksay kurtarma dosyalarını silmez.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorRecoveryCardWiringTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly string _canonical;

    public EditorRecoveryCardWiringTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlRecoveryCard_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("RecoveryCardTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _canonical = Path.Combine(_root, "Assets", "json", "full_story_graph.json");

        // Sağlam bir kayıt (son sağlam kopya + günlük), ardından kanonik dosyayı boz.
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new MainWindowViewModel(_root, connectEngine: false);
            try { Assert.True(vm.SaveProjectNow()); }
            finally { vm.Dispose(); }
        });
        File.WriteAllText(_canonical, "not json{{{");
    }

    public void Dispose()
    {
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); } catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    [Fact]
    public void CorruptProject_OffersCard_RestoreIsTwoStep()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new MainWindowViewModel(_root, connectEngine: false);
            try
            {
                var card = vm.RecoveryOffer;
                Assert.NotNull(card);
                Assert.True(card!.NodeCount > 0);
                Assert.Equal("not json{{{", File.ReadAllText(_canonical)); // asla kendiliğinden ezilmez

                card.RestoreCommand.Execute(null);           // 1. adım: yalnız onay ister
                Assert.True(card.IsConfirmingRestore);
                Assert.Equal("not json{{{", File.ReadAllText(_canonical));

                card.RestoreCommand.Execute(null);           // 2. adım: uygular
                Assert.Null(vm.RecoveryOffer);
                Assert.NotEqual("not json{{{", File.ReadAllText(_canonical));
                Assert.Equal(card.NodeCount, vm.Nodes.Count);
            }
            finally { vm.Dispose(); }
        });
    }

    [Fact]
    public void Dismiss_HidesCard_KeepsRecoveryFiles()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var vm = new MainWindowViewModel(_root, connectEngine: false);
            try
            {
                Assert.NotNull(vm.RecoveryOffer);
                string recoveryDir = CrashRecoveryService.GetRecoveryDir(MainWindowViewModel.AssetsJsonPath);
                int before = Directory.GetFiles(recoveryDir).Length;
                Assert.True(before > 0);

                vm.RecoveryOffer!.DismissCommand.Execute(null);
                Assert.Null(vm.RecoveryOffer);
                Assert.Equal(before, Directory.GetFiles(recoveryDir).Length);
                Assert.Equal("not json{{{", File.ReadAllText(_canonical));
            }
            finally { vm.Dispose(); }
        });
    }
}
