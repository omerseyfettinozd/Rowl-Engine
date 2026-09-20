using System;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorWorkspaceThemeTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n[Test 2]: Theming (Kemik Beyazı + Saf Siyah OLED)...");
        var settings = new SettingsViewModel();
        if (settings.SelectedTheme != "Kemik Beyazı (Karanlık)")
            throw new Exception("Default theme should be Kemik Beyazı (Karanlık)");
        if (settings.AvailableThemes.Count != 2)
            throw new Exception("Expected exactly 2 themes");
        settings.SelectedTheme = "Saf Siyah (OLED)";
        settings.ApplyTheme();
        var app = Avalonia.Application.Current
            ?? throw new Exception("Application.Current is missing");
        if (!app.Resources.TryGetValue("AppBackgroundColor", out var bg) ||
            !(bg is Avalonia.Media.Color bgc && bgc == Avalonia.Media.Color.Parse("#000000")))
            throw new Exception("OLED theme did not apply AppBackgroundColor");
        if (!app.Resources.TryGetValue("PrimaryTextColor", out var fg) ||
            !(fg is Avalonia.Media.Color fgc && fgc == Avalonia.Media.Color.Parse("#F2EFE6")))
            throw new Exception("OLED theme did not apply PrimaryTextColor");
        settings.SelectedTheme = "Kemik Beyazı (Karanlık)";
        settings.ApplyTheme();
        if (!app.Resources.TryGetValue("AppBackgroundColor", out var bg2) ||
            !(bg2 is Avalonia.Media.Color bgc2 && bgc2 == Avalonia.Media.Color.Parse("#0A0A0B")))
            throw new Exception("Default theme did not restore AppBackgroundColor");
        Console.WriteLine("  [PASS] Theme switch (Kemik Beyazı <-> Saf Siyah OLED) verified");

        mainVm.ShowPanel("Hierarchy");
        if (mainVm.HierarchyPanelWidth.Value != 0 || mainVm.HierarchySplitterWidth.Value != 0)
            throw new Exception("Hidden Hierarchy still reserved workspace width");
        mainVm.ShowPanel("Hierarchy");
        if (mainVm.HierarchyPanelWidth.Value != 240 || mainVm.HierarchySplitterWidth.Value != 6)
            throw new Exception("Hierarchy did not restore its workspace width");
        mainVm.ShowPanel("Inspector");
        if (mainVm.InspectorPanelWidth.Value != 0 || mainVm.InspectorSplitterWidth.Value != 0)
            throw new Exception("Hidden Inspector still reserved workspace width");
        mainVm.ShowPanel("Inspector");
        if (mainVm.InspectorPanelWidth.Value != 280 || mainVm.InspectorSplitterWidth.Value != 6)
            throw new Exception("Inspector did not restore its workspace width");
        Console.WriteLine("  [PASS] Hidden side panels release workspace width");

        mainVm.ShowPanel("Log");
        if (mainVm.IsLogPanelVisible || !mainVm.IsAssetsPanelVisible || !mainVm.IsBottomPanelVisible)
            throw new Exception("Closing Log incorrectly hid the Assets workspace");
        mainVm.ShowPanel("Assets");
        if (!mainVm.IsAssetsPanelVisible || mainVm.BottomPanelActiveTab != 1 || !mainVm.IsBottomPanelVisible)
            throw new Exception("Assets panel could not become the active bottom workspace");
        mainVm.ShowPanel("Assets");
        if (mainVm.IsBottomPanelVisible)
            throw new Exception("Bottom workspace remained visible after both tabs were closed");
        if (mainVm.BottomPanelHeight.Value != 0 || mainVm.BottomSplitterHeight.Value != 0)
            throw new Exception("Hidden bottom workspace still reserved height");
        mainVm.ShowPanel("Assets");
        if (!mainVm.IsBottomPanelVisible || mainVm.BottomPanelActiveTab != 1 ||
            mainVm.BottomPanelHeight.Value != 180 || mainVm.BottomSplitterHeight.Value != 6)
        {
            throw new Exception("Assets panel did not restore independently");
        }
        mainVm.ShowPanel("Backlog");
        if (!mainVm.IsBacklogPanelVisible || mainVm.BottomPanelActiveTab != 2 || !mainVm.IsBottomPanelVisible)
            throw new Exception("Dialogue backlog panel did not become an independent bottom workspace");
        mainVm.ShowPanel("Backlog");
        if (mainVm.IsBacklogPanelVisible)
            throw new Exception("Dialogue backlog panel did not close independently");
        Console.WriteLine(
            "  [PASS] Bottom Log and Assets panel visibility is independent and reclaims height");

        mainVm.ShowPanel("SplitScreen");
        mainVm.ShowPanel("Preview");
        if (mainVm.SplitScreenMode != 0 || !mainVm.IsPreviewActive || mainVm.IsNodeGraphActive)
            throw new Exception("Preview mode did not exit split screen cleanly");
        mainVm.ShowPanel("SplitScreen");
        mainVm.ShowPanel("EnginePreview");
        if (mainVm.SplitScreenMode != 0 || !mainVm.IsEnginePreviewActive || mainVm.IsNodeGraphActive)
            throw new Exception("Game preview mode did not exit split screen cleanly");
        mainVm.ShowPanel("NodeGraph");
        Console.WriteLine("  [PASS] Single preview modes exit split screen consistently");
    }
}
