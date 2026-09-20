using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using RowlEngine.Editor.Services.Search;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 4 — color editor with two modes. <c>Palette</c> assigns a
    /// node color tag (preset swatches + clear, hex readout via
    /// <c>NodeColorTags.ToHex</c>); <c>Hex</c> edits a raw #RRGGBB string
    /// (preview chip + commit-on-valid, invalid input never propagates).
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

        private readonly StackPanel _swatches = new() { Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 4 };
        private readonly TextBlock _hexReadout = new() { FontSize = 10, VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center };
        private readonly TextBox _hexBox = new() { FontSize = 11, Width = 92, Watermark = "#RRGGBB" };
        private readonly Border _preview = new() { Width = 22, Height = 22, CornerRadius = new CornerRadius(4) };

        public ColorPickerControl()
        {
            var root = new StackPanel { Spacing = 4, Orientation = Avalonia.Layout.Orientation.Horizontal };
            var clear = new Button
            {
                Content = "X",
                FontSize = 10,
                Padding = new Thickness(6, 2),
                [ToolTip.TipProperty] = "Temizle",
            };
            clear.Click += (_, _) => SelectedValue = string.Empty;
            BuildSwatches();
            root.Children.Add(_swatches);
            root.Children.Add(_preview);
            root.Children.Add(_hexBox);
            root.Children.Add(_hexReadout);
            root.Children.Add(clear);
            Content = root;
            ApplyMode();
        }

        static ColorPickerControl()
        {
            ModeProperty.Changed.AddClassHandler<ColorPickerControl>((c, _) => c.ApplyMode());
            SelectedValueProperty.Changed.AddClassHandler<ColorPickerControl>((c, _) => c.Refresh());
        }

        private void BuildSwatches()
        {
            _swatches.Children.Clear();
            foreach (string name in NodeColorTags.PaletteNames)
            {
                var swatch = new Border
                {
                    Width = 20,
                    Height = 20,
                    CornerRadius = new CornerRadius(10),
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
        }

        private void ApplyMode()
        {
            bool hex = Mode == ColorPickerMode.Hex;
            _hexBox.IsVisible = hex;
            _hexReadout.IsVisible = !hex;
            Refresh();
        }

        private void Refresh()
        {
            string value = SelectedValue ?? string.Empty;
            foreach (var child in _swatches.Children.OfType<Border>())
            {
                bool active = Mode == ColorPickerMode.Palette
                    ? string.Equals(child.Tag as string, value, StringComparison.OrdinalIgnoreCase)
                    : string.Equals(NodeColorTags.ToHex(child.Tag as string), value, StringComparison.OrdinalIgnoreCase);
                child.BorderThickness = new Thickness(active ? 2 : 0);
            }
            if (Mode == ColorPickerMode.Palette)
            {
                _hexReadout.Text = string.IsNullOrEmpty(value) ? "yok" : NodeColorTags.ToHex(value);
                _preview.Background = string.IsNullOrEmpty(value)
                    ? Brushes.Transparent
                    : NodeColorTags.GetBrush(value);
            }
            else
            {
                if (_hexBox.IsFocused)
                    return;
                if (_hexBox.Text != value)
                    _hexBox.Text = value;
                _preview.Background = TryParseHex(value, out Color color)
                    ? new SolidColorBrush(color)
                    : Brushes.Transparent;
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
