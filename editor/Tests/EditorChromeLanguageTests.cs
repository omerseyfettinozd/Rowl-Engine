using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor
{
    /// <summary>
    /// Faz 5 Dilim 5 kilidi: üst krom Türkçe birliği (menü + sekme
    /// başlıkları ilk faz). KRİTİK: yalnızca görünen metin çevrilir —
    /// CommandParameter kimlikleri (ShowPanel yönlendirme) İngilizce kalır;
    /// aşırı-çeviri menüyü sessizce öldürür.
    /// </summary>
    internal static class EditorChromeLanguageTests
    {
        public static void Run(MainWindowViewModel mainVm)
        {
            Console.WriteLine("\n[Test 39]: Chrome language lock (TR headers, EN command identifiers)...");
            var window = new MainWindow(mainVm);
            try
            {
                string[] expectedHeaders = new[]
                {
                    "Hiyerarşi", "Denetçi", "Günlük", "Varlıklar",
                    "Diyalog Geçmişi", "Kayıt Slotları", "Sorunlar"
                };
                string[] expectedIds = new[]
                {
                    "Hierarchy", "Inspector", "Log", "Assets",
                    "Backlog", "SaveSlots", "ProjectIssues"
                };

                var windowsMenu = window.FindControl<MenuItem>("WindowsMenu")
                    ?? throw new Exception("WindowsMenu not found");
                var items = windowsMenu.Items.OfType<MenuItem>().ToArray();
                string[] headers = items.Select(m => m.Header as string ?? string.Empty).ToArray();
                if (!headers.SequenceEqual(expectedHeaders))
                    throw new Exception($"Menu headers diverged: [{string.Join(", ", headers)}]");
                // Aşırı-çeviri koruması: kimlikler İngilizce kalmalı.
                string[] ids = items.Select(m => m.CommandParameter as string ?? string.Empty).ToArray();
                if (!ids.SequenceEqual(expectedIds))
                    throw new Exception($"Menu command identifiers must stay English: [{string.Join(", ", ids)}]");

                // Sekme başlıkları menüyle aynı sözlük.
                string[] tabs = window.GetLogicalDescendants().OfType<TabItem>()
                    .Select(t => t.Header as string ?? string.Empty)
                    .Where(h => expectedHeaders.Contains(h) || h == "Log" || h == "Assets"
                        || h == "Backlog" || h == "Save Slots" || h == "Issues")
                    .ToArray();
                string[] expectedTabs = new[]
                {
                    "Günlük", "Varlıklar", "Diyalog Geçmişi", "Kayıt Slotları", "Sorunlar"
                };
                if (!tabs.SequenceEqual(expectedTabs))
                    throw new Exception($"Tab headers diverged: [{string.Join(", ", tabs)}]");

                // Bölünmüş-ekran düğmesi + Reset eylemi Türkçe.
                string split = mainVm.SplitScreenButtonText;
                if (split != "Bölünmüş Ekran"
                    && !(split.StartsWith("Bölünmüş: ", StringComparison.Ordinal)
                         && (split.EndsWith("H", StringComparison.Ordinal) || split.EndsWith("V", StringComparison.Ordinal))))
                    throw new Exception($"Split button not Turkish: '{split}'.");
                var graphView = window.GetLogicalDescendants()
                    .OfType<NodeGraphView>().FirstOrDefault()
                    ?? throw new Exception("NodeGraphView not found");
                var reset = graphView.FindControl<Button>("ResetViewButton")
                    ?? throw new Exception("ResetViewButton not found");
                if ((reset.Content as string) != "Görünümü Sıfırla (0,0)")
                    throw new Exception($"Reset button diverged: '{reset.Content}'.");

                // Denetçi panel başlığı.
                var inspector = window.GetLogicalDescendants()
                    .OfType<NodeInspectorView>().FirstOrDefault()
                    ?? throw new Exception("NodeInspectorView not found");
                var titles = inspector.GetLogicalDescendants().OfType<TextBlock>()
                    .Select(t => t.Text ?? string.Empty).ToArray();
                if (!titles.Contains("DENETÇİ") || titles.Any(t => t == "INSPECTOR"))
                    throw new Exception("Inspector panel title diverged.");
            }
            finally
            {
                window.Close();
            }
            Console.WriteLine("  [PASS] Turkish top chrome with English command identifiers verified");
        }
    }
}
