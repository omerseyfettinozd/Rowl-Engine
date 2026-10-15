using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Unity kromu Dilim G2 kilidi: Düzen önayarları (Varsayılan / Sahne Odağı /
/// Oyun Testi) panelleri açık/kapalı olarak atar (geçiş yapmaz). VM testi
/// paketsiz koşar; MainWindow kromu paket bağlamında (Test 42) kilitlenir.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorLayoutPresetTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorLayoutPresetTests()
    {
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlLayout_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("LayoutTest", parent);
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
    public void Presets_AssignPanelsExplicitly()
    {
        Assert.Equal(new[] { "Varsayılan", "Sahne Odağı", "Oyun Testi" },
            _vm.AvailableLayoutPresets.ToArray());
        Assert.Equal("Varsayılan", _vm.SelectedLayoutPreset);

        _vm.SelectedLayoutPreset = "Sahne Odağı";
        Assert.False(_vm.IsHierarchyPanelVisible);
        Assert.False(_vm.IsInspectorPanelVisible);
        Assert.True(_vm.IsAssetsPanelVisible);
        // Tek şerit: Varlıklar sekmesi açıkken alt şerit görünür
        // (yalnızca Varlıklar sekmesiyle).
        Assert.True(_vm.IsBottomPanelVisible);
        Assert.Equal(0, _vm.BottomPanelActiveTab);
        Assert.True(_vm.IsNodeGraphActive);

        _vm.SelectedLayoutPreset = "Oyun Testi";
        Assert.True(_vm.IsEnginePreviewActive);
        Assert.False(_vm.IsNodeGraphActive);
        Assert.False(_vm.IsAssetsPanelVisible);
        Assert.True(_vm.IsInspectorPanelVisible);
        Assert.False(_vm.IsHierarchyPanelVisible);

        _vm.SelectedLayoutPreset = "Varsayılan";
        Assert.True(_vm.IsHierarchyPanelVisible);
        Assert.True(_vm.IsInspectorPanelVisible);
        Assert.True(_vm.IsAssetsPanelVisible);
        Assert.True(_vm.IsLogPanelVisible);
        Assert.True(_vm.IsNodeGraphActive);

        // Bilinmeyen ad no-op'tur.
        _vm.SelectedLayoutPreset = "Uydurma Düzen";
        Assert.True(_vm.IsHierarchyPanelVisible);
        Assert.True(_vm.IsNodeGraphActive);
    }

    /// <summary>
    /// Paket krom kilidi (Test 42): Düzen ComboBox'ı kilitli kapların
    /// dışındadır, listeye ve seçime iki yönlü bağlıdır.
    /// </summary>
    internal static void Run(MainWindowViewModel mainVm)
    {
        if (Avalonia.Application.Current != null && !Dispatcher.UIThread.CheckAccess())
        {
            Dispatcher.UIThread.Invoke(() => Run(mainVm));
            return;
        }

        Console.WriteLine("\n[Test 42]: Unity kromu Dilim G2 — layout preset lock...");
        var window = new Views.MainWindow(mainVm);
        try
        {
            // Faz 3 (K4): açılır kutu yerine "…" > Düzen menüsü.
            var layoutMenu = window.FindControl<MenuItem>("LayoutMenu")
                ?? throw new Exception("LayoutMenu not found");
            var presetItems = layoutMenu.Items.OfType<MenuItem>().ToList();
            string?[] presetParams = presetItems.Select(m => m.CommandParameter as string).ToArray();
            if (!presetParams.SequenceEqual(mainVm.AvailableLayoutPresets))
                throw new Exception("Layout menu must list AvailableLayoutPresets in order");
            if (mainVm.SelectedLayoutPreset != "Varsayılan")
                throw new Exception("Layout preset must default to Varsayılan");

            var sceneFocus = presetItems.Single(m => (m.CommandParameter as string) == "Sahne Odağı");
            sceneFocus.Command!.Execute(sceneFocus.CommandParameter);
            if (mainVm.SelectedLayoutPreset != "Sahne Odağı")
                throw new Exception("Layout menu did not select Sahne Odağı");
            if (mainVm.IsHierarchyPanelVisible || mainVm.IsInspectorPanelVisible)
                throw new Exception("Sahne Odağı did not hide side panels");
            mainVm.SelectLayoutPresetCommand.Execute("Bilinmeyen");
            if (mainVm.SelectedLayoutPreset != "Sahne Odağı")
                throw new Exception("Unknown preset name must be ignored");
        }
        finally
        {
            mainVm.SelectedLayoutPreset = "Varsayılan";
        }
        Console.WriteLine("  [PASS] Layout presets assign panels and the … > Düzen menu stays wired");
    }
}
