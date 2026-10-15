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
        // Faz 1 (EDITOR_UI_PORT_PLAN K1-K3): OLED, prototipten türetilen
        // varsayılan paletin bir kademe koyusu (App #000000 + metin #F2EFE6 sabit).
        if (!app.Resources.TryGetValue("SurfaceBackgroundColor", out var obg) ||
            !(obg is Avalonia.Media.Color obgc && obgc == Avalonia.Media.Color.Parse("#0B0B0D")))
            throw new Exception("OLED theme did not apply shifted SurfaceBackgroundColor");
        if (!app.Resources.TryGetValue("PanelBackgroundColor", out var opnl) ||
            !(opnl is Avalonia.Media.Color opnlc && opnlc == Avalonia.Media.Color.Parse("#111113")))
            throw new Exception("OLED theme did not apply shifted PanelBackgroundColor");
        settings.SelectedTheme = "Kemik Beyazı (Karanlık)";
        settings.ApplyTheme();
        if (!app.Resources.TryGetValue("AppBackgroundColor", out var bg2) ||
            !(bg2 is Avalonia.Media.Color bgc2 && bgc2 == Avalonia.Media.Color.Parse("#0A0A0B")))
            throw new Exception("Default theme did not restore AppBackgroundColor");
        // Faz 1: varsayılan tema yüzeyleri web prototipinin değerleri
        // (--surface #131315, panel #19191C; App #0A0A0B sabit).
        if (!app.Resources.TryGetValue("SurfaceBackgroundColor", out var dbg) ||
            !(dbg is Avalonia.Media.Color dbgc && dbgc == Avalonia.Media.Color.Parse("#131315")))
            throw new Exception("Default theme did not apply shifted SurfaceBackgroundColor");
        if (!app.Resources.TryGetValue("PanelBackgroundColor", out var dpnl) ||
            !(dpnl is Avalonia.Media.Color dpnlc && dpnlc == Avalonia.Media.Color.Parse("#19191C")))
            throw new Exception("Default theme did not apply shifted PanelBackgroundColor");
        Console.WriteLine("  [PASS] Theme switch (Kemik Beyazı <-> Saf Siyah OLED) verified");

        mainVm.ShowPanel("Hierarchy");
        if (mainVm.HierarchyPanelWidth.Value != 0 || mainVm.HierarchySplitterWidth.Value != 0)
            throw new Exception("Hidden Hierarchy still reserved workspace width");
        mainVm.ShowPanel("Hierarchy");
        if (mainVm.HierarchyPanelWidth.Value != 200 || mainVm.HierarchySplitterWidth.Value != 6)
            throw new Exception("Hierarchy did not restore its workspace width");
        mainVm.ShowPanel("Inspector");
        if (mainVm.InspectorPanelWidth.Value != 0 || mainVm.InspectorSplitterWidth.Value != 0)
            throw new Exception("Hidden Inspector still reserved workspace width");
        mainVm.ShowPanel("Inspector");
        if (mainVm.InspectorPanelWidth.Value != 280 || mainVm.InspectorSplitterWidth.Value != 6)
            throw new Exception("Inspector did not restore its workspace width");
        Console.WriteLine("  [PASS] Hidden side panels release workspace width");

        // Tek şerit: Varlıklar ilk sekme (0), Günlük ikinci (1);
        // ikisi aynı alanı paylaşır, bağımsız açılıp kapanır. Şerit,
        // sekmelerden en az biri açıkken yüksekliğini korur. Kapanan
        // aktif sekmede indeks görünen ilk sekmeye çekilir.
        mainVm.ShowPanel("Assets");
        if (mainVm.IsAssetsPanelVisible)
            throw new Exception("Assets tab did not close independently");
        if (!mainVm.IsBottomPanelVisible)
            throw new Exception("Closing Assets incorrectly hid the shared strip");
        if (mainVm.BottomPanelActiveTab != 1)
            throw new Exception("Active tab did not fall back to the visible Log tab");
        if (mainVm.BottomPanelHeight.Value != 180 || mainVm.BottomSplitterHeight.Value != 6)
            throw new Exception("Shared strip did not keep its height after Assets closed");
        mainVm.ShowPanel("Log");
        if (mainVm.IsLogPanelVisible || mainVm.IsBottomPanelVisible)
            throw new Exception("Bottom workspace remained visible after both tabs were closed");
        if (mainVm.BottomPanelHeight.Value != 0 || mainVm.BottomSplitterHeight.Value != 0)
            throw new Exception("Hidden bottom workspace still reserved height");
        mainVm.ShowPanel("Assets");
        if (!mainVm.IsAssetsPanelVisible || !mainVm.IsBottomPanelVisible
            || mainVm.BottomPanelHeight.Value != 180 || mainVm.BottomSplitterHeight.Value != 6
            || mainVm.BottomPanelActiveTab != 0)
        {
            throw new Exception("Assets tab did not restore the shared strip");
        }
        mainVm.ShowPanel("Backlog");
        if (!mainVm.IsBacklogPanelVisible || mainVm.BottomPanelActiveTab != 2 || !mainVm.IsBottomPanelVisible)
            throw new Exception("Dialogue backlog panel did not become an independent bottom workspace");
        mainVm.ShowPanel("Backlog");
        if (mainVm.IsBacklogPanelVisible)
            throw new Exception("Dialogue backlog panel did not close independently");
        if (mainVm.BottomPanelActiveTab != 0)
            throw new Exception("Active tab did not fall back to the visible Assets tab");
        if (mainVm.IsLogPanelVisible || !mainVm.IsAssetsPanelVisible)
            throw new Exception("Bottom tabs did not restore their default state");
        Console.WriteLine(
            "  [PASS] Bottom tabs share one strip, toggle independently and reclaim height");

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
