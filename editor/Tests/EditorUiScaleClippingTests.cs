using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 8 D3 (yol haritası B06): hedef ölçeklerde panel başlıkları kesilmez. %130 ölçekte
/// 1440×900 pencerede (≈1108×692 mantıksal) her görünür panel başlığı doğal genişliğine
/// sığar. Bozuk sürüm: Denetçi başlığında kimlik metni önce ölçülünce "DENETÇİ" → "D…".
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorUiScaleClippingTests : IDisposable
{
    private readonly string _previousRoot;
    private readonly string _root;
    private readonly MainWindowViewModel _vm;

    public EditorUiScaleClippingTests()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        _previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlUiScale_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        var created = ProjectFactory.CreateNewProject("UiScaleTest", parent);
        if (!created.Success || created.Info == null)
            throw new Exception("ProjectFactory.CreateNewProject failed: " + created.Error);
        _root = created.Info.Path;
        _vm = Dispatcher.UIThread.Invoke(() => new MainWindowViewModel(_root, connectEngine: false));
    }

    public void Dispose()
    {
        EditorUiScale.Apply(1.0);
        try { Dispatcher.UIThread.Invoke(() => _vm.Dispose()); } catch (Exception) { }
        try { Directory.Delete(Path.GetDirectoryName(_root)!, recursive: true); } catch (Exception) { }
        try
        {
            if (string.Equals(MainWindowViewModel.ProjectRoot, _root, StringComparison.Ordinal))
                MainWindowViewModel.ProjectRoot = _previousRoot;
        }
        catch (Exception) { }
    }

    [Theory]
    [InlineData(1.0)]
    [InlineData(1.15)]
    [InlineData(1.3)]
    public void PanelTitles_FitAtTargetScales(double scale)
    {
        Dispatcher.UIThread.Invoke(() =>
        {
            EditorUiScale.Apply(scale);
            var window = new MainWindow(_vm) { Width = 1440, Height = 900 };
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                window.UpdateLayout();
                var titles = window.GetVisualDescendants().OfType<TextBlock>()
                    .Where(t => t.Classes.Contains("panelTitle") && t.IsEffectivelyVisible && !string.IsNullOrEmpty(t.Text))
                    .ToList();
                Assert.NotEmpty(titles);
                foreach (var title in titles)
                {
                    var probe = new TextBlock
                    {
                        Text = title.Text, FontSize = title.FontSize, FontWeight = title.FontWeight,
                        FontFamily = title.FontFamily, LetterSpacing = title.LetterSpacing,
                    };
                    probe.Measure(Size.Infinity);
                    Assert.True(title.Bounds.Width + 0.5 >= probe.DesiredSize.Width,
                        $"'{title.Text}' %{scale * 100:0} ölçekte kesiliyor: {title.Bounds.Width:0.#} < {probe.DesiredSize.Width:0.#}");
                }
            }
            finally
            {
                window.Close();
                EditorUiScale.Apply(1.0);
            }
        });
    }
}
