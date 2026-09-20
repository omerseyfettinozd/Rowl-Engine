using System.Windows.Input;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Data.Converters;
using Avalonia.Layout;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 4 — project-relative asset path editor: text field plus
    /// an optional Browse button (bound to the existing MainVM picker
    /// commands). Existence/format problems surface through a
    /// <see cref="FieldValidationBadge"/> placed under the control, bound to
    /// the same field key — this control stays a dumb path editor.
    /// </summary>
    public class AssetPickerControl : UserControl
    {
        public static readonly StyledProperty<string> AssetPathProperty =
            AvaloniaProperty.Register<AssetPickerControl, string>(
                nameof(AssetPath), defaultBindingMode: BindingMode.TwoWay);

        public static readonly StyledProperty<string?> WatermarkProperty =
            AvaloniaProperty.Register<AssetPickerControl, string?>(nameof(Watermark));

        public static readonly StyledProperty<ICommand?> BrowseCommandProperty =
            AvaloniaProperty.Register<AssetPickerControl, ICommand?>(nameof(BrowseCommand));

        public static readonly StyledProperty<object?> BrowseParameterProperty =
            AvaloniaProperty.Register<AssetPickerControl, object?>(nameof(BrowseParameter));

        public string AssetPath
        {
            get => GetValue(AssetPathProperty);
            set => SetValue(AssetPathProperty, value);
        }

        public string? Watermark
        {
            get => GetValue(WatermarkProperty);
            set => SetValue(WatermarkProperty, value);
        }

        public ICommand? BrowseCommand
        {
            get => GetValue(BrowseCommandProperty);
            set => SetValue(BrowseCommandProperty, value);
        }

        public object? BrowseParameter
        {
            get => GetValue(BrowseParameterProperty);
            set => SetValue(BrowseParameterProperty, value);
        }

        public AssetPickerControl()
        {
            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("*, Auto"),
            };
            var box = new TextBox { Margin = new Thickness(0, 0, 4, 0) };
            box.Bind(TextBox.TextProperty, new Binding(nameof(AssetPath)) { Source = this, Mode = BindingMode.TwoWay });
            box.Bind(TextBox.WatermarkProperty, new Binding(nameof(Watermark)) { Source = this });
            var browse = new Button { Content = "Gözat", Padding = new Thickness(8, 4), FontSize = 11 };
            browse.Bind(Button.CommandProperty, new Binding(nameof(BrowseCommand)) { Source = this });
            browse.Bind(Button.CommandParameterProperty, new Binding(nameof(BrowseParameter)) { Source = this });
            browse.Bind(IsVisibleProperty, new Binding(nameof(BrowseCommand))
            {
                Source = this,
                Converter = new FuncValueConverter<ICommand?, bool>(command => command is not null),
            });
            grid.Children.Add(box);
            Grid.SetColumn(box, 0);
            grid.Children.Add(browse);
            Grid.SetColumn(browse, 1);
            Content = grid;
        }
    }
}
