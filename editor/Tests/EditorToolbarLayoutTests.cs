using System;
using System.Linq;
using Avalonia.Controls;
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
                if (rightButtons.Count != 4)
                    throw new Exception(
                        $"Right toolbar should have 4 buttons, has {rightButtons.Count}");
                if ((rightButtons[^1].Content as string) != "Ayarlar")
                    throw new Exception("Ayarlar must be the last (standalone) right toolbar button");

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
