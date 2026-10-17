using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels;
using Xunit;

namespace RowlEngine.Editor.Tests;

// B kabul yolculuğu: gerçek pencere açıkken create → edit → import → preview → save → SaveAs → export.
[Collection("StaticRootSequential")]
public sealed class EditorBAcceptanceJourneyTests : IDisposable
{
    private readonly string _previousRoot;

    public EditorBAcceptanceJourneyTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
    }

    public void Dispose()
    {
        try { MainWindowViewModel.ProjectRoot = _previousRoot; } catch (Exception) { }
        try { UndoRedoService.Instance.Clear(); } catch (Exception) { }
    }

    private static string NewParent(string prefix)
    {
        string parent = Path.Combine(Path.GetTempPath(), prefix + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        return parent;
    }

    private static void CleanupParent(string? parent)
    {
        try { if (!string.IsNullOrEmpty(parent) && Directory.Exists(parent)) Directory.Delete(parent, true); } catch (Exception) { }
    }

    private static async Task WaitUntilAsync(Func<bool> cond, int ms = 5000)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(ms);
        while (!cond())
        {
            if (DateTime.UtcNow >= deadline)
                throw new TimeoutException("Condition not met in time.");
            await Task.Delay(20).ConfigureAwait(false);
        }
    }

    [Fact]
    public void Journey_CreateEditSaveReopen()
    {
        string parent = NewParent("RowlJourneyA_");
        string root = "";
        MainWindowViewModel? vm = null;
        MainWindowViewModel? reopenVm = null;
        int savedCount = 0;
        string savedTitle = "";
        try
        {
            var created = ProjectFactory.CreateNewProject("JourneyA", parent);
            Assert.True(created.Success && created.Info != null);
            root = created.Info!.Path;

            Dispatcher.UIThread.Invoke(() =>
            {
                vm = new MainWindowViewModel(root, connectEngine: false);
                var window = new MainWindow(vm) { Width = 1440, Height = 900 };
                window.Show();
                try
                {
                    int initial = vm.Nodes.Count;
                    Assert.True(initial > 0);
                    UndoRedoService.Instance.Clear();

                    // Gerçek girdi: Ctrl+N yeni düğüm.
                    window.KeyPressQwerty(PhysicalKey.N, RawInputModifiers.Control);
                    Dispatcher.UIThread.RunJobs();
                    Assert.Equal(initial + 1, vm.Nodes.Count);

                    var node = vm.SelectedNode ?? vm.Nodes.Last();
                    vm.SelectedNode = node;
                    Dispatcher.UIThread.RunJobs();
                    string original = node.Title;
                    string edited = original + " Yolculuk";

                    // Gerçek girdi: Denetçi başlık kutusuna yaz.
                    var inspector = window.GetVisualDescendants().OfType<NodeInspectorView>().First();
                    var titleBox = inspector.GetVisualDescendants().OfType<TextBox>()
                        .First(t => ReferenceEquals(t.DataContext, vm.InspectorScene) && t.Text == original); // Faz 10: yeni Denetçi başlığı
                    titleBox.Focus();
                    Dispatcher.UIThread.RunJobs();
                    titleBox.Text = edited;
                    Dispatcher.UIThread.RunJobs();
                    Assert.Equal(edited, node.Title);

                    // Odağı tuvale ver: tek undo adımı oluşur (Faz 9 E3).
                    window.GetVisualDescendants().OfType<NodeGraphView>().First().Focus();
                    Dispatcher.UIThread.RunJobs();
                    Assert.True(UndoRedoService.Instance.CanUndo);

                    // Gerçek girdi: Ctrl+Z geri al, Ctrl+Y yinele.
                    window.KeyPressQwerty(PhysicalKey.Z, RawInputModifiers.Control);
                    Dispatcher.UIThread.RunJobs();
                    Assert.Equal(original, node.Title);
                    window.KeyPressQwerty(PhysicalKey.Y, RawInputModifiers.Control);
                    Dispatcher.UIThread.RunJobs();
                    Assert.Equal(edited, node.Title);

                    // Gerçek girdi: Ctrl+S kaydet, sonra eşzamanlı tamamla.
                    window.KeyPressQwerty(PhysicalKey.S, RawInputModifiers.Control);
                    Dispatcher.UIThread.RunJobs();
                    Assert.True(vm.SaveProjectNow());
                    savedCount = vm.Nodes.Count;
                    savedTitle = node.Title;
                }
                finally
                {
                    window.Close();
                }
            });

            // Yeniden aç: yeni VM ile düğüm sayısı ve başlık aynı.
            Dispatcher.UIThread.Invoke(() =>
            {
                reopenVm = new MainWindowViewModel(root, connectEngine: false);
                Assert.Equal(savedCount, reopenVm.Nodes.Count);
                Assert.Contains(reopenVm.Nodes, n => n.Title == savedTitle);
            });
        }
        finally
        {
            if (vm != null) Dispatcher.UIThread.Invoke(() => { try { vm.Dispose(); } catch (Exception) { } });
            if (reopenVm != null) Dispatcher.UIThread.Invoke(() => { try { reopenVm.Dispose(); } catch (Exception) { } });
            try { MainWindowViewModel.ProjectRoot = _previousRoot; } catch (Exception) { }
            UndoRedoService.Instance.Clear();
            CleanupParent(parent);
        }
    }

    [Fact]
    public async Task Journey_ImportCancelThenImport_PreviewUsesAsset()
    {
        string parent = NewParent("RowlJourneyB_");
        string? srcDir = null;
        MainWindowViewModel? vm = null;
        try
        {
            var created = ProjectFactory.CreateNewProject("JourneyB", parent);
            Assert.True(created.Success && created.Info != null);
            string root = created.Info!.Path;
            string assetsDir = Path.Combine(root, "Assets");

            // Kaynak Assets dışında, küçük PNG baytları (uçtan uca testteki gibi).
            srcDir = Path.Combine(Path.GetTempPath(), "RowlJourneyImp_" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(srcDir);
            string src = Path.Combine(srcDir, "a.png");
            File.WriteAllBytes(src, new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            string dest = Path.Combine(assetsDir, "images", "a.png");

            // İptal: VM yolu (ekteki iptal testi deseni).
            var cancelVm = new AssetImportViewModel();
            cancelVm.ImportCoreOverride = async (paths, r, ct, prog, resolve) =>
            {
                await Task.Delay(10000, ct).ConfigureAwait(false);
                return new List<string>();
            };
            var run = cancelVm.RunAsync(new[] { src }, assetsDir, null);
            await WaitUntilAsync(() => cancelVm.IsRunning);
            Assert.True(cancelVm.CancelCommand.CanExecute(null));
            cancelVm.CancelCommand.Execute(null);
            await run;
            Assert.False(cancelVm.IsRunning);
            Assert.Equal(RowlEngine.Editor.Services.Strings.EditorStrings.T("Import.Result.Cancelled"), cancelVm.LastResultText);
            Assert.False(File.Exists(dest));
            int partCount = Directory.Exists(assetsDir)
                ? Directory.GetFiles(assetsDir, "*.part", SearchOption.AllDirectories).Length
                : 0;
            Assert.Equal(0, partCount);

            // İptalsiz içe aktar: ekteki uçtan uca testteki servis yolu.
            var imported = EditorAssetImportService.ImportAssetFiles(new[] { src }, assetsDir, msg => { });
            Assert.Single(imported);
            Assert.True(File.Exists(dest));

            // Önizleme: pencere açıkken uçtan uca durum eşitleme adımı.
            Dispatcher.UIThread.Invoke(() =>
            {
                vm = new MainWindowViewModel(root, connectEngine: false);
                var window = new MainWindow(vm) { Width = 1440, Height = 900 };
                window.Show();
                try
                {
                    var node = vm.Nodes.First();
                    vm.SelectedNode = node;
                    Dispatcher.UIThread.RunJobs();
                    if (!vm.EngineHost.IsInitialized)
                        vm.EngineHost.Initialize(1920, 1080, false);
                    vm.EngineHost.SetProjectDirectory(root);
                    Assert.True(vm.PushSceneToEngine(node));
                    vm.SelectNodeQuiet(node);
                    vm.ScheduleEnginePreviewUpdate(node);
                    Assert.False(vm.DeliverScheduledEnginePreview());
                    node.DialogueText = "Journey preview take two.";
                    vm.ScheduleEnginePreviewUpdate(node);
                    Assert.True(vm.DeliverScheduledEnginePreview());
                    Dispatcher.UIThread.RunJobs();
                }
                finally
                {
                    window.Close();
                }
            });
        }
        finally
        {
            if (vm != null) Dispatcher.UIThread.Invoke(() => { try { vm.Dispose(); } catch (Exception) { } });
            try { MainWindowViewModel.ProjectRoot = _previousRoot; } catch (Exception) { }
            UndoRedoService.Instance.Clear();
            CleanupParent(parent);
            CleanupParent(srcDir);
        }
    }

    [Fact]
    public void Journey_SaveAsThenExport()
    {
        string parent = NewParent("RowlJourneyC_");
        string? saveAsParent = null;
        string? pkgFile = null;
        MainWindowViewModel? vm = null;
        try
        {
            var created = ProjectFactory.CreateNewProject("JourneyC", parent);
            Assert.True(created.Success && created.Info != null);
            string root = created.Info!.Path;
            string canonical = Path.Combine(root, "Assets", "json", "full_story_graph.json");

            Dispatcher.UIThread.Invoke(() =>
            {
                vm = new MainWindowViewModel(root, connectEngine: false);
                var window = new MainWindow(vm) { Width = 1440, Height = 900 };
                window.Show();
                try
                {
                    Assert.True(vm.SaveProjectNow());
                    byte[] before = File.ReadAllBytes(canonical);

                    // Farklı kaydet: ekteki SaveAs testi yolu, pencere açıkken.
                    saveAsParent = Path.Combine(Path.GetTempPath(), "RowlJourneySaveAs_" + Guid.NewGuid().ToString("N"));
                    var saveRes = ProjectSaveAsCoordinator.SaveProjectCopy(
                        root,
                        saveAsParent,
                        vm.Nodes.Count,
                        vm.GetStartNode()?.Id ?? 101,
                        () => { vm.SaveProjectNow(); });
                    Assert.True(saveRes.Succeeded && Directory.Exists(saveAsParent));
                    Assert.True(File.Exists(Path.Combine(saveAsParent, "project.rowlproj")));
                    Assert.True(File.Exists(Path.Combine(saveAsParent, "Assets", "json", "full_story_graph.json")));
                    // Eski kök değişmedi.
                    Assert.Equal(before, File.ReadAllBytes(canonical));

                    // Dışa aktar/paketle: ekteki paketleme yolu.
                    pkgFile = Path.Combine(Path.GetTempPath(), "journey_" + Guid.NewGuid().ToString("N") + ".rowlpkg");
                    var pkg = ProjectBuildService.PackageAssetsAsync(
                        Path.Combine(root, "Assets"),
                        pkgFile).GetAwaiter().GetResult();
                    Assert.True(pkg.Succeeded);
                    Assert.True(File.Exists(pkgFile));
                    Assert.True(new FileInfo(pkgFile).Length > 0);

                    // Paketleme yolu işlem sonucu kartını sürmez; kart kablosu
                    // EditorOperationStatusWiringTests'te kilitli. Burada yalnız
                    // başarısızlık görünümü kalmadığı doğrulanır.
                    Assert.NotEqual(OperationState.Failed, vm.OperationStatus.State);
                    Dispatcher.UIThread.RunJobs();
                }
                finally
                {
                    window.Close();
                }
            });
        }
        finally
        {
            if (vm != null) Dispatcher.UIThread.Invoke(() => { try { vm.Dispose(); } catch (Exception) { } });
            try { MainWindowViewModel.ProjectRoot = _previousRoot; } catch (Exception) { }
            UndoRedoService.Instance.Clear();
            CleanupParent(parent);
            CleanupParent(saveAsParent);
            try { if (!string.IsNullOrEmpty(pkgFile) && File.Exists(pkgFile)) File.Delete(pkgFile); } catch (Exception) { }
        }
    }

    [Fact]
    public void Journey_DiscardKeepsDiskBytes()
    {
        string parent = NewParent("RowlJourneyD_");
        MainWindowViewModel? vm = null;
        try
        {
            var created = ProjectFactory.CreateNewProject("JourneyD", parent);
            Assert.True(created.Success && created.Info != null);
            string root = created.Info!.Path;
            string canonical = Path.Combine(root, "Assets", "json", "full_story_graph.json");

            Dispatcher.UIThread.Invoke(() =>
            {
                vm = new MainWindowViewModel(root, connectEngine: false);
                Assert.True(vm.SaveProjectNow());
                byte[] before = File.ReadAllBytes(canonical);

                var window = new MainWindow(vm) { Width = 1440, Height = 900 };
                window.Show();
                try
                {
                    // Kaydetmeden başlık değiştir (gerçek Denetçi girdisi).
                    var node = vm.Nodes.First();
                    vm.SelectedNode = node;
                    Dispatcher.UIThread.RunJobs();
                    string original = node.Title;
                    var inspector = window.GetVisualDescendants().OfType<NodeInspectorView>().First();
                    var box = inspector.GetVisualDescendants().OfType<TextBox>()
                        .First(t => ReferenceEquals(t.DataContext, vm.InspectorScene) && t.Text == original); // Faz 10: yeni Denetçi başlığı
                    box.Focus();
                    Dispatcher.UIThread.RunJobs();
                    box.Text = original + " Discard";
                    Dispatcher.UIThread.RunJobs();
                    Assert.Equal(original + " Discard", node.Title);
                    window.GetVisualDescendants().OfType<NodeGraphView>().First().Focus();
                    Dispatcher.UIThread.RunJobs();

                    // Discard: ekteki yaşam döngüsü testi yolu.
                    bool isDirty = true;
                    bool saved = false;
                    bool ok = EditorProjectLifecycleCoordinator.ResolveUnsavedChangesAsync(
                        () => Task.FromResult<string?>("discard"),
                        () => saved = true,
                        () => isDirty,
                        d => isDirty = d).GetAwaiter().GetResult();
                    Assert.True(ok);
                    Assert.False(isDirty);
                    Assert.False(saved);
                    Assert.Equal(before, File.ReadAllBytes(canonical));
                }
                finally
                {
                    window.Close();
                }
            });
        }
        finally
        {
            if (vm != null) Dispatcher.UIThread.Invoke(() => { try { vm.Dispose(); } catch (Exception) { } });
            try { MainWindowViewModel.ProjectRoot = _previousRoot; } catch (Exception) { }
            UndoRedoService.Instance.Clear();
            CleanupParent(parent);
        }
    }

    [Fact]
    public void Journey_RecoveryCardRestore()
    {
        string parent = NewParent("RowlJourneyE_");
        MainWindowViewModel? vm = null;
        try
        {
            var created = ProjectFactory.CreateNewProject("JourneyE", parent);
            Assert.True(created.Success && created.Info != null);
            string root = created.Info!.Path;
            string canonical = Path.Combine(root, "Assets", "json", "full_story_graph.json");

            // Kurulum: sağlam kayıt, sonra kanoniği boz (kurtarma kartı testi deseni).
            Dispatcher.UIThread.Invoke(() =>
            {
                var seed = new MainWindowViewModel(root, connectEngine: false);
                try { Assert.True(seed.SaveProjectNow()); }
                finally { seed.Dispose(); }
            });
            File.WriteAllText(canonical, "not json{{{");

            Dispatcher.UIThread.Invoke(() =>
            {
                vm = new MainWindowViewModel(root, connectEngine: false);
                var card = vm.RecoveryOffer;
                Assert.NotNull(card);
                int want = card!.NodeCount;
                Assert.True(want > 0);

                var window = new MainWindow(vm) { Width = 1440, Height = 900 };
                window.Show();
                try
                {
                    Dispatcher.UIThread.RunJobs();
                    // Kart görünür: ana penceredeki kurtarma kartı denetimi.
                    var cardView = Assert.Single(window.GetVisualDescendants().OfType<RecoveryOfferCard>());
                    Assert.True(cardView.IsEffectivelyVisible);

                    // İki adımlı geri yükle.
                    card.RestoreCommand.Execute(null);
                    Assert.True(card.IsConfirmingRestore);
                    Assert.Equal("not json{{{", File.ReadAllText(canonical));
                    card.RestoreCommand.Execute(null);
                    Assert.Null(vm.RecoveryOffer);
                    Assert.NotEqual("not json{{{", File.ReadAllText(canonical));
                    Assert.Equal(want, vm.Nodes.Count);
                }
                finally
                {
                    window.Close();
                }
            });
        }
        finally
        {
            if (vm != null) Dispatcher.UIThread.Invoke(() => { try { vm.Dispose(); } catch (Exception) { } });
            try { MainWindowViewModel.ProjectRoot = _previousRoot; } catch (Exception) { }
            UndoRedoService.Instance.Clear();
            CleanupParent(parent);
        }
    }
}
