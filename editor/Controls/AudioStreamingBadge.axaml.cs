using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 5 Dilim 1 — OGG streaming karar rozeti. <c>Mode</c>
    /// <c>"stream"</c> iken yeşil STREAM rozeti gösterir (metin
    /// <see cref="Services.AudioStreamingBadgeService"/> üretir);
    /// <c>memory</c>/<c>unknown</c> modlarında gizlenir (fail-closed).
    /// Karar mantığı servistedir; bu kontrol yalnızca görünümdür.
    /// </summary>
    public partial class AudioStreamingBadge : UserControl
    {
        public static readonly StyledProperty<string> ModeProperty =
            AvaloniaProperty.Register<AudioStreamingBadge, string>(
                nameof(Mode), "unknown");

        public static readonly StyledProperty<string> BadgeTextProperty =
            AvaloniaProperty.Register<AudioStreamingBadge, string>(
                nameof(BadgeText), string.Empty);

        public string Mode
        {
            get => GetValue(ModeProperty);
            set => SetValue(ModeProperty, value);
        }

        public string BadgeText
        {
            get => GetValue(BadgeTextProperty);
            set => SetValue(BadgeTextProperty, value);
        }

        public AudioStreamingBadge()
        {
            InitializeComponent();
            Refresh();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == ModeProperty || change.Property == BadgeTextProperty)
                Refresh();
        }

        private void Refresh()
        {
            // Çift kapı: Mode tek başına yetmez, boş metinli tutarsız
            // iddia da gizlenir (ViewModel zaten Visible ile yönetir).
            bool isStream = Mode == "stream" && !string.IsNullOrEmpty(BadgeText);
            IsVisible = isStream;
            if (!isStream)
                return;
            var label = this.FindControl<TextBlock>("BadgeLabel");
            var border = this.FindControl<Border>("BadgeBorder");
            if (label is not null)
            {
                label.Text = BadgeText;
                label.Foreground = this.FindResource("SuccessColor") as IBrush ?? ThemeFallbackColors.TextBrush;
            }
            if (border is not null)
            {
                border.Background = this.FindResource("SuccessBrush") as IBrush ?? ThemeFallbackColors.SurfaceBrush;
                border.BorderBrush = this.FindResource("SuccessColor") as IBrush ?? ThemeFallbackColors.BorderBrush;
                border.BorderThickness = new Thickness(1);
            }
        }
    }
}
