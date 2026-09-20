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
/// Unity kromu Dilim C kilidi: canvas-içi sahne başlık şeridi (zoom +/−,
/// Sığdır/Seçim, minimap anahtarı). NodeGraphView bir UserControl olduğu
/// için pencere platformu gerekmez — doğrudan kurulur, DataContext okunur.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorSceneViewHeaderTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorSceneViewHeaderTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlSceneHeader_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("SceneHeaderTest", parent);
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
    public void SceneViewHeader_HostsZoomFitAndMinimapToggle()
    {
        var view = new NodeGraphView { DataContext = _vm };
        var header = view.FindControl<Border>("SceneViewHeader")
            ?? throw new Exception("SceneViewHeader not found");

        // NOT: CheckBox ToggleButton'dan (o da Button'dan) türer; sayımda
        // Harita anahtarı elenir.
        var buttons = header.GetLogicalDescendants().OfType<Button>()
            .Where(b => b is not CheckBox).ToList();
        if (buttons.Count != 4)
            throw new Exception($"SceneViewHeader should have 4 buttons, has {buttons.Count}");
        if (buttons.Any(b => b.Command is null))
            throw new Exception("SceneViewHeader buttons must all be wired to commands");
        string?[] tips = buttons.Select(b => ToolTip.GetTip(b) as string).ToArray();
        string?[] expectedTips =
            { "Uzaklaştır", "Yakınlaştır", "Tüm grafı ekrana sığdır", "Seçili düğüme odaklan (F)" };
        if (!tips.SequenceEqual(expectedTips))
            throw new Exception($"SceneViewHeader tooltips changed: [{string.Join(", ", tips)}]");

        // Zoom rozeti başlıkta yaşar (Test 37'nin overlay yasağı delinmez).
        var zoomLabel = header.GetLogicalDescendants().OfType<TextBlock>()
            .FirstOrDefault(t => (ToolTip.GetTip(t) as string) == "Yakınlaştırma")
            ?? throw new Exception("Zoom label lost from SceneViewHeader");
        if (string.IsNullOrEmpty(zoomLabel.Text))
            throw new Exception("Zoom label is not bound");

        // Minimap anahtarı kartı açıp kapatır.
        var card = view.FindControl<Border>("MinimapCard")
            ?? throw new Exception("MinimapCard not found");
        if (!card.IsVisible)
            throw new Exception("MinimapCard must be visible by default");
        _vm.IsMinimapVisible = false;
        if (card.IsVisible)
            throw new Exception("Minimap toggle did not hide the card");
        _vm.IsMinimapVisible = true;
        if (!card.IsVisible)
            throw new Exception("Minimap toggle did not restore the card");

        // Reset butonu yerinde, metni kilitli (Test 39 ile aynı beklenti).
        var reset = view.FindControl<Button>("ResetViewButton")
            ?? throw new Exception("ResetViewButton lost from NodeGraphView");
        if ((reset.Content as string) != "Görünümü Sıfırla (0,0)")
            throw new Exception("ResetViewButton text changed");

        // Boş sahnede Sığdır çökmez.
        _vm.NodeGraphViewModel.ZoomToFitCommand.Execute(null);
    }

    [Fact]
    public void ZoomStep_ClampsToGraphLimits()
    {
        _vm.TargetZoom = 1.0;
        _vm.ZoomInViewCommand.Execute(null);
        Assert.Equal(1.2, _vm.TargetZoom, precision: 4);

        _vm.TargetZoom = NodeGraphViewModel.MaxZoom;
        _vm.ZoomInViewCommand.Execute(null);
        Assert.Equal(NodeGraphViewModel.MaxZoom, _vm.TargetZoom);

        _vm.TargetZoom = NodeGraphViewModel.MinZoom;
        _vm.ZoomOutViewCommand.Execute(null);
        Assert.Equal(NodeGraphViewModel.MinZoom, _vm.TargetZoom);
    }
}
