using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
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
            if (Avalonia.Application.Current != null && !Dispatcher.UIThread.CheckAccess())
            {
                Dispatcher.UIThread.Invoke(() => Run(mainVm));
                return;
            }

            Console.WriteLine("\n[Test 35]: Toolbar layout lock (Faz 3 capsule, … menu, node toolbar, shortcuts)...");
            // NOT: pencere headless'ta Show() edilmez — boş engine
            // bitmap'li Image kontrolleri layout ölçümünde patlar. İçerik
            // bağlamaları DataContext atanır atanmaz çözüldüğü için
            // FindControl + Content okuma Show'suz çalışır.
            var window = new MainWindow(mainVm);
            try
            {
                // EDITOR_UI_PORT_PLAN Faz 3 (K4): tek satır üst çubuk, ortada
                // kapsül = görünümler | taşıma | Araçlar · Diğer (…). Eski iki
                // sıralı sol/sağ düğme grupları bilerek kaldırıldı.
                var capsule = window.FindControl<Border>("TopCapsule")
                    ?? throw new Exception("TopCapsule not found");
                if (Grid.GetColumn(capsule) != 1 || capsule.Parent is not Grid topGrid
                    || topGrid.ColumnDefinitions.Count != 3)
                    throw new Exception("TopCapsule must sit in the centre column of the top bar");
                foreach (string gone in new[] { "ToolbarLeftGroups", "ToolbarRightGroups", "LayoutPresetCombo" })
                    if (window.FindControl<Control>(gone) is not null)
                        throw new Exception($"Removed two-row toolbar element '{gone}' is back");

                // Taşıma kümesi (Oynat/Duraklat/Adım), komutlara bağlı,
                // oynamıyorken Duraklat/Adım kapalı.
                var cluster = window.FindControl<StackPanel>("TransportCluster")
                    ?? throw new Exception("TransportCluster not found");
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

                // Görünüm sekmeleri: Node · Oyun · Sahne Düzenle (K4). Komut
                // parametreleri EN kalır, vurgu aktif görünümü izler.
                var tabs = window.FindControl<StackPanel>("CenterViewTabs")
                    ?? throw new Exception("CenterViewTabs not found");
                string?[] tabParams = tabs.Children.OfType<Button>()
                    .Select(b => b.CommandParameter as string).ToArray();
                string?[] expectedParams = new[] { "NodeGraph", "EnginePreview", "Preview" };
                if (!tabParams.SequenceEqual(expectedParams))
                    throw new Exception(
                        $"CenterViewTabs params changed: [{string.Join(", ", tabParams)}]");
                mainVm.ShowPanelCommand.Execute("EnginePreview");
                if (!mainVm.IsEnginePreviewActive || mainVm.GameTabBrush == "Transparent")
                    throw new Exception("Game tab highlight did not follow EnginePreview");
                mainVm.ShowPanelCommand.Execute("NodeGraph");
                if (!mainVm.IsNodeGraphActive || mainVm.SceneTabBrush == "Transparent")
                    throw new Exception("Scene tab highlight did not follow NodeGraph");

                // "…" menüsü: proje, kayıt, dışa aktarma, düzen, bölünmüş ekran, ayarlar.
                var more = window.FindControl<MenuItem>("MoreMenu")
                    ?? throw new Exception("MoreMenu not found");
                var moreCommands = more.Items.OfType<MenuItem>().Select(m => m.Command).ToList();
                foreach (var (command, name) in new (object?, string)[]
                {
                    (mainVm.SaveProjectCommand, "Kaydet"), (mainVm.UndoCommand, "Geri Al"),
                    (mainVm.RedoCommand, "Yinele"), (mainVm.BuildGameCommand, "Dışa Aktar"),
                    (mainVm.CancelBuildCommand, "İptal"), (mainVm.OpenSettingsCommand, "Ayarlar"),
                })
                {
                    if (!moreCommands.Contains(command))
                        throw new Exception($"'{name}' is missing from the … menu");
                }

                // Node araç çubuğu kanvasta (K4): 5 düğüm komutu, sırayla.
                var nodeBar = window.FindControl<StackPanel>("NodeToolbar")
                    ?? throw new Exception("NodeToolbar not found");
                var nodeCommands = nodeBar.Children.OfType<Button>().Select(b => b.Command).ToList();
                var expectedNode = new object?[]
                {
                    mainVm.AddNodeCommand, mainVm.DisconnectSelectedNodeCablesCommand,
                    mainVm.DeleteSelectedNodeCommand, mainVm.CreateGroupFromSelectionCommand,
                    mainVm.ExitSubgraphCommand,
                };
                if (!nodeCommands.SequenceEqual(expectedNode))
                    throw new Exception("Node toolbar commands/order changed");

                // Kaydedilmemiş değişiklik noktası projenin kirli durumunu izler.
                var dot = window.FindControl<Avalonia.Controls.Shapes.Ellipse>("UnsavedDot")
                    ?? throw new Exception("UnsavedDot not found");
                bool dirtyBefore = mainVm.IsProjectDirty;
                mainVm.IsProjectDirty = true;
                bool shownWhenDirty = dot.IsVisible;
                mainVm.IsProjectDirty = false;
                bool hiddenWhenClean = !dot.IsVisible;
                mainVm.IsProjectDirty = dirtyBefore;
                if (!shownWhenDirty || !hiddenWhenClean)
                    throw new Exception("UnsavedDot does not follow IsProjectDirty");

                var windowsMenu = window.FindControl<MenuItem>("WindowsMenu")
                    ?? throw new Exception("WindowsMenu not found");
                if (windowsMenu.Items.Count != 9)
                    throw new Exception(
                        $"Pencereler menu should have 9 items (Faz 7: + Library, Lua), has {windowsMenu.Items.Count}");

                // Faz 5: panel kısayolları menüde yazar (InputGesture) ve
                // gerçekten yönlendirilir (TryPanelShortcut + KeyDown).
                // KeyGesture.ToString() sürüm-formatına kilitlenmemek için
                // yalnızca rozet varlığı denetlenir; eşleşme TryPanelShortcut
                // tablosuyla aşağıda kilitlenir.
                var menuItems = windowsMenu.Items.OfType<MenuItem>().ToArray();
                if (menuItems.Length != 9
                    || menuItems.Any(m => m.InputGesture is null))
                    throw new Exception(
                        $"Panel shortcut gestures changed: [{string.Join(", ", menuItems.Select(m => m.InputGesture?.ToString() ?? "-"))}]");
                string?[] expectedPanels = new[]
                {
                    "Hierarchy", "Inspector", "Log", "Assets",
                    "Backlog", "SaveSlots", "ProjectIssues", "Library", "Lua"
                };
                for (int i = 0; i < 9; i++)
                {
                    var key = (Avalonia.Input.Key)((int)Avalonia.Input.Key.D1 + i);
                    if (!MainWindow.TryPanelShortcut(key, out string? panel) || panel != expectedPanels[i])
                        throw new Exception($"TryPanelShortcut({key}) did not map to {expectedPanels[i]}");
                }
                if (MainWindow.TryPanelShortcut(Avalonia.Input.Key.D0, out _))
                    throw new Exception("TryPanelShortcut must reject keys outside Ctrl+1..9");

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
