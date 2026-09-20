using System;
using System.IO;
using Avalonia;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 6 (ızgara sanallaştırma) kilidi: WrapPanel sanallaşmadığı için
/// ızgara veri-penceresiyle çizilir — ListBox VisibleGridItems'a bağlıdır,
/// 200'erlik adımlarla genişler, klasör/filtre değişiminde başa sarar,
/// izleyici yenilemelerinde konumu korur. Headless; pencere açılmaz.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAssetGridVirtualizationTests : IDisposable
{
    private const int ProbeFileCount = 450;

    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorAssetGridVirtualizationTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlGrid_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("GridTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = new MainWindowViewModel(_root, connectEngine: false);
        // KÖK GERİ VERİLMEZ: tarama statik AssetsPath üzerinden yapıldığı
        // için kök test boyunca bizde kalır (koleksiyon zaten ardışıldır);
        // Dispose geri verir.
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

    private void WriteProbeFiles(int count)
    {
        string assetsDir = Path.Combine(_root, "Assets");
        for (int i = 0; i < count; i++)
            File.WriteAllText(Path.Combine(assetsDir, $"viz_{i:0000}.txt"), "x");
        _vm.AssetBrowserViewModel.RefreshAssets();
    }

    [Fact]
    public void Grid_RendersInChunksAndExpandsOnDemand()
    {
        WriteProbeFiles(ProbeFileCount);
        var browser = _vm.AssetBrowserViewModel;

        // 450 dosya + images dizini (json/packages tarayıcıca yoksayılır).
        const int totalItems = ProbeFileCount + 1;
        Assert.Equal(totalItems, browser.FolderContents.Count);
        Assert.Equal(AssetBrowserViewModel.DefaultGridRenderLimit, browser.VisibleGridItems.Count);
        Assert.True(browser.HasMoreGridItems);
        Assert.Equal("200/451 gösteriliyor", browser.GridItemsStatus);

        browser.ShowMoreGridItemsCommand.Execute(null);
        Assert.Equal(400, browser.VisibleGridItems.Count);
        Assert.True(browser.HasMoreGridItems);

        browser.ShowMoreGridItemsCommand.Execute(null);
        Assert.Equal(totalItems, browser.VisibleGridItems.Count);
        Assert.False(browser.HasMoreGridItems);
        Assert.Equal("451 öğe", browser.GridItemsStatus);

        // Pencere aynı referansların ön-ekidir (klon yok).
        Assert.Same(browser.FolderContents[0], browser.VisibleGridItems[0]);
    }

    [Fact]
    public void Grid_ResetsLimitOnFilterAndFolderChange()
    {
        WriteProbeFiles(ProbeFileCount);
        var browser = _vm.AssetBrowserViewModel;
        browser.ShowMoreGridItemsCommand.Execute(null);
        Assert.Equal(400, browser.GridRenderLimit);

        // Filtre değişimi başa sarar ve pencereyi daraltır.
        browser.SearchText = "viz_000";
        Assert.Equal(AssetBrowserViewModel.DefaultGridRenderLimit, browser.GridRenderLimit);
        Assert.Equal(10, browser.VisibleGridItems.Count);
        Assert.False(browser.HasMoreGridItems);
        browser.SearchText = string.Empty;

        // Klasör değişimi başa sarar.
        browser.ShowMoreGridItemsCommand.Execute(null);
        string subDir = Path.Combine(_root, "Assets", "VizAlt");
        Directory.CreateDirectory(subDir);
        File.WriteAllText(Path.Combine(subDir, "alt.txt"), "x");
        browser.RefreshAssets();
        AssetNodeViewModel? folder = null;
        var stack = new System.Collections.Generic.Stack<AssetNodeViewModel>(browser.AssetTree);
        while (stack.Count > 0)
        {
            var node = stack.Pop();
            if (node.IsDirectory && node.Name == "VizAlt") { folder = node; break; }
            foreach (var child in node.Children) stack.Push(child);
        }
        Assert.NotNull(folder);
        browser.EnterFolder(folder);
        Assert.Equal(AssetBrowserViewModel.DefaultGridRenderLimit, browser.GridRenderLimit);
        Assert.Single(browser.VisibleGridItems);
    }
}
