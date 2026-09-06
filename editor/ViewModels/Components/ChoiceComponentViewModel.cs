using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Avalonia.Media.Imaging;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>A stable, player-visible branch option owned by a Choice component.</summary>
    public partial class ChoiceOptionViewModel : ObservableObject
    {
        [ObservableProperty] private string _optionId = Guid.NewGuid().ToString("N")[..12];
        [ObservableProperty] private string _text = "New choice";
        [ObservableProperty] private ulong _targetNodeId;
        [ObservableProperty] private string _condition = "true";
        [ObservableProperty] private bool _isEnabled = true;

        // Runtime button appearance. These values deliberately live with the
        // option, so the graph route and the clickable UI cannot drift apart.
        [ObservableProperty] private bool _isExpanded;
        [ObservableProperty] private string _stylePreset = "Neon";
        [ObservableProperty] private double _fontSize = 22.0;
        [ObservableProperty] private string _fontFamily = "Default";
        [ObservableProperty] private string _textColor = "#FFFFFF";
        [ObservableProperty] private string _backgroundColor = "#1E293B";
        [ObservableProperty] private string _hoverColor = "#0EA5E9";
        [ObservableProperty] private string _disabledColor = "#475569";
        [ObservableProperty] private string _borderColor = "#38BDF8";
        [ObservableProperty] private double _borderThickness = 2.0;
        [ObservableProperty] private double _cornerRadius = 8.0;
        [ObservableProperty] private double _width = 560.0;
        [ObservableProperty] private double _height = 64.0;
        [ObservableProperty] private double _padding = 16.0;
        [ObservableProperty] private string _textAlignment = "Center";
        [ObservableProperty] private string _backgroundImage = string.Empty;
        [ObservableProperty] private Bitmap? _backgroundBitmap;
        [ObservableProperty] private double _x = 680.0;
        [ObservableProperty] private double _y = 520.0;
        [ObservableProperty] private double _opacity = 1.0;
        [ObservableProperty] private string _anchor = "Center";
        [ObservableProperty] private string _normalImage = string.Empty;
        [ObservableProperty] private string _hoverImage = string.Empty;
        [ObservableProperty] private string _pressedImage = string.Empty;
        [ObservableProperty] private string _disabledImage = string.Empty;
        [ObservableProperty] private Bitmap? _normalBitmap;

        partial void OnBackgroundImageChanged(string value)
        {
            BackgroundBitmap = AssetBitmapCache.GetOrLoad(value);
            if (string.IsNullOrEmpty(NormalImage)) NormalImage = value;
        }
        partial void OnNormalImageChanged(string value) => NormalBitmap = AssetBitmapCache.GetOrLoad(value);

        [RelayCommand]
        private void ApplyPreset(string? preset)
        {
            StylePreset = preset ?? "Neon";
            ConfigurePreset(StylePreset);
        }

        partial void OnStylePresetChanged(string value) => ConfigurePreset(value);

        private void ConfigurePreset(string preset)
        {
            switch (preset)
            {
                case "Minimal":
                    BackgroundColor = "#111827"; HoverColor = "#374151"; BorderColor = "#6B7280"; CornerRadius = 4; BorderThickness = 1; break;
                case "Paper":
                    BackgroundColor = "#F5E6C8"; HoverColor = "#E8C98E"; TextColor = "#241A12"; BorderColor = "#7C5C3B"; CornerRadius = 2; BorderThickness = 2; break;
                case "Danger":
                    BackgroundColor = "#450A0A"; HoverColor = "#B91C1C"; TextColor = "#FEE2E2"; BorderColor = "#EF4444"; CornerRadius = 8; BorderThickness = 2; break;
                default:
                    BackgroundColor = "#1E293B"; HoverColor = "#0EA5E9"; TextColor = "#FFFFFF"; BorderColor = "#38BDF8"; CornerRadius = 8; BorderThickness = 2; break;
            }
        }
    }

    /// <summary>
    /// Defines a player decision. The option ID, rather than its visual order,
    /// is the stable identity used by graph edges, saves and the native runtime.
    /// </summary>
    public partial class ChoiceComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Player Choices";
        public override string Icon => "🔀";
        public override string TypeKey => "choice";

        [ObservableProperty] private string _layout = "Vertical";
        [ObservableProperty] private string _title = "Choose";
        public ObservableCollection<ChoiceOptionViewModel> Options { get; } = new();

        /// <summary>
        /// Lightweight notification used when an existing option changes.  Keeping
        /// this separate from <see cref="Options"/> prevents Avalonia from rebuilding
        /// the entire button ItemsControl for every typed character or drag pixel.
        /// </summary>
        public bool ChoiceDataChanged => true;

        public ChoiceComponentViewModel()
        {
            Options.CollectionChanged += OnOptionsCollectionChanged;
            AddTrackedOption(new ChoiceOptionViewModel { Text = "Continue", Y = 520 });
        }

        [RelayCommand]
        private void AddOption() => AddTrackedOption(new ChoiceOptionViewModel
        {
            Text = $"Choice {Options.Count + 1}",
            Y = Math.Min(980, 520 + Options.Count * 80)
        });

        [RelayCommand]
        private void RemoveOption(ChoiceOptionViewModel? option)
        {
            if (option != null)
            {
                option.PropertyChanged -= OnOptionPropertyChanged;
                Options.Remove(option);
            }
        }

        public override Dictionary<string, object> Serialize() => new()
        {
            ["title"] = Title,
            ["layout"] = Layout,
            ["options"] = Options.Select(option => new Dictionary<string, object>
            {
                ["option_id"] = option.OptionId,
                ["text"] = option.Text,
                ["target_node_id"] = option.TargetNodeId,
                ["condition"] = option.Condition,
                ["enabled"] = option.IsEnabled,
                ["style_preset"] = option.StylePreset,
                ["font_size"] = option.FontSize,
                ["font_family"] = option.FontFamily,
                ["text_color"] = option.TextColor,
                ["background_color"] = option.BackgroundColor,
                ["hover_color"] = option.HoverColor,
                ["disabled_color"] = option.DisabledColor,
                ["border_color"] = option.BorderColor,
                ["border_thickness"] = option.BorderThickness,
                ["corner_radius"] = option.CornerRadius,
                ["width"] = option.Width,
                ["height"] = option.Height,
                ["padding"] = option.Padding,
                ["text_alignment"] = option.TextAlignment,
                ["background_image"] = option.BackgroundImage,
                ["x"] = option.X,
                ["y"] = option.Y,
                ["opacity"] = option.Opacity,
                ["anchor"] = option.Anchor,
                ["normal_image"] = option.NormalImage,
                ["hover_image"] = option.HoverImage,
                ["pressed_image"] = option.PressedImage,
                ["disabled_image"] = option.DisabledImage
            }).ToList()
        };

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("title", out var title) && title is string value) Title = value;
            if (data.TryGetValue("layout", out var layout) && layout is string layoutValue) Layout = layoutValue;
            if (!data.TryGetValue("options", out var options) || options is not string rawJson) return;
            try
            {
                using var document = JsonDocument.Parse(rawJson);
                if (document.RootElement.ValueKind != JsonValueKind.Array) return;
                foreach (var existing in Options)
                    existing.PropertyChanged -= OnOptionPropertyChanged;
                Options.Clear();
                var index = 0;
                foreach (var item in document.RootElement.EnumerateArray())
                {
                    AddTrackedOption(new ChoiceOptionViewModel
                    {
                        OptionId = item.TryGetProperty("option_id", out var id) ? id.GetString() ?? Guid.NewGuid().ToString("N")[..12] : Guid.NewGuid().ToString("N")[..12],
                        Text = item.TryGetProperty("text", out var text) ? text.GetString() ?? "Choice" : "Choice",
                        TargetNodeId = item.TryGetProperty("target_node_id", out var target) && target.TryGetUInt64(out var targetId) ? targetId : 0,
                        Condition = item.TryGetProperty("condition", out var condition) ? condition.GetString() ?? "true" : "true",
                        IsEnabled = !item.TryGetProperty("enabled", out var enabled) || enabled.GetBoolean(),
                        StylePreset = GetString(item, "style_preset", "Neon"),
                        FontSize = GetNumber(item, "font_size", 22),
                        FontFamily = GetString(item, "font_family", "Default"),
                        TextColor = GetString(item, "text_color", "#FFFFFF"),
                        BackgroundColor = GetString(item, "background_color", "#1E293B"),
                        HoverColor = GetString(item, "hover_color", "#0EA5E9"),
                        DisabledColor = GetString(item, "disabled_color", "#475569"),
                        BorderColor = GetString(item, "border_color", "#38BDF8"),
                        BorderThickness = GetNumber(item, "border_thickness", 2),
                        CornerRadius = GetNumber(item, "corner_radius", 8),
                        Width = GetNumber(item, "width", 560),
                        Height = GetNumber(item, "height", 64),
                        Padding = GetNumber(item, "padding", 16),
                        TextAlignment = GetString(item, "text_alignment", "Center"),
                        BackgroundImage = GetString(item, "background_image", ""),
                        X = GetNumber(item, "x", 680),
                        Y = GetNumber(item, "y", 520 + index * 80),
                        Opacity = GetNumber(item, "opacity", 1),
                        Anchor = GetString(item, "anchor", "Center"),
                        NormalImage = GetString(item, "normal_image", GetString(item, "background_image", "")),
                        HoverImage = GetString(item, "hover_image", ""),
                        PressedImage = GetString(item, "pressed_image", ""),
                        DisabledImage = GetString(item, "disabled_image", "")
                    });
                    index++;
                }
            }
            catch (JsonException) { }
        }

        private void AddTrackedOption(ChoiceOptionViewModel option)
        {
            Options.Add(option);
        }

        private void OnOptionsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            if (e.OldItems != null)
            {
                foreach (ChoiceOptionViewModel option in e.OldItems)
                    option.PropertyChanged -= OnOptionPropertyChanged;
            }
            if (e.NewItems != null)
            {
                foreach (ChoiceOptionViewModel option in e.NewItems)
                    option.PropertyChanged += OnOptionPropertyChanged;
            }

            // Only additions/removals/reordering replace the collection projection.
            OnPropertyChanged(nameof(Options));
        }

        private void OnOptionPropertyChanged(object? sender, PropertyChangedEventArgs e)
        {
            // The option instance itself already updates all direct XAML bindings.
            // Bubble one cheap marker solely for persistence/native preview work.
            OnPropertyChanged(nameof(ChoiceDataChanged));
        }

        private static string GetString(JsonElement item, string key, string fallback) =>
            item.TryGetProperty(key, out var value) ? value.GetString() ?? fallback : fallback;

        private static double GetNumber(JsonElement item, string key, double fallback) =>
            item.TryGetProperty(key, out var value) && value.TryGetDouble(out var number) ? number : fallback;
    }
}
