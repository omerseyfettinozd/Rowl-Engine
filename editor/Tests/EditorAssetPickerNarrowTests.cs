using System.Linq;
using System.Windows.Input;
using Avalonia.Controls;
using Avalonia.LogicalTree;
using Avalonia.Threading;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Controls;
using RowlEngine.Editor.Tools.RowlEye;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 10 artığı kilidi: dar Denetçi'de varlık yolu kutusu sütundan taşmaz (Gözat düğmesi
/// görünür kalır) ve kesilen uzun yolun tamamı ipucunda okunur.
/// </summary>
public sealed class EditorAssetPickerNarrowTests
{
    [Theory]
    [InlineData(160)]
    [InlineData(200)]
    [InlineData(240)]
    public void NarrowPicker_KeepsBrowseVisible_AndShowsFullPathInTooltip(double width)
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            const string longPath = "images/backgrounds/chapter_03/ışıklı_röle_gece_versiyonu_final.png";
            ICommand browse = new RelayCommand(() => { });
            var picker = new AssetPickerControl { AssetPath = longPath, BrowseCommand = browse };
            var window = new Window { Width = width, Height = 120, Content = picker };
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                var box = picker.GetLogicalDescendants().OfType<TextBox>().Single();
                var button = picker.GetLogicalDescendants().OfType<Button>().Single();

                Assert.True(button.IsVisible, "Gözat düğmesi görünmüyor");
                double right = button.Bounds.Right;
                Assert.True(right <= picker.Bounds.Width + 0.5,
                    $"{width}px: seçici taşıyor (Gözat sağ kenarı {right:F1} > {picker.Bounds.Width:F1})");
                Assert.True(box.Bounds.Right <= button.Bounds.Left + 0.5,
                    $"{width}px: yol kutusu Gözat'ın altına giriyor");
                Assert.Equal(longPath, ToolTip.GetTip(box) as string);
            }
            finally
            {
                window.Close();
            }
        });
    }
}
