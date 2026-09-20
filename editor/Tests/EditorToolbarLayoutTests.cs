using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Layout;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor
{
    /// <summary>
    /// Faz 3 kilidi: toolbar buton sırası/sayımı, menü öğe sayıları ve
    /// taşınan komutların yaşaması. Düzen yalnızca XAML'dadır; bu test
    /// sessiz geri-dönüşleri (kaldırılan butonun dönmesi, menü şişmesi)
    /// yakalar.
    /// </summary>
    internal static class EditorToolbarLayoutTests
    {
        public static void Run(MainWindowViewModel mainVm)
        {
            Console.WriteLine("\n[Test 35]: Toolbar layout lock (button order, menus, relocated commands)...");
            // NOT: pencere headless'ta Show() edilmez — boş engine
            // bitmap'li Image kontrolleri layout ölçümünde patlar. İçerik
            // bağlamaları DataContext atanır atanmaz çözüldüğü için
            // FindControl + Content okuma Show'suz çalışır.
            var window = new MainWindow(mainVm);
            try
            {
                var left = window.FindControl<StackPanel>("ToolbarLeftGroups")
                    ?? throw new Exception("ToolbarLeftGroups not found");
                string[] leftContents = left.Children.OfType<Button>()
                    .Select(b => b.Content as string ?? string.Empty).ToArray();
                string[] expectedLeft = new[]
                {
                    "Kaydet", "Geri Al", "Yinele",
                    "+ Node", "Bağlantıyı Kes", "Sil",
                    "+ Grup", "Üst"
                };
                if (!leftContents.SequenceEqual(expectedLeft))
                    throw new Exception(
                        $"Left toolbar order changed: [{string.Join(", ", leftContents)}]");

                var right = window.FindControl<StackPanel>("ToolbarRightGroups")
                    ?? throw new Exception("ToolbarRightGroups not found");
                var rightButtons = right.Children.OfType<Button>().ToList();
                // Unity düzeni: Play ortadaki TransportCluster'a taşındı,
                // sağda Build/İptal/Ayarlar kaldı.
                if (rightButtons.Count != 3)
                    throw new Exception(
                        $"Right toolbar should have 3 buttons, has {rightButtons.Count}");
                if ((rightButtons[^1].Content as string) != "Ayarlar")
                    throw new Exception("Ayarlar must be the last (standalone) right toolbar button");

                // Unity düzeni Dilim A: ortalanmış taşıma kümesi
                // (Oynat/Duraklat/Adım), komutlara bağlı, oynamıyorken
                // Duraklat/Adım kapalı.
                var cluster = window.FindControl<StackPanel>("TransportCluster")
                    ?? throw new Exception("TransportCluster not found");
                if (cluster.HorizontalAlignment != HorizontalAlignment.Center)
                    throw new Exception("TransportCluster must be centered");
                var transportButtons = cluster.Children.OfType<Button>().ToList();
                if (transportButtons.Count != 3
                    || transportButtons.Any(b => b.Command is null))
                    throw new Exception("TransportCluster should have 3 wired buttons");
                string[] expectedTips = { "Oynat / Durdur", "Duraklat", "Kare Adım (1/60 sn)" };
                string?[] actualTips = transportButtons
                    .Select(b => ToolTip.GetTip(b) as string).ToArray();
                if (!actualTips.SequenceEqual(expectedTips))
                    throw new Exception(
                        $"Transport tooltips changed: [{string.Join(", ", actualTips)}]");
                if (transportButtons[1].IsEnabled || transportButtons[2].IsEnabled)
                    throw new Exception("Pause/Step must be disabled while not playing");

                // Unity düzeni Dilim B: Scene/Game sekme şeridi — komut
                // parametreleri EN kalır, vurgu aktif görünümü izler.
                var tabs = window.FindControl<StackPanel>("CenterViewTabs")
                    ?? throw new Exception("CenterViewTabs not found");
                var tabButtons = tabs.Children.OfType<Button>().ToList();
                if (tabButtons.Count != 4)
                    throw new Exception(
                        $"CenterViewTabs should have 4 buttons, has {tabButtons.Count}");
                string?[] tabParams = tabButtons
                    .Select(b => b.CommandParameter as string).ToArray();
                string?[] expectedParams = new[] { "NodeGraph", "Preview", "EnginePreview", "SplitScreen" };
                if (!tabParams.SequenceEqual(expectedParams))
                    throw new Exception(
                        $"CenterViewTabs params changed: [{string.Join(", ", tabParams)}]");
                mainVm.ShowPanelCommand.Execute("EnginePreview");
                if (!mainVm.IsEnginePreviewActive || mainVm.GameTabBrush == "Transparent")
                    throw new Exception("Game tab highlight did not follow EnginePreview");
                mainVm.ShowPanelCommand.Execute("NodeGraph");
                if (!mainVm.IsNodeGraphActive || mainVm.SceneTabBrush == "Transparent")
                    throw new Exception("Scene tab highlight did not follow NodeGraph");

                string[] allToolbar = leftContents
                    .Concat(rightButtons.Select(b => b.Content as string ?? string.Empty))
                    .ToArray();
                foreach (string gone in new[] { "Ara", "Oyuncu" })
                    if (allToolbar.Contains(gone))
                        throw new Exception($"Removed toolbar button '{gone}' is back");

                var windowsMenu = window.FindControl<MenuItem>("WindowsMenu")
                    ?? throw new Exception("WindowsMenu not found");
                if (windowsMenu.Items.Count != 7)
                    throw new Exception(
                        $"Pencereler menu should have 7 items, has {windowsMenu.Items.Count}");

                // Faz 5: panel kısayolları menüde yazar (InputGesture) ve
                // gerçekten yönlendirilir (TryPanelShortcut + KeyDown).
                // KeyGesture.ToString() sürüm-formatına kilitlenmemek için
                // yalnızca rozet varlığı denetlenir; eşleşme TryPanelShortcut
                // tablosuyla aşağıda kilitlenir.
                var menuItems = windowsMenu.Items.OfType<MenuItem>().ToArray();
                if (menuItems.Length != 7
                    || menuItems.Any(m => m.InputGesture is null))
                    throw new Exception(
                        $"Panel shortcut gestures changed: [{string.Join(", ", menuItems.Select(m => m.InputGesture?.ToString() ?? "-"))}]");
                string?[] expectedPanels = new[]
                {
                    "Hierarchy", "Inspector", "Log", "Assets",
                    "Backlog", "SaveSlots", "ProjectIssues"
                };
                for (int i = 0; i < 7; i++)
                {
                    var key = (Avalonia.Input.Key)((int)Avalonia.Input.Key.D1 + i);
                    if (!MainWindow.TryPanelShortcut(key, out string? panel) || panel != expectedPanels[i])
                        throw new Exception($"TryPanelShortcut({key}) did not map to {expectedPanels[i]}");
                }
                if (MainWindow.TryPanelShortcut(Avalonia.Input.Key.D8, out _))
                    throw new Exception("TryPanelShortcut must reject keys outside Ctrl+1..7");

                // Gerçek tuş yönlendirme: Ctrl+3 Log paneline ulaşır (durum geri alınır).
                bool logBefore = mainVm.IsLogPanelVisible;
                int tabBefore = mainVm.BottomPanelActiveTab;
                window.RaiseEvent(new Avalonia.Input.KeyEventArgs
                {
                    RoutedEvent = Avalonia.Input.InputElement.KeyDownEvent,
                    Key = Avalonia.Input.Key.D3,
                    KeyModifiers = Avalonia.Input.KeyModifiers.Control
                });
                if (mainVm.IsLogPanelVisible == logBefore
                    && mainVm.BottomPanelActiveTab == tabBefore)
                    throw new Exception("Ctrl+3 did not reach the Log panel.");
                mainVm.IsLogPanelVisible = logBefore;
                mainVm.BottomPanelActiveTab = tabBefore;

                var projectsMenu = window.FindControl<MenuItem>("ProjectsMenu")
                    ?? throw new Exception("ProjectsMenu not found");
                if (projectsMenu.Items.Count != 2)
                    throw new Exception(
                        $"Projeler menu should have 2 items, has {projectsMenu.Items.Count}");

                // Taşınan butonların arkasındaki komutlar yaşamalı (derleme
                // kilidi: yöntem silinirse bu satırlar derlenmez).
                if (mainVm.ToggleSearchCommand is null
                    || mainVm.OpenPlayerWindowCommand is null
                    || mainVm.AnalyzeStoryGraphCommand is null
                    || mainVm.OpenLocalizationDeskCommand is null
                    || mainVm.CreateGroupFromSelectionCommand is null
                    || mainVm.ExitSubgraphCommand is null)
                    throw new Exception("A relocated toolbar command is missing");
            }
            finally
            {
                window.Close();
            }
            Console.WriteLine("  [PASS] Toolbar order, menus and relocated commands verified");
        }
    }
}
