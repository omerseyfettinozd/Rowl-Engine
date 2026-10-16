using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Controls;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 8 sonrası iki görsel açık (1440×900 pencere):
/// 1) Node paneli darken (Assets Store sağda açık) sağ üstteki mini harita sol üstteki araç
///    satırıyla (kırıntı + düğüm komutları) yatayda çakışıyordu. Artık satırın altına iner.
///    Bozuk sürüm: kart hep Margin="16" ile sağ üstte → kesişim.
/// 2) Sağ alt bildirim yığını üç kart birlikteyken ~440 px yükselip Denetçi/Varlıklar'ı
///    örtüyordu. Artık kart alanı pencerenin %40'ı ile sınırlı ve kaydırılır.
///    Bozuk sürüm: ScrollViewer / MaxHeight yok → yığın sınırsız uzar.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorOverlayCollisionTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorOverlayCollisionTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlOverlay_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("OverlayTest", parent);
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
    }

    // ── Saf hesap ─────────────────────────────────────────────────────

    [Fact]
    public void Placement_WidePanel_StaysTopRight()
    {
        var r = MinimapPlacement.Resolve(new Size(900, 600), new Rect(12, 12, 420, 38), new Size(210, 140));
        Assert.Equal(new MinimapPlacement.Result(MinimapPlacement.EdgeMargin, true, false), r);
    }

    [Fact]
    public void Placement_NarrowPanel_DropsBelowToolRow()
    {
        var row = new Rect(12, 12, 420, 38);
        var r = MinimapPlacement.Resolve(new Size(576, 590), row, new Size(210, 140));
        Assert.True(r.IsDropped);
        Assert.True(r.IsVisible);
        Assert.Equal(row.Bottom + MinimapPlacement.Gap, r.Top);
    }

    [Fact]
    public void Placement_NarrowAndShortPanel_Hides()
    {
        var r = MinimapPlacement.Resolve(new Size(400, 240), new Rect(12, 12, 420, 38), new Size(210, 140));
        Assert.False(r.IsVisible);
    }

    [Fact]
    public void NotificationMetrics_CapsAtRatioWithFloor()
    {
        Assert.Equal(360, NotificationStackMetrics.MaxCardsHeight(900));
        Assert.Equal(NotificationStackMetrics.MinCardsHeight, NotificationStackMetrics.MaxCardsHeight(200));
        Assert.True(double.IsPositiveInfinity(NotificationStackMetrics.MaxCardsHeight(0)));
    }

    // ── Gerçek pencere (başsız) ───────────────────────────────────────

    [Theory]
    [InlineData("Store")]
    [InlineData("Library")]
    public void NarrowNodePanel_MinimapDoesNotOverlapToolRow(string sidePanel)
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            _vm.ShowPanel(sidePanel);
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                Settle(window);
                var row = Find<StackPanel>(window, "CanvasToolRow");
                var card = Find<Border>(window, "MinimapCard");
                var rowRect = RectIn(window, row);
                var cardRect = RectIn(window, card);

                // Ön koşul: bu boyutta panel gerçekten dar (yan yana sığmıyor).
                Assert.True(rowRect.Right + MinimapPlacement.Gap > cardRect.Right - cardRect.Width,
                    $"Ön koşul tutmadı: araç satırı {rowRect}, harita {cardRect}");
                if (card.IsEffectivelyVisible)
                    Assert.False(rowRect.Intersects(cardRect),
                        $"Mini harita araç satırına biniyor: satır {rowRect}, harita {cardRect}");
            }
            finally
            {
                window.Close();
            }
        });
    }

    [Fact]
    public void ThreeCards_StackIsCappedAndScrolls()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            _vm.RecoveryOffer = new RecoveryOfferViewModel(
                new RestoreOffer("journal",
                    "{\"format_version\":5,\"nodes\":[{\"title\":\"A\"},{\"title\":\"B\"},{\"title\":\"C\"}]}", 5, 3),
                "Unexpected end of JSON", () => true, () => { });
            _vm.AssetImport.IsRunning = true;
            _vm.AssetImport.Total = 12;
            _vm.AssetImport.Completed = 5;
            _vm.AssetImport.CurrentFile = "koridor_gece.png";
            _vm.AssetImport.PendingConflict = new ImportConflict("/tmp/koridor_gece.png", "images/koridor_gece.png");
            _vm.OperationStatus.Fail(OperationKind.Export, "disk dolu");

            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                Settle(window);
                var stack = Find<StackPanel>(window, "NotificationStack");
                var shell = (Control)stack.Parent!;
                double cap = NotificationStackMetrics.MaxCardsHeight(shell.Bounds.Height);
                var scroller = Find<ScrollViewer>(window, "NotificationCardScroller");

                Assert.True(scroller.Bounds.Height <= cap + 0.5,
                    $"Kart alanı sınırı aşıyor: {scroller.Bounds.Height:0} > {cap:0}");
                Assert.True(stack.Bounds.Height <= shell.Bounds.Height * 0.5,
                    $"Bildirim yığını kabuğun yarısından uzun: {stack.Bounds.Height:0} / {shell.Bounds.Height:0}");
                // Hiçbir kart kaybolmaz: fazlası kaydırmayla erişilebilir.
                Assert.True(scroller.Extent.Height > scroller.Viewport.Height + 0.5,
                    $"Üç kart sığmamalı ve kaydırılmalı: extent {scroller.Extent.Height:0}, viewport {scroller.Viewport.Height:0}");
            }
            finally
            {
                window.Close();
            }
        });
    }

    private static void Settle(Window window)
    {
        for (int i = 0; i < 3; i++)
        {
            Dispatcher.UIThread.RunJobs();
            window.UpdateLayout();
        }
    }

    private static T Find<T>(Window window, string name) where T : Control
        => window.GetVisualDescendants().OfType<T>().FirstOrDefault(c => c.Name == name)
           ?? throw new Exception(name + " not found");

    private static Rect RectIn(Visual root, Visual v)
    {
        var origin = v.TranslatePoint(new Point(0, 0), root)
                     ?? throw new Exception("translate failed for " + v);
        return new Rect(origin, v.Bounds.Size);
    }
}
