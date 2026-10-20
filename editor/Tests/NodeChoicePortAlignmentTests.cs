using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Threading;
using Avalonia.VisualTree;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.Views;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Gerçek ekran bulgusu (2026-10-20): seçim çıkış noktaları X+323'te çiziliyor, tel ise
/// X+265'ten (ConnectionViewModel) çıkıyordu — tel etiketin içinden geçiyor, yakın
/// düğümlerde etiket sonraki düğümün giriş ucuna biniyordu. Nokta merkezi tel
/// başlangıcıyla aynı yerde olmalı.
/// </summary>
public sealed class NodeChoicePortAlignmentTests
{
    [Fact]
    public void ChoicePortDots_SitWhereWiresStart()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            var choice = node.CreateObject("Choice").AddComponent<ChoiceComponentViewModel>();
            choice.Options.Add(new ChoiceOptionViewModel { Text = "Answer the signal" });
            choice.Options.Add(new ChoiceOptionViewModel { Text = "Let it fade" });
            var control = new NodeControl { DataContext = node, HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Left, VerticalAlignment = Avalonia.Layout.VerticalAlignment.Top };
            var window = new Window { Width = 600, Height = 300, Content = control };
            window.Show();
            try
            {
                Dispatcher.UIThread.RunJobs();
                foreach (var option in node.ChoiceOptions)
                {
                    var port = control.GetVisualDescendants().OfType<Border>()
                        .Single(b => Equals(b.Tag, option.OptionId));
                    var dot = port.GetVisualDescendants().OfType<Ellipse>().Single();
                    var center = dot.TranslatePoint(new Point(dot.Bounds.Width / 2, dot.Bounds.Height / 2), control)
                        ?? throw new System.Exception("nokta konumu çözülemedi");
                    Assert.Equal(265, center.X, 1);
                    Assert.Equal(node.GetOutputPortY(option.OptionId), center.Y, 1);
                }
            }
            finally
            {
                window.Close();
            }
        });
    }
}
