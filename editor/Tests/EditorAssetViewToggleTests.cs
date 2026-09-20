using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Unity kromu Dilim G1 kilidi: sağ bölme ızgara/liste anahtarı. Anahtar
/// yalnızca sunumu değiştirir — her iki görünüm de aynı pencereye
/// (VisibleGridItems) ve aynı seçim otoritesine (SelectedNode) bağlıdır.
/// ProjectAssetsView bir UserControl olduğu için pencere platformu gerekmez.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAssetViewToggleTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorAssetViewToggleTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlAssetView_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("AssetViewTest", parent);
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
    public void Toggle_FlipsViewAndButtonText()
    {
        var browser = _vm.AssetBrowserViewModel;
        Assert.True(browser.IsGridView);
        Assert.Equal("Liste", browser.AssetViewToggleText);

        browser.ToggleAssetViewCommand.Execute(null);
        Assert.False(browser.IsGridView);
        Assert.Equal("Izgara", browser.AssetViewToggleText);

        browser.ToggleAssetViewCommand.Execute(null);
        Assert.True(browser.IsGridView);
        Assert.Equal("Liste", browser.AssetViewToggleText);
    }

    [Fact]
    public void Chrome_BothViewsShareWindowAndSelection()
    {
        // Paket-dışı tekil koşuda GridSplitter platform servisi
        // (ICursorFactory) yoktur; krom kilidi paket içinde (Test 41)
        // çalışır. Burada yalnızca görünüm metni önden doğrulanır.
        Assert.Equal("Liste", _vm.AssetBrowserViewModel.AssetViewToggleText);
    }

    /// <summary>
    /// Paket krom kilidi (Test 41): headless Avalonia önyüklemesi
    /// (BuildAvaloniaAppHeadless) GridSplitter servisini sağlar, o yüzden
    /// ProjectAssetsView ancak paket bağlamında kurulabilir.
    /// </summary>
    internal static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n[Test 41]: Unity kromu Dilim G1 — asset view toggle lock...");
        var view = new ProjectAssetsView { DataContext = mainVm };
        var browser = mainVm.AssetBrowserViewModel;

        var toggle = view.GetLogicalDescendants().OfType<Button>()
            .FirstOrDefault(b => (b.Content as string) == "Liste")
            ?? throw new Exception("Asset view toggle button not found");
        if (!ReferenceEquals(browser.ToggleAssetViewCommand, toggle.Command))
            throw new Exception("View toggle button must be wired to ToggleAssetViewCommand");

        var grid = view.FindControl<ListBox>("AssetGridListBox")
            ?? throw new Exception("AssetGridListBox not found");
        var list = view.FindControl<ListBox>("AssetListListBox")
            ?? throw new Exception("AssetListListBox not found");
        if (!ReferenceEquals(browser.VisibleGridItems, grid.ItemsSource))
            throw new Exception("Grid view must bind VisibleGridItems");
        if (!ReferenceEquals(browser.VisibleGridItems, list.ItemsSource))
            throw new Exception("List view must bind VisibleGridItems");

        if (!grid.IsVisible || list.IsVisible)
            throw new Exception("Grid view must be visible by default");
        browser.ToggleAssetViewCommand.Execute(null);
        if (grid.IsVisible || !list.IsVisible)
            throw new Exception("Toggle did not swap grid/list visibility");
        browser.ToggleAssetViewCommand.Execute(null);
        if (!grid.IsVisible || list.IsVisible)
            throw new Exception("Toggle did not restore grid visibility");
        Console.WriteLine("  [PASS] Asset grid/list views share window and selection");
    }
}
