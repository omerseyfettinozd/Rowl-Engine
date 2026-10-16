using System;
using System.Globalization;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Markup.Xaml.MarkupExtensions;
using Avalonia.Media;
using RowlEngine.Editor.Services.Search;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 4 + U1 — color editor with two modes. <c>Palette</c> assigns a
    /// node color tag (16 px preset swatches in a wrapping row plus one circular
    /// slash clear button at the end of that row; the empty state is shown by
    /// that button's selected ring instead of a text label); <c>Hex</c> edits a
    /// raw #RRGGBB string (preview chip + commit-on-valid, invalid input never
    /// propagates) with Unity-style R/G/B sliders writing the same hex value,
    /// so every color is reachable without typing. The top row wraps, so a
    /// narrow Inspector never clips the control.
    /// </summary>
    public enum ColorPickerMode
    {
        Palette,
        Hex,
    }

    public class ColorPickerControl : UserControl
    {
        public static readonly StyledProperty<string> SelectedValueProperty =
            AvaloniaProperty.Register<ColorPickerControl, string>(
                nameof(SelectedValue), defaultBindingMode: BindingMode.TwoWay);

        public static readonly StyledProperty<ColorPickerMode> ModeProperty =
            AvaloniaProperty.Register<ColorPickerControl, ColorPickerMode>(nameof(Mode));

        public string SelectedValue
        {
            get => GetValue(SelectedValueProperty);
            set => SetValue(SelectedValueProperty, value);
        }

        public ColorPickerMode Mode
        {
            get => GetValue(ModeProperty);
            set => SetValue(ModeProperty, value);
        }

        private readonly WrapPanel _swatches = new() { Orientation = Orientation.Horizontal };
        private readonly TextBlock _hexReadout = new() { FontSize = 10, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(4, 0, 0, 0) };
        private readonly TextBox _hexBox = new() { FontSize = 11, Width = 92, Watermark = "#RRGGBB", Margin = new Thickness(4, 0, 0, 0) };
        private readonly Border _preview = new() { Width = 22, Height = 22, CornerRadius = new CornerRadius(4), Margin = new Thickness(4, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        private readonly Button _clearButton = new()
        {
            Width = 16,
            Height = 16,
            MinWidth = 16,
            MinHeight = 16,
            Padding = new Thickness(0),
            Margin = new Thickness(0, 0, 0, 4),
            Background = Brushes.Transparent,
            BorderBrush = Brushes.Transparent,
            BorderThickness = new Thickness(0),
            CornerRadius = new CornerRadius(8),
            Cursor = new Cursor(StandardCursorType.Hand),
            VerticalAlignment = VerticalAlignment.Center,
            [ToolTip.TipProperty] = "Temizle",
        };
        private readonly Ellipse _clearRing = new() { Width = 16, Height = 16, StrokeThickness = 1 };
        private readonly Ellipse _clearSelectedRing = new() { Width = 16, Height = 16, StrokeThickness = 1.5 };
        private readonly Path _clearSlash = new()
        {
            Width = 16,
            Height = 16,
            Data = Geometry.Parse("M4,12 L12,4"),
            StrokeThickness = 1,
            Stretch = Stretch.None,
        };

        private readonly StackPanel _rgbPanel = new() { Spacing = 2, Orientation = Orientation.Vertical };
        private readonly Slider _rSlider = new() { Minimum = 0, Maximum = 255, SmallChange = 1, LargeChange = 16 };
        private readonly Slider _gSlider = new() { Minimum = 0, Maximum = 255, SmallChange = 1, LargeChange = 16 };
        private readonly Slider _bSlider = new() { Minimum = 0, Maximum = 255, SmallChange = 1, LargeChange = 16 };
        private readonly TextBlock _rValue = new() { FontSize = 10, VerticalAlignment = VerticalAlignment.Center };
        private readonly TextBlock _gValue = new() { FontSize = 10, VerticalAlignment = VerticalAlignment.Center };
        private readonly TextBlock _bValue = new() { FontSize = 10, VerticalAlignment = VerticalAlignment.Center };
        private bool _syncing;

        public ColorPickerControl()
        {
            var root = new StackPanel { Spacing = 4, Orientation = Orientation.Vertical };
            var top = new WrapPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            _clearRing.Bind(Shape.StrokeProperty, new DynamicResourceExtension("MutedText"));
            _clearSlash.Bind(Shape.StrokeProperty, new DynamicResourceExtension("MutedText"));
            _clearSelectedRing.Bind(Shape.StrokeProperty, new DynamicResourceExtension("PrimaryText"));
            _clearButton.Content = new Panel
            {
                Width = 16,
                Height = 16,
                Children = { _clearRing, _clearSlash, _clearSelectedRing },
            };
            _clearButton.Click += (_, _) => SelectedValue = string.Empty;
            BuildSwatches();
            top.Children.Add(_swatches);
            top.Children.Add(_preview);
            top.Children.Add(_hexBox);
            top.Children.Add(_hexReadout);
            _rgbPanel.Children.Add(BuildChannelRow("R", _rSlider, _rValue));
            _rgbPanel.Children.Add(BuildChannelRow("G", _gSlider, _gValue));
            _rgbPanel.Children.Add(BuildChannelRow("B", _bSlider, _bValue));
            _rSlider.ValueChanged += (_, _) => OnChannelChanged();
            _gSlider.ValueChanged += (_, _) => OnChannelChanged();
            _bSlider.ValueChanged += (_, _) => OnChannelChanged();
            root.Children.Add(top);
            root.Children.Add(_rgbPanel);
            Content = root;
            ApplyMode();
        }

        static ColorPickerControl()
        {
            ModeProperty.Changed.AddClassHandler<ColorPickerControl>((c, _) => c.ApplyMode());
            SelectedValueProperty.Changed.AddClassHandler<ColorPickerControl>((c, _) => c.Refresh());
        }

        private static Grid BuildChannelRow(string label, Slider slider, TextBlock value)
        {
            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("Auto, *, Auto"),
                ColumnSpacing = 6,
            };
            var name = new TextBlock
            {
                Text = label,
                FontSize = 10,
                VerticalAlignment = VerticalAlignment.Center,
            };
            value.MinWidth = 24;
            grid.Children.Add(name);
            Grid.SetColumn(name, 0);
            grid.Children.Add(slider);
            Grid.SetColumn(slider, 1);
            grid.Children.Add(value);
            Grid.SetColumn(value, 2);
            return grid;
        }

        private void BuildSwatches()
        {
            _swatches.Children.Clear();
            foreach (string name in NodeColorTags.PaletteNames)
            {
                var swatch = new Border
                {
                    Width = 16,
                    Height = 16,
                    CornerRadius = new CornerRadius(8),
                    Margin = new Thickness(0, 0, 4, 4),
                    Background = NodeColorTags.GetBrush(name),
                    BorderBrush = new SolidColorBrush(Colors.White),
                    BorderThickness = new Thickness(0),
                    Cursor = new Cursor(StandardCursorType.Hand),
                    Tag = name,
                    [ToolTip.TipProperty] = name,
                };
                swatch.PointerPressed += (_, e) =>
                {
                    if (e.GetCurrentPoint(swatch).Properties.IsLeftButtonPressed && swatch.Tag is string tag)
                        SelectedValue = Mode == ColorPickerMode.Palette ? tag : NodeColorTags.ToHex(tag);
                };
                _swatches.Children.Add(swatch);
            }
            _swatches.Children.Add(_clearButton);
        }

        private void ApplyMode()
        {
            bool hex = Mode == ColorPickerMode.Hex;
            _hexBox.IsVisible = hex;
            _hexReadout.IsVisible = !hex && !string.IsNullOrEmpty(SelectedValue);
            _preview.IsVisible = hex || !string.IsNullOrEmpty(SelectedValue);
            _rgbPanel.IsVisible = hex;
            Refresh();
        }

        private void OnChannelChanged()
        {
            if (_syncing || Mode != ColorPickerMode.Hex)
                return;
            int r = (int)Math.Round(_rSlider.Value);
            int g = (int)Math.Round(_gSlider.Value);
            int b = (int)Math.Round(_bSlider.Value);
            _rValue.Text = r.ToString(CultureInfo.InvariantCulture);
            _gValue.Text = g.ToString(CultureInfo.InvariantCulture);
            _bValue.Text = b.ToString(CultureInfo.InvariantCulture);
            string hex = $"#{r:X2}{g:X2}{b:X2}";
            if (!string.Equals(SelectedValue, hex, StringComparison.OrdinalIgnoreCase))
            {
                _syncing = true;
                try
                {
                    SelectedValue = hex;
                }
                finally
                {
                    _syncing = false;
                }
            }
            _preview.Background = new SolidColorBrush(Color.FromRgb((byte)r, (byte)g, (byte)b));
            if (!_hexBox.IsFocused && _hexBox.Text != hex)
                _hexBox.Text = hex;
        }

        private void Refresh()
        {
            string value = SelectedValue ?? string.Empty;
            _clearSelectedRing.IsVisible = string.IsNullOrEmpty(value);
            if (_syncing)
                return;
            foreach (var child in _swatches.Children.OfType<Border>())
            {
                bool active = Mode == ColorPickerMode.Palette
                    ? string.Equals(child.Tag as string, value, StringComparison.OrdinalIgnoreCase)
                    : string.Equals(NodeColorTags.ToHex(child.Tag as string), value, StringComparison.OrdinalIgnoreCase);
                child.BorderThickness = new Thickness(active ? 2 : 0);
            }
            if (Mode == ColorPickerMode.Palette)
            {
                _hexReadout.Text = string.IsNullOrEmpty(value) ? string.Empty : NodeColorTags.ToHex(value);
                _hexReadout.IsVisible = !string.IsNullOrEmpty(value);
                _preview.IsVisible = !string.IsNullOrEmpty(value);
                _preview.Background = string.IsNullOrEmpty(value)
                    ? Brushes.Transparent
                    : NodeColorTags.GetBrush(value);
            }
            else
            {
                _preview.IsVisible = true;
                if (TryParseHex(value, out Color color))
                {
                    _rValue.Text = color.R.ToString(CultureInfo.InvariantCulture);
                    _gValue.Text = color.G.ToString(CultureInfo.InvariantCulture);
                    _bValue.Text = color.B.ToString(CultureInfo.InvariantCulture);
                    if (!_hexBox.IsFocused && _hexBox.Text != value)
                        _hexBox.Text = value;
                    if (!_rSlider.IsFocused && !_gSlider.IsFocused && !_bSlider.IsFocused)
                    {
                        _syncing = true;
                        try
                        {
                            _rSlider.Value = color.R;
                            _gSlider.Value = color.G;
                            _bSlider.Value = color.B;
                        }
                        finally
                        {
                            _syncing = false;
                        }
                    }
                    _preview.Background = new SolidColorBrush(Color.FromRgb(color.R, color.G, color.B));
                }
                else
                {
                    if (!_hexBox.IsFocused && _hexBox.Text != value)
                        _hexBox.Text = value;
                    _preview.Background = Brushes.Transparent;
                }
            }
        }

        private IBrush? _defaultHexBorder;

        protected override void OnLoaded(RoutedEventArgs e)
        {
            base.OnLoaded(e);
            _defaultHexBorder = _hexBox.BorderBrush;
            _hexBox.LostFocus += (_, _) => CommitHex();
            _hexBox.KeyDown += (_, args) =>
            {
                if (args.Key == Key.Enter)
                    CommitHex();
            };
            Refresh();
        }

        private void CommitHex()
        {
            string text = (_hexBox.Text ?? string.Empty).Trim();
            if (text.Length == 0)
            {
                SelectedValue = string.Empty;
                return;
            }
            if (TryParseHex(text, out _))
            {
                SelectedValue = NormalizeHex(text);
                _hexBox.BorderBrush = _defaultHexBorder;
            }
            else
            {
                _hexBox.BorderBrush = new SolidColorBrush(Colors.Red);
            }
            Refresh();
        }

        /// <summary>Parses #RGB / #RRGGBB / #AARRGGBB (leading # optional).</summary>
        public static bool TryParseHex(string? text, out Color color)
        {
            color = Colors.White;
            if (string.IsNullOrWhiteSpace(text))
                return false;
            string hex = text.Trim();
            if (!hex.StartsWith("#"))
                hex = "#" + hex;
            if (hex.Length == 4)
                hex = $"#{hex[1]}{hex[1]}{hex[2]}{hex[2]}{hex[3]}{hex[3]}";
            return Color.TryParse(hex, out color);
        }

        public static string NormalizeHex(string text)
        {
            string hex = text.Trim();
            if (!hex.StartsWith("#"))
                hex = "#" + hex;
            if (hex.Length == 4)
                hex = $"#{hex[1]}{hex[1]}{hex[2]}{hex[2]}{hex[3]}{hex[3]}";
            return hex.ToUpperInvariant();
        }
    }
}
