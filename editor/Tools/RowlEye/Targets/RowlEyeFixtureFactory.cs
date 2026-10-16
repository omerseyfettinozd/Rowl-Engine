using System;
using System.IO;
using Avalonia;
using Avalonia.Controls;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Dialogs;
using RowlEngine.Editor.Views.Panels;

namespace RowlEngine.Editor.Tools.RowlEye.Targets;

/// <summary>
/// Factory producing deterministic view, viewmodel, and layout fixtures for any RowlEyeTarget,
/// including State Variants (e.g. Empty, Populated, Selected, SearchActive).
/// </summary>
public static class RowlEyeFixtureFactory
{
    public static (Window Window, object? ViewModel, Size Size) Create(
        RowlEyeTarget target,
        string theme = "Dark",
        string variant = "Default")
    {
        return target switch
        {
            RowlEyeTarget.SettingsDialog => CreateSettingsDialog(theme, variant),
            RowlEyeTarget.MainWindow => CreateMainWindow(theme, variant),
            RowlEyeTarget.NodeGraph => CreateIsolatedNodeGraph(theme, variant),
            RowlEyeTarget.Hierarchy => CreateIsolatedHierarchy(theme, variant),
            RowlEyeTarget.Inspector => CreateIsolatedInspector(theme, variant),
            RowlEyeTarget.AssetBrowser => CreateIsolatedAssetBrowser(theme, variant),
            RowlEyeTarget.LivePreview => CreateIsolatedLivePreview(theme, variant),
            _ => throw new ArgumentOutOfRangeException(nameof(target), target, null)
        };
    }

    private static (Window, object?, Size) CreateSettingsDialog(string theme, string variant)
    {
        var win = new SettingsDialog();
        ApplyTheme(win, theme);
        return (win, win.DataContext, new Size(860, 620));
    }

    private static (Window, object?, Size) CreateMainWindow(string theme, string variant)
    {
        var vm = CreateSealedMainViewModel();
        var win = new MainWindow(vm) { Width = 1920, Height = 1080 };
        ApplyTheme(win, theme);
        // Faz 8: elle görsel denetim için panel aç (ör. ROWL_EYE_PANELS=Store,Log); golden koşusunda boş kalır.
        string? panels = Environment.GetEnvironmentVariable("ROWL_EYE_PANELS");
        if (!string.IsNullOrWhiteSpace(panels))
        {
            foreach (string panel in panels.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                vm.ShowPanel(panel);
        }
        // Faz 8: işlem kartı / arayüz ölçeği elle denetimi (ROWL_EYE_OPERATION_FAIL=metin, ROWL_EYE_UI_SCALE=1.3).
        string? failure = Environment.GetEnvironmentVariable("ROWL_EYE_OPERATION_FAIL");
        if (!string.IsNullOrWhiteSpace(failure))
            vm.OperationStatus.Fail(OperationKind.Export, failure);
        if (double.TryParse(Environment.GetEnvironmentVariable("ROWL_EYE_UI_SCALE"),
                System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out double uiScale))
            RowlEngine.Editor.Services.EditorUiScale.Apply(uiScale);
        return (win, vm, new Size(1920, 1080));
    }

    private static (Window, object?, Size) CreateIsolatedNodeGraph(string theme, string variant)
    {
        var vm = CreateSealedMainViewModel();

        if (string.Equals(variant, "Empty", StringComparison.OrdinalIgnoreCase))
        {
            vm.Nodes?.Clear();
        }
        else if (string.Equals(variant, "Selected", StringComparison.OrdinalIgnoreCase) && vm.Nodes?.Count > 0)
        {
            vm.SelectedNode = vm.Nodes[0];
        }

        var control = new NodeGraphView { DataContext = vm };
        var win = RowlEyeIsolationHost.WrapInWindow(control, new Size(1920, 1080), theme);
        return (win, vm, new Size(1920, 1080));
    }

    private static (Window, object?, Size) CreateIsolatedHierarchy(string theme, string variant)
    {
        var vm = CreateSealedMainViewModel();
        var control = new NodeHierarchyView { DataContext = vm };
        var win = RowlEyeIsolationHost.WrapInWindow(control, new Size(400, 1080), theme);
        return (win, vm.HierarchyViewModel, new Size(400, 1080));
    }

    private static (Window, object?, Size) CreateIsolatedInspector(string theme, string variant)
    {
        var vm = CreateSealedMainViewModel();
        if (string.Equals(variant, "Selected", StringComparison.OrdinalIgnoreCase) && vm.Nodes?.Count > 0)
        {
            vm.SelectedNode = vm.Nodes[0];
        }

        var control = new NodeInspectorView { DataContext = vm };
        var win = RowlEyeIsolationHost.WrapInWindow(control, new Size(420, 1080), theme);
        return (win, vm, new Size(420, 1080));
    }

    private static (Window, object?, Size) CreateIsolatedAssetBrowser(string theme, string variant)
    {
        var vm = CreateSealedMainViewModel();

        if (string.Equals(variant, "SearchActive", StringComparison.OrdinalIgnoreCase))
        {
            vm.AssetBrowserViewModel.SearchText = "Texture";
        }

        var control = new ProjectAssetsView { DataContext = vm.AssetBrowserViewModel };
        var win = RowlEyeIsolationHost.WrapInWindow(control, new Size(1280, 720), theme);
        return (win, vm.AssetBrowserViewModel, new Size(1280, 720));
    }

    private static (Window, object?, Size) CreateIsolatedLivePreview(string theme, string variant)
    {
        var vm = CreateSealedMainViewModel();
        var control = new LivePreviewControl { DataContext = vm };
        var win = RowlEyeIsolationHost.WrapInWindow(control, new Size(960, 540), theme);
        return (win, vm, new Size(960, 540));
    }

    private static MainWindowViewModel CreateSealedMainViewModel()
    {
        string sourceAssets = MainWindowViewModel.AssetsPath;
        string tempProject = Path.Combine(Path.GetTempPath(), $"RowlEye_Fixture_{Guid.NewGuid():N}");
        if (Directory.Exists(sourceAssets))
        {
            CopyDirectory(sourceAssets, Path.Combine(tempProject, "Assets"));
        }
        else
        {
            Directory.CreateDirectory(tempProject);
        }

        return new MainWindowViewModel(tempProject, connectEngine: false);
    }

    private static void ApplyTheme(Window win, string theme)
    {
        if (string.Equals(theme, "Light", StringComparison.OrdinalIgnoreCase))
            win.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Light;
        else
            win.RequestedThemeVariant = Avalonia.Styling.ThemeVariant.Dark;
    }

    private static void CopyDirectory(string sourceDir, string targetDir)
    {
        Directory.CreateDirectory(targetDir);
        foreach (string file in Directory.GetFiles(sourceDir))
            File.Copy(file, Path.Combine(targetDir, Path.GetFileName(file)), overwrite: true);
        foreach (string subDirectory in Directory.GetDirectories(sourceDir))
            CopyDirectory(subDirectory, Path.Combine(targetDir, Path.GetFileName(subDirectory)));
    }
}
