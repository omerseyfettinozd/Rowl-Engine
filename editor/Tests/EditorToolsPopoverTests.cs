using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 6 görsel uyum kilidi: üst kapsüldeki Araçlar (WindowsMenu) açıldığında
/// prototipin .tool-shortcuts kartı gibi 2 sütunlu, 68 px yüksek kartlar dizer
/// (Faz 7: 9 panel → 5 satır). Menü yapısı (9 MenuItem, başlık metni) EditorToolbarLayoutTests'te.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorToolsPopoverTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorToolsPopoverTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlToolsPopover_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("ToolsPopoverTest", parent);
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
    public void ToolsMenu_OpensAsTwoColumnCardGrid()
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                var tools = window.FindControl<MenuItem>("WindowsMenu")
                    ?? throw new Exception("WindowsMenu not found");
                tools.IsSubMenuOpen = true;
                Dispatcher.UIThread.RunJobs();

                var cards = tools.Items.OfType<MenuItem>().ToArray();
                Assert.Equal(9, cards.Length); // Faz 7: + Düğüm kütüphanesi, Lua editörü
                Assert.All(cards, c => Assert.Contains("toolCard", c.Classes));
                Assert.All(cards, c => Assert.NotNull(c.Icon));

                var root = cards[0].GetVisualRoot() as Visual
                    ?? throw new Exception("Tools popover did not open (card has no visual root)");
                (root as Layoutable)?.UpdateLayout();
                var rects = cards.Select(c =>
                {
                    Point p = c.TranslatePoint(default, root) ?? throw new Exception("card not in popup");
                    return new Rect(p, c.Bounds.Size);
                }).ToArray();

                int columns = rects.Select(r => Math.Round(r.X)).Distinct().Count();
                int rows = rects.Select(r => Math.Round(r.Y)).Distinct().Count();
                Assert.True(columns == 2, $"tools popover should have 2 columns, has {columns}");
                Assert.True(rows == 5, $"9 cards in 2 columns should make 5 rows, has {rows}");
                Assert.All(rects, r => Assert.True(r.Height >= 68, $"card height {r.Height} < 68"));
                // Aynı satırdaki kartlar yan yana, eşit genişlikte (UniformGrid).
                Assert.True(Math.Abs(rects[0].Y - rects[1].Y) < 0.5 && rects[1].X > rects[0].X,
                    "first two cards should share a row");
                Assert.True(Math.Abs(rects[0].Width - rects[1].Width) < 0.5, "cards should be equal width");
            }
            finally
            {
                window.Close();
            }
        });
    }
}
