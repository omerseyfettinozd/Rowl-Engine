using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor
{
    /// <summary>
    /// Faz 5 Dilim 3 kilidi: breadcrumb + status birleşimi. Zoom ve
    /// build-hedefi yalnızca status barda; breadcrumb yalnızca crumb
    /// listesi + Bölüm filtresi (Grup/Üst toolbar'a taşındı, geri
    /// dönmemeli); canvas overlay'de zoom rozeti yok; yüzen gölgeler sönük.
    /// </summary>
    internal static class EditorBreadcrumbStatusTests
    {
        public static void Run(MainWindowViewModel mainVm)
        {
            Console.WriteLine("\n[Test 37]: Breadcrumb/status merge lock (single zoom, slim breadcrumb, soft shadows)...");
            var window = new MainWindow(mainVm);
            try
            {
                // Breadcrumb: 1 crumb listesi + 1 Bölüm filtresi, Grup/Üst yok.
                var crumb = window.FindControl<Border>("BreadcrumbPanel")
                    ?? throw new Exception("BreadcrumbPanel not found");
                var crumbDesc = crumb.GetLogicalDescendants().ToArray();
                // ComboBox da ItemsControl türevidir; crumb listesi ComboBox dışı tekil olmalı.
                if (crumbDesc.OfType<ItemsControl>().Count(i => i is not ComboBox) != 1)
                    throw new Exception("Breadcrumb must host exactly the crumb list.");
                var chapter = crumbDesc.OfType<ComboBox>().SingleOrDefault()
                    ?? throw new Exception("Chapter filter ComboBox lost from breadcrumb.");
                if (chapter.ItemsSource is null)
                    throw new Exception("Chapter filter options not bound.");
                if (!crumbDesc.OfType<TextBlock>().Any(t => t.Text == "Bölüm:"))
                    throw new Exception("Bölüm label lost from breadcrumb.");
                string[] crumbButtons = crumbDesc.OfType<Button>()
                    .Select(b => b.Content as string ?? string.Empty).ToArray();
                if (crumbButtons.Any(c => c.Contains("Grup") || c == "Üst"))
                    throw new Exception(
                        $"Grup/Üst moved to toolbar, must not return to breadcrumb: [{string.Join(", ", crumbButtons)}]");
                if (crumb.BoxShadow.Count != 1 || crumb.BoxShadow[0].Color.A > 0x40)
                    throw new Exception("Breadcrumb shadow must stay soft (alpha <= 0x40).");

                // Canvas overlay: zoom rozeti yok, Reset butonu yaşar.
                // (NodeGraphView ayrı XAML namescope'udur; önce tip bulunur.)
                var graphView = window.GetLogicalDescendants()
                    .OfType<RowlEngine.Editor.Views.Panels.NodeGraphView>().FirstOrDefault()
                    ?? throw new Exception("NodeGraphView not found");
                var overlay = graphView.FindControl<Border>("CanvasViewportOverlay")
                    ?? throw new Exception("CanvasViewportOverlay not found");
                if (overlay.GetLogicalDescendants().OfType<TextBlock>().Any())
                    throw new Exception("Zoom badge is back on the canvas overlay (zoom lives in the status bar).");
                var reset = overlay.GetLogicalDescendants().OfType<Button>().SingleOrDefault()
                    ?? throw new Exception("Reset View button lost from canvas overlay.");

                // Status bar: zoom + build-hedefi + mesaj, canlı değerlerle.
                _ = window.FindControl<Border>("StatusBarPanel")
                    ?? throw new Exception("StatusBarPanel not found");
                var zoom = window.FindControl<TextBlock>("StatusZoomText")
                    ?? throw new Exception("StatusZoomText not found");
                if (zoom.Text != mainVm.ZoomScale.ToString("P0"))
                    throw new Exception($"Status zoom mismatch: '{zoom.Text}'.");
                var target = window.FindControl<TextBlock>("StatusBuildTargetText")
                    ?? throw new Exception("StatusBuildTargetText not found");
                if (target.Text != mainVm.CurrentBuildTarget)
                    throw new Exception($"Status build target mismatch: '{target.Text}'.");
                var msg = window.FindControl<TextBlock>("StatusMessageText")
                    ?? throw new Exception("StatusMessageText not found");
                if (msg.Text != mainVm.StatusText)
                    throw new Exception("Status message mismatch.");

                // Yüzen gölgeler sönük: arama + toast.
                var search = window.FindControl<Border>("SearchOverlayPanel")
                    ?? throw new Exception("SearchOverlayPanel not found");
                if (search.BoxShadow.Count != 1 || search.BoxShadow[0].Color.A > 0x40)
                    throw new Exception("Search overlay shadow must stay soft (alpha <= 0x40).");
                var toast = window.FindControl<Border>("ToastPanel")
                    ?? throw new Exception("ToastPanel not found");
                if (toast.BoxShadow.Count != 1 || toast.BoxShadow[0].Color.A > 0x50)
                    throw new Exception("Toast shadow must stay soft (alpha <= 0x50).");
            }
            finally
            {
                window.Close();
            }
            Console.WriteLine("  [PASS] Single zoom in status bar, slim breadcrumb, soft shadows verified");
        }
    }
}
