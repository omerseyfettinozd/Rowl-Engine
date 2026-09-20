using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor
{
    /// <summary>
    /// Faz 5 Dilim 4 kilidi: boş-durum tasarımları (metin + eylem).
    /// Assets (dosya yok / filtre sonuçsuz), Issues (sorun yok), Backlog
    /// (kayıt yok). Headless: pencere açılmaz, paneller tip üzerinden
    /// bulunur (UserControl namescope'ları pencere FindControl'üne kapalıdır).
    /// </summary>
    internal static class EditorEmptyStateTests
    {
        public static void Run(MainWindowViewModel mainVm)
        {
            Console.WriteLine("\n[Test 38]: Empty-state lock (text + action for Assets, Issues, Backlog)...");
            var window = new MainWindow(mainVm);
            // Issues listesi test sonunda geri yüklenir.
            var issuesVm = mainVm.ProjectIssuesViewModel;
            var issuesBefore = issuesVm.Issues.ToArray();
            var browser = mainVm.AssetBrowserViewModel;
            try
            {
                // ── Assets: dosya varken paneller gizli ──
                var assetsView = window.GetLogicalDescendants()
                    .OfType<ProjectAssetsView>().FirstOrDefault()
                    ?? throw new Exception("ProjectAssetsView not found");
                browser.SearchText = string.Empty;
                browser.SelectedTypeFilter = "Tümü";
                browser.RefreshAssets();
                var emptyPanel = assetsView.FindControl<StackPanel>("AssetsEmptyPanel")
                    ?? throw new Exception("AssetsEmptyPanel not found");
                var noMatchPanel = assetsView.FindControl<StackPanel>("AssetsNoMatchPanel")
                    ?? throw new Exception("AssetsNoMatchPanel not found");
                if (emptyPanel.IsVisible)
                    throw new Exception("Assets empty panel visible while files exist.");
                if (noMatchPanel.IsVisible)
                    throw new Exception("Assets no-match panel visible without a filter.");

                // ── Assets: sonuçsuz filtre → temizleme paneli + eylem ──
                browser.SearchText = "hicbirseybulunamaz123";
                if (!browser.IsFilterNoMatch)
                    throw new Exception("IsFilterNoMatch not set on a no-match filter.");
                if (!noMatchPanel.IsVisible)
                    throw new Exception("Assets no-match panel did not appear.");
                if (emptyPanel.IsVisible)
                    throw new Exception("Tree-empty panel must not show on filter no-match.");
                var clearBtn = noMatchPanel.GetLogicalDescendants().OfType<Button>().SingleOrDefault()
                    ?? throw new Exception("Clear-filter action button lost.");
                if (clearBtn.Command is null)
                    throw new Exception("Clear-filter button has no command.");
                clearBtn.Command.Execute(null);
                if (browser.SearchText != string.Empty || browser.SelectedTypeFilter != "Tümü")
                    throw new Exception("ClearFilter did not reset search and type filter.");
                if (noMatchPanel.IsVisible || browser.IsFilterNoMatch)
                    throw new Exception("No-match state did not clear.");

                // ── Issues: boş ↔ dolu iki yön ──
                issuesVm.SetIssues(Array.Empty<ProjectValidationIssue>());
                if (!issuesVm.IsEmpty || issuesVm.HasIssues)
                    throw new Exception("Issues empty flags diverged.");
                var issuesView = window.GetLogicalDescendants()
                    .OfType<ProjectIssuesView>().FirstOrDefault()
                    ?? throw new Exception("ProjectIssuesView not found");
                var issuesPanel = issuesView.FindControl<StackPanel>("IssuesEmptyPanel")
                    ?? throw new Exception("IssuesEmptyPanel not found");
                // Headless ayrinti: UserControl uzerindeki acik
                // DataContext="{Binding X}" baglantisi headless'ta yeniden
                // cozulmez (uretimde calisir — Issues listesi canli ozellik).
                // Kilit, uretim baglantisinin sagladigi VM ile dogrulanir.
                if (issuesView.DataContext is null)
                    issuesView.DataContext = issuesVm;
                if (!issuesPanel.IsVisible)
                    throw new Exception("Issues empty panel did not appear on empty list.");
                var analyzeBtn = issuesPanel.GetLogicalDescendants().OfType<Button>().SingleOrDefault()
                    ?? throw new Exception("Re-analyze action button lost.");
                if (analyzeBtn.Command is null)
                    throw new Exception("Re-analyze button has no command.");
                issuesVm.SetIssues(new[] { new ProjectValidationIssue(false, "kilit-probe") });
                if (issuesVm.IsEmpty || !issuesVm.HasIssues)
                    throw new Exception("Issues flags did not flip on new issue.");
                if (issuesPanel.IsVisible)
                    throw new Exception("Issues empty panel did not hide on new issue.");

                // ── Backlog: kablo + eylem (history headless'ta salt-okunur;
                // panel görünürlüğü VM bayrağını aynen yansıtmalı) ──
                var backlogVm = mainVm.BacklogViewModel;
                if (backlogVm.IsEmpty == backlogVm.HasEntries)
                    throw new Exception("Backlog empty flags diverged.");
                var backlogView = window.GetLogicalDescendants()
                    .OfType<BacklogView>().FirstOrDefault()
                    ?? throw new Exception("BacklogView not found");
                if (backlogView.DataContext is null)
                    backlogView.DataContext = backlogVm;
                var backlogPanel = backlogView.FindControl<StackPanel>("BacklogEmptyPanel")
                    ?? throw new Exception("BacklogEmptyPanel not found");
                if (backlogPanel.IsVisible != backlogVm.IsEmpty)
                    throw new Exception("Backlog empty panel does not mirror IsEmpty.");
                var playerBtn = backlogPanel.GetLogicalDescendants().OfType<Button>().SingleOrDefault()
                    ?? throw new Exception("Open-player action button lost.");
                if (playerBtn.Command is null || !ReferenceEquals(playerBtn.Command, backlogVm.OpenPlayerCommand))
                    throw new Exception("Open-player button is not wired to OpenPlayerCommand.");
            }
            finally
            {
                issuesVm.SetIssues(issuesBefore);
                browser.SearchText = string.Empty;
                browser.SelectedTypeFilter = "Tümü";
                browser.RefreshAssets();
                window.Close();
            }
            Console.WriteLine("  [PASS] Empty states with actions verified for Assets, Issues, Backlog");
        }
    }
}
