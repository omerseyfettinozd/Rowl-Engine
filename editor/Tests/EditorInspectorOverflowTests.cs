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
/// Unity kromu Dilim G3 kilidi: Denetçi kartındaki "..." taşma menüsü ve
/// BİLEŞENLER şeridindeki "..." kısayolu yalnızca mevcut komutlara bağlıdır
/// (yeni davranış yok). VM testi paketsiz koşar; NodeInspectorView içindeki
/// ColorPickerControl kurucuda imleç istediği için krom kilidi paket
/// bağlamında (Test 43) çalışır — G1'deki GridSplitter deseniyle aynı.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorInspectorOverflowTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorInspectorOverflowTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlInspectorMenu_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("InspectorMenuTest", parent);
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
    public void OverflowCommands_RoundTripOnViewModel()
    {
        var node = new NodeViewModel(903, "InspectorNode", 0, 0, bare: true);
        var original = node.CreateObject("Kahraman");
        _vm.SelectedNode = node;
        Assert.True(_vm.InspectorViewModel.HasSelectedObject);

        // "..." menüsünün tetikleyeceği komutlar gerçekten çalışır.
        var hvm = _vm.HierarchyViewModel;
        Assert.Equal(1, hvm.FilteredObjects.Count);
        hvm.DuplicateObjectCommand.Execute(hvm.SelectedObject);
        Assert.Equal(2, hvm.FilteredObjects.Count);
        var copy = hvm.FilteredObjects.First(o => !ReferenceEquals(o, original));
        hvm.DeleteObjectCommand.Execute(copy);
        Assert.Single(hvm.FilteredObjects);
        Assert.Same(original, hvm.FilteredObjects[0]);

        // BİLEŞENLER "..." kısayolu Ekle menüsünü açıp kapatır.
        Assert.False(_vm.IsAddComponentMenuOpen);
        _vm.ShowAddComponentMenuCommand.Execute(null);
        Assert.True(_vm.IsAddComponentMenuOpen);
        _vm.ShowAddComponentMenuCommand.Execute(null);
        Assert.False(_vm.IsAddComponentMenuOpen);
    }

    /// <summary>
    /// Paket krom kilidi (Test 43): iki "..." butonu, kart menüsünün sırası
    /// ve mevcut komut kablosu, şerit kısayolunun görünürlük turu.
    /// </summary>
    internal static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n[Test 43]: Unity kromu Dilim G3 — inspector overflow lock...");
        var previous = mainVm.SelectedNode;
        var node = new NodeViewModel(904, "InspectorMenuNode", 0, 0, bare: true);
        node.CreateObject("Menü Nesnesi");
        mainVm.SelectedNode = node;
        try
        {
            if (!mainVm.InspectorViewModel.HasSelectedObject)
                throw new Exception("Inspector must report a selected object");

            var view = new NodeInspectorView { DataContext = mainVm };
            var dots = view.GetLogicalDescendants().OfType<Button>()
                .Where(b => (b.Content as string) == "...").ToList();
            if (dots.Count != 2)
                throw new Exception($"Expected 2 overflow buttons, found {dots.Count}");

            // Başlıklar korunur (Test 39 paritesi).
            var texts = view.GetLogicalDescendants().OfType<TextBlock>()
                .Select(t => t.Text).ToList();
            if (!texts.Contains("DENETÇİ") || !texts.Contains("BİLEŞENLER"))
                throw new Exception("Inspector headers must survive the overflow edit");

            // Kart 1: nesne işlemleri menüsü (sıra + mevcut komut kablosu).
            var cardMenu = dots.FirstOrDefault(b => b.Flyout is MenuFlyout)
                ?? throw new Exception("Object overflow menu button not found");
            var items = ((MenuFlyout)cardMenu.Flyout!).Items.OfType<MenuItem>().ToList();
            string[] expected = ["Çoğalt", "Sil", "Yukarı Taşı", "Aşağı Taşı"];
            string[] actual = items.Select(i => i.Header as string).ToArray()!;
            if (!expected.SequenceEqual(actual))
                throw new Exception("Object menu must list Çoğalt/Sil/Yukarı Taşı/Aşağı Taşı");
            var hvm = mainVm.HierarchyViewModel;
            if (!ReferenceEquals(hvm.DuplicateObjectCommand, items[0].Command)
                || !ReferenceEquals(hvm.DeleteObjectCommand, items[1].Command)
                || !ReferenceEquals(hvm.MoveObjectUpCommand, items[2].Command)
                || !ReferenceEquals(hvm.MoveObjectDownCommand, items[3].Command))
                throw new Exception("Object menu must reuse HierarchyViewModel commands");

            // BİLEŞENLER şeridi: "..." Ekle komutunun kısayoludur.
            var strip = dots.FirstOrDefault(b => b.Command != null)
                ?? throw new Exception("Component overflow shortcut not found");
            if (!ReferenceEquals(mainVm.ShowAddComponentMenuCommand, strip.Command))
                throw new Exception("Component shortcut must reuse ShowAddComponentMenuCommand");
            if (!strip.IsVisible)
                throw new Exception("Component shortcut must be visible when add menu is closed");
            mainVm.ShowAddComponentMenuCommand.Execute(null);
            if (strip.IsVisible)
                throw new Exception("Component shortcut must hide while add menu is open");
            mainVm.ShowAddComponentMenuCommand.Execute(null);
            if (!strip.IsVisible)
                throw new Exception("Component shortcut must return when add menu closes");
        }
        finally
        {
            mainVm.SelectedNode = previous;
        }
        Console.WriteLine("  [PASS] Inspector overflow menus reuse existing commands");
    }
}
