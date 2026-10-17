using System;
using System.IO;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Workspace;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

[Collection("StaticRootSequential")]
public sealed class EditorIssueAssetFocusTests
{
    // Geçici proje kur, VM üret (UI thread).
    private static MainWindowViewModel CreateVm(string name, string parent, string capturedRoot)
    {
        return Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(capturedRoot, connectEngine: false));
    }

    [Fact]
    public void FocusIssueAsset_Reveals_File_And_Activates_Assets_Tab()
    {
        // Geçici proje + VM kur.
        string parent = Path.Combine(Path.GetTempPath(), "rowl_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        string? prevRoot = Dispatcher.UIThread.Invoke(() => MainWindowViewModel.ProjectRoot);
        MainWindowViewModel? vm = null;
        string root = string.Empty;
        try
        {
            root = ProjectFactory.CreateNewProject("IssueAssetA", parent).Info!.Path;
            string captured = root;
            vm = CreateVm("IssueAssetA", parent, captured);
            Dispatcher.UIThread.Invoke(() =>
            {
                // Test varlığını yaz.
                string dir = Path.Combine(captured, "Assets", "images");
                Directory.CreateDirectory(dir);
                File.WriteAllBytes(Path.Combine(dir, "a.png"), new byte[] { 1, 2, 3, 4 });
                vm!.AssetBrowserViewModel.RefreshAssets();
                // Başlangıcı Sorunlar yap (geçiş kanıtı).
                vm.Workspace = vm.Workspace.WithBottomTab(BottomStripState.IssuesTab, true).WithActiveBottomTab(BottomStripState.IssuesTab);
            });
            bool ok = Dispatcher.UIThread.Invoke(() => vm!.FocusIssueAsset("images/a.png"));
            Assert.True(ok);
            int active = Dispatcher.UIThread.Invoke(() => vm!.Workspace.Bottom.ActiveTab);
            Assert.Equal(BottomStripState.AssetsTab, active);
            string? rel = Dispatcher.UIThread.Invoke(() => vm!.AssetBrowserViewModel.SelectedNode?.RelativePath);
            Assert.NotNull(rel);
            Assert.True(rel!.Replace('\\', '/').EndsWith("images/a.png", StringComparison.Ordinal));
        }
        finally
        {
            if (vm != null)
                Dispatcher.UIThread.Invoke(() => vm.Dispose());
            try { if (Directory.Exists(parent)) Directory.Delete(parent, true); } catch { }
            string? prev = prevRoot;
            Dispatcher.UIThread.Invoke(() => MainWindowViewModel.ProjectRoot = prev);
        }
    }

    [Fact]
    public void FocusIssueAsset_Backslash_Assets_Prefix_Works()
    {
        // Ters eğik + önek normalize olur.
        string parent = Path.Combine(Path.GetTempPath(), "rowl_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        string? prevRoot = Dispatcher.UIThread.Invoke(() => MainWindowViewModel.ProjectRoot);
        MainWindowViewModel? vm = null;
        string root = string.Empty;
        try
        {
            root = ProjectFactory.CreateNewProject("IssueAssetB", parent).Info!.Path;
            string captured = root;
            vm = CreateVm("IssueAssetB", parent, captured);
            Dispatcher.UIThread.Invoke(() =>
            {
                string dir = Path.Combine(captured, "Assets", "images");
                Directory.CreateDirectory(dir);
                File.WriteAllBytes(Path.Combine(dir, "a.png"), new byte[] { 5, 6, 7 });
                vm!.AssetBrowserViewModel.RefreshAssets();
            });
            bool ok = Dispatcher.UIThread.Invoke(() => vm!.FocusIssueAsset("Assets\\images\\a.png"));
            Assert.True(ok);
            string? rel = Dispatcher.UIThread.Invoke(() => vm!.AssetBrowserViewModel.SelectedNode?.RelativePath);
            Assert.NotNull(rel);
            Assert.True(rel!.Replace('\\', '/').EndsWith("images/a.png", StringComparison.Ordinal));
        }
        finally
        {
            if (vm != null)
                Dispatcher.UIThread.Invoke(() => vm.Dispose());
            try { if (Directory.Exists(parent)) Directory.Delete(parent, true); } catch { }
            string? prev = prevRoot;
            Dispatcher.UIThread.Invoke(() => MainWindowViewModel.ProjectRoot = prev);
        }
    }

    [Fact]
    public void FocusIssueAsset_Missing_Returns_False_And_Keeps_Tab()
    {
        // Olmayan yol: false, sekme sabit.
        string parent = Path.Combine(Path.GetTempPath(), "rowl_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        string? prevRoot = Dispatcher.UIThread.Invoke(() => MainWindowViewModel.ProjectRoot);
        MainWindowViewModel? vm = null;
        string root = string.Empty;
        try
        {
            root = ProjectFactory.CreateNewProject("IssueAssetC", parent).Info!.Path;
            string captured = root;
            vm = CreateVm("IssueAssetC", parent, captured);
            Dispatcher.UIThread.Invoke(() =>
            {
                string dir = Path.Combine(captured, "Assets", "images");
                Directory.CreateDirectory(dir);
                File.WriteAllBytes(Path.Combine(dir, "a.png"), new byte[] { 1 });
                vm!.AssetBrowserViewModel.RefreshAssets();
                vm.Workspace = vm.Workspace.WithBottomTab(BottomStripState.IssuesTab, true).WithActiveBottomTab(BottomStripState.IssuesTab);
            });
            int before = Dispatcher.UIThread.Invoke(() => vm!.Workspace.Bottom.ActiveTab);
            bool ok = Dispatcher.UIThread.Invoke(() => vm!.FocusIssueAsset("images/yok.png"));
            Assert.False(ok);
            int after = Dispatcher.UIThread.Invoke(() => vm!.Workspace.Bottom.ActiveTab);
            Assert.Equal(before, after);
        }
        finally
        {
            if (vm != null)
                Dispatcher.UIThread.Invoke(() => vm.Dispose());
            try { if (Directory.Exists(parent)) Directory.Delete(parent, true); } catch { }
            string? prev = prevRoot;
            Dispatcher.UIThread.Invoke(() => MainWindowViewModel.ProjectRoot = prev);
        }
    }

    [Fact]
    public void HasTarget_Covers_Four_Combinations()
    {
        // Dört kombinasyon + boşluk kenarı.
        var none = new ProjectValidationIssue(false, "m");
        Assert.False(none.HasNodeTarget);
        Assert.False(none.HasAssetTarget);
        Assert.False(none.HasTarget);

        var onlyNode = new ProjectValidationIssue(false, "m", 1UL, null);
        Assert.True(onlyNode.HasNodeTarget);
        Assert.False(onlyNode.HasAssetTarget);
        Assert.True(onlyNode.HasTarget);

        var onlyAsset = new ProjectValidationIssue(false, "m", null, "images/a.png");
        Assert.False(onlyAsset.HasNodeTarget);
        Assert.True(onlyAsset.HasAssetTarget);
        Assert.True(onlyAsset.HasTarget);

        var both = new ProjectValidationIssue(false, "m", 7UL, "images/a.png");
        Assert.True(both.HasNodeTarget);
        Assert.True(both.HasAssetTarget);
        Assert.True(both.HasTarget);

        // Boşluk hedef sayılmaz.
        Assert.False(new ProjectValidationIssue(false, "m", null, "  ").HasTarget);
    }
}
