using System;
using Avalonia;
using Avalonia.Controls;
using RowlEngine.Editor.Services.Inspector;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 4 — inline field validation badge. Binds to the shared
    /// <see cref="InspectorValidationService"/> and shows the first matching
    /// issue for one (node, field) pair: red for errors, amber for
    /// warnings (full message in the tooltip). Collapsed when the field is
    /// clean. Refresh is event-driven (<c>InspectorValidationService.Changed</c>),
    /// so typing in the Inspector updates badges without polling.
    /// </summary>
    public class FieldValidationBadge : Border
    {
        public static readonly StyledProperty<InspectorValidationService?> ValidationProperty =
            AvaloniaProperty.Register<FieldValidationBadge, InspectorValidationService?>(nameof(Validation));

        public static readonly StyledProperty<ulong> NodeIdProperty =
            AvaloniaProperty.Register<FieldValidationBadge, ulong>(nameof(NodeId));

        public static readonly StyledProperty<string?> FieldKeyProperty =
            AvaloniaProperty.Register<FieldValidationBadge, string?>(nameof(FieldKey));

        public InspectorValidationService? Validation
        {
            get => GetValue(ValidationProperty);
            set => SetValue(ValidationProperty, value);
        }

        public ulong NodeId
        {
            get => GetValue(NodeIdProperty);
            set => SetValue(NodeIdProperty, value);
        }

        public string? FieldKey
        {
            get => GetValue(FieldKeyProperty);
            set => SetValue(FieldKeyProperty, value);
        }

        private readonly TextBlock _label;

        public FieldValidationBadge()
        {
            CornerRadius = new CornerRadius(4);
            Padding = new Thickness(6, 2);
            Margin = new Thickness(0, 2, 0, 0);
            _label = new TextBlock
            {
                FontSize = 10,
                FontWeight = Avalonia.Media.FontWeight.SemiBold,
                TextWrapping = Avalonia.Media.TextWrapping.Wrap,
            };
            Child = _label;
            IsVisible = false;
        }

        static FieldValidationBadge()
        {
            ValidationProperty.Changed.AddClassHandler<FieldValidationBadge>((b, _) => b.Rehook());
            NodeIdProperty.Changed.AddClassHandler<FieldValidationBadge>((b, _) => b.Refresh());
            FieldKeyProperty.Changed.AddClassHandler<FieldValidationBadge>((b, _) => b.Refresh());
        }

        private InspectorValidationService? _hooked;

        private void Rehook()
        {
            if (!ReferenceEquals(_hooked, Validation))
            {
                if (_hooked is not null)
                    _hooked.Changed -= OnValidationChanged;
                _hooked = Validation;
                if (_hooked is not null)
                    _hooked.Changed += OnValidationChanged;
            }
            Refresh();
        }

        private void OnValidationChanged(object? sender, EventArgs e) => Refresh();

        /// <summary>Re-evaluates the badge (tests + property changes).</summary>
        public void Refresh()
        {
            if (Validation is null || string.IsNullOrEmpty(FieldKey) || NodeId == 0)
            {
                IsVisible = false;
                return;
            }
            var hits = Validation.IssuesFor(NodeId, FieldKey);
            if (hits.Count == 0)
            {
                IsVisible = false;
                return;
            }
            var first = hits[0];
            foreach (var hit in hits)
            {
                if (hit.IsError)
                {
                    first = hit;
                    break;
                }
            }
            _label.Text = first.IsError ? $"Hata: {first.Message}" : $"Uyarı: {first.Message}";
            string color = first.IsError ? "#7F1D1D" : "#78350F";
            string border = first.IsError ? "#EF4444" : "#F59E0B";
            Background = new Avalonia.Media.SolidColorBrush(
                Avalonia.Media.Color.Parse(first.IsError ? "#FECACA" : "#FEF3C7"));
            BorderBrush = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse(border));
            BorderThickness = new Thickness(1);
            _label.Foreground = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse(color));
            ToolTip.SetTip(this, first.Message);
            IsVisible = true;
        }
    }
}
