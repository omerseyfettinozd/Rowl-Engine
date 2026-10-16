using System;
using Avalonia;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views.Workspace;

namespace RowlEngine.Editor;

internal static class EditorWorkspaceThemeTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        // Uygulama kaynakları UI thread'ine bağlı: Avalonia başka bir testte UI thread'iyle
        // başlatıldıysa tema değişimi o thread'de yapılmalı (süitin diğer adımlarıyla aynı kalıp).
        if (Application.Current != null && !Avalonia.Threading.Dispatcher.UIThread.CheckAccess())
        {
            Avalonia.Threading.Dispatcher.UIThread.Invoke(() => Run(mainVm));
            return;
        }
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

        // Faz 2: ölçek token'ları, Fluent vurgu tek renk (K3) ve stil sınıfları
        // gerçekten uygulanıyor (yalnız tanımlı değil).
        if (!app.TryGetResource("FontSizeCaption", null, out var caption) || caption is not double cs || cs != 9)
            throw new Exception("FontSizeCaption token missing or not 9");
        if (!app.TryGetResource("ControlHeight", null, out var ch) || ch is not double chv || chv != 32)
            throw new Exception("ControlHeight token missing or not 32 (K2)");
        if (!app.TryGetResource("SystemAccentColor", Avalonia.Styling.ThemeVariant.Dark, out var accent) ||
            accent is not Avalonia.Media.Color ac || ac != Avalonia.Media.Color.Parse("#D4CBB6"))
            throw new Exception("Fluent accent still blue: SystemAccentColor override not applied (K3)");
        Avalonia.Threading.Dispatcher.UIThread.Invoke(() =>
        {
            var probe = new Avalonia.Controls.TextBlock { Text = "x" };
            probe.Classes.Add("caption");
            var probeWindow = new Avalonia.Controls.Window { Content = probe };
            probeWindow.Show();
            try
            {
                if (probe.FontSize != 9)
                    throw new Exception($"TextBlock.caption style not applied (FontSize={probe.FontSize})");
            }
            finally
            {
                probeWindow.Close();
            }
        });
        Console.WriteLine("  [PASS] Faz 2 scale tokens, accent override and style classes applied");

        // Faz 5 D3: panel boyutları sabit piksel özelliklerinden değil, ağaç oranlarından
        // WorkspaceHost'ta çizilir. Davranış eşdeğeri ölçülür: 1440 px genişlikte (çalışma
        // alanı 1440×820 = 1440×900 pencere − üst çubuk/durum çubuğu) varsayılan yerleşim
        // Hiyerarşi ≈ 200, Denetçi ≈ 280, alt şerit ≈ 180 px (±%10); gizli panel yer tutmaz.
        Avalonia.Threading.Dispatcher.UIThread.Invoke(() => RunWorkspaceSizes(mainVm));
        Console.WriteLine("  [PASS] Hidden side panels release workspace width");
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

    private static void RunWorkspaceSizes(MainWindowViewModel mainVm)
    {
        var host = new WorkspaceHost();
        host.Bind(WorkspaceHost.LayoutProperty, new Avalonia.Data.Binding(nameof(MainWindowViewModel.WorkspaceLayout)) { Source = mainVm });
        host.SplitResized += (_, e) => mainVm.ResizeWorkspaceSplit(e.SplitId, e.Ratio);
        foreach ((string id, double minW, double minH) in new[]
        {
            (PanelIds.Hierarchy, WorkspacePanelMetrics.HierarchyMinWidth, WorkspacePanel.DefaultMinHeight),
            (PanelIds.Node, WorkspacePanelMetrics.CenterMinWidth, WorkspacePanelMetrics.CenterMinHeight),
            (PanelIds.Edit, WorkspacePanelMetrics.CenterMinWidth, WorkspacePanelMetrics.CenterMinHeight),
            (PanelIds.Game, WorkspacePanelMetrics.CenterMinWidth, WorkspacePanelMetrics.CenterMinHeight),
            (PanelIds.Bottom, WorkspacePanel.DefaultMinWidth, WorkspacePanelMetrics.BottomMinHeight),
            (PanelIds.Inspector, WorkspacePanelMetrics.InspectorMinWidth, WorkspacePanel.DefaultMinHeight),
        })
        {
            host.Panels.Add(new WorkspacePanel { PanelId = id, PanelMinWidth = minW, PanelMinHeight = minH, Content = new Avalonia.Controls.Border() });
        }
        var window = new Avalonia.Controls.Window { Width = 1440, Height = 820, Content = host };
        window.Show();
        try
        {
            Rect Measure(string id)
            {
                // Faz 5 D6 sözleşmesi: düzen geçişi CompleteMotion'a (ya da bir render tick'ine)
                // kadar 0'dan başlar; son yerleşimi ölçmek için geçişi bitir.
                window.UpdateLayout();
                host.CompleteMotion();
                window.UpdateLayout();
                return host.GetPanelRect(id);
            }
            void Near(string what, double actual, double expected)
            {
                if (Math.Abs(actual - expected) > expected * 0.10)
                    throw new Exception($"{what}: {actual:0.#} px, expected {expected} ±10%");
            }

            Near("Default Hierarchy width", Measure(PanelIds.Hierarchy).Width, 200);
            Near("Default Inspector width", Measure(PanelIds.Inspector).Width, 280);
            Near("Default bottom strip height", Measure(PanelIds.Bottom).Height, 180);
            double nodeWidth = Measure(PanelIds.Node).Width;

            mainVm.ShowPanel("Hierarchy");
            if (Measure(PanelIds.Hierarchy).Width != 0 || host.FindPanel(PanelIds.Hierarchy)!.IsVisible)
                throw new Exception("Hidden Hierarchy still reserved workspace width");
            if (Measure(PanelIds.Node).Width <= nodeWidth + 150)
                throw new Exception("Center did not take the hidden Hierarchy's width");
            mainVm.ShowPanel("Hierarchy");
            Near("Restored Hierarchy width", Measure(PanelIds.Hierarchy).Width, 200);
            mainVm.ShowPanel("Inspector");
            if (Measure(PanelIds.Inspector).Width != 0 || host.FindPanel(PanelIds.Inspector)!.IsVisible)
                throw new Exception("Hidden Inspector still reserved workspace width");
            mainVm.ShowPanel("Inspector");
            Near("Restored Inspector width", Measure(PanelIds.Inspector).Width, 280);

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
            Near("Shared strip height after Assets closed", Measure(PanelIds.Bottom).Height, 180);
            mainVm.ShowPanel("Log");
            if (mainVm.IsLogPanelVisible || mainVm.IsBottomPanelVisible)
                throw new Exception("Bottom workspace remained visible after both tabs were closed");
            if (Measure(PanelIds.Bottom).Height != 0 || Measure(PanelIds.Node).Height < 800)
                throw new Exception("Hidden bottom workspace still reserved height");
            mainVm.ShowPanel("Assets");
            if (!mainVm.IsAssetsPanelVisible || !mainVm.IsBottomPanelVisible || mainVm.BottomPanelActiveTab != 0)
                throw new Exception("Assets tab did not restore the shared strip");
            Near("Restored strip height", Measure(PanelIds.Bottom).Height, 180);
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
        }
        finally
        {
            window.Close();
        }
    }
}
