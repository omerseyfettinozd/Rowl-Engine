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
/// Unity kromu Dilim D kilidi: Hierarchy başlığındaki arama kutusu nesne
/// listesini daraltır (aynı referanslar), temizleme geri yükler, seçim
/// korunur. VM + krom aynı dosyada; pencere açılmaz (UserControl).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorHierarchyFilterTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorHierarchyFilterTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlHierFilter_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("HierFilterTest", parent);
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
    public void Filter_NarrowsRestoresAndPreservesSelection()
    {
        var node = new NodeViewModel(901, "FilterNode", 0, 0, bare: true);
        node.CreateObject("Background Image");
        node.CreateObject("Karakter Sprite");
        node.CreateObject("DIALOGUE Box");
        _vm.SelectedNode = node;

        var hvm = _vm.HierarchyViewModel;
        Assert.Equal(3, hvm.FilteredObjects.Count);
        var selected = hvm.SelectedObject;
        Assert.NotNull(selected);

        // Büyük/küçük harf duyarsız daraltma.
        hvm.HierarchyFilterText = "dialog";
        Assert.Single(hvm.FilteredObjects);
        Assert.Same(selected, hvm.SelectedObject);

        hvm.HierarchyFilterText = "KARAKTER";
        Assert.Single(hvm.FilteredObjects);
        Assert.Equal("Karakter Sprite", hvm.FilteredObjects[0].Name);

        // Sonuç yok: liste boş, seçim korunur.
        hvm.HierarchyFilterText = "uydurma";
        Assert.Empty(hvm.FilteredObjects);
        Assert.Same(selected, hvm.SelectedObject);

        // Temizleme geri yükler (aynı referanslar).
        hvm.HierarchyFilterText = string.Empty;
        Assert.Equal(3, hvm.FilteredObjects.Count);

        // Canlı ekleme filtreye yansır.
        hvm.HierarchyFilterText = "extra";
        var added = node.CreateObject("Extra Prop");
        Assert.Single(hvm.FilteredObjects);
        Assert.Same(added, hvm.FilteredObjects[0]);
    }

    [Fact]
    public void SearchBox_BindsToFilterAndListShowsFilteredObjects()
    {
        var node = new NodeViewModel(902, "ChromeNode", 0, 0, bare: true);
        node.CreateObject("Alpha");
        _vm.SelectedNode = node;

        var view = new NodeHierarchyView { DataContext = _vm };
        var box = view.GetLogicalDescendants().OfType<TextBox>()
            .FirstOrDefault(t => Equals(t.Watermark, "Ara: nesne adı"))
            ?? throw new Exception("Hierarchy search box not found");
        var list = view.GetLogicalDescendants().OfType<ListBox>().FirstOrDefault()
            ?? throw new Exception("Hierarchy ListBox not found");
        if (!ReferenceEquals(_vm.HierarchyViewModel.FilteredObjects, list.ItemsSource))
            throw new Exception("Hierarchy ListBox must bind FilteredObjects");

        // Kutuya yazmak filtreyi sürer (TwoWay bağ).
        box.Text = "alpha";
        Assert.Equal("alpha", _vm.HierarchyViewModel.HierarchyFilterText);
        Assert.Single(_vm.HierarchyViewModel.FilteredObjects);
    }
}
