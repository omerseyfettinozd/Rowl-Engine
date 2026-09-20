using Avalonia;
using Avalonia.Media;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Güvenlik-ağı renkleri (UI Sadeleştirme Faz 2). Ürün kodundaki TEK
    /// hardcoded-hex noktasıdır: FindResource zinciri bir tema anahtarını
    /// bulamazsa kullanılan yedek değerler. Aktif stil HER ZAMAN tema
    /// token'ından gelir (SettingsViewModel paletleri); buradaki değerler
    /// yalnızca ulaşılamaz-yedek ve runtime okuma varsayılanıdır. Lint
    /// istisnası bu dosyayla sınırlıdır (bkz. EditorEmojiLintTests).
    /// </summary>
    internal static class ThemeFallbackColors
    {
        public static readonly Color Surface = Color.Parse("#232327");
        public static readonly Color Border = Color.Parse("#3A3A40");
        public static readonly Color Text = Color.Parse("#F2EFE6");
        public static readonly Color Muted = Color.Parse("#A8A49C");
        public static readonly Color Dim = Color.Parse("#6E6C66");
        public static readonly Color Success = Color.Parse("#7DA56D");
        public static readonly Color Warning = Color.Parse("#DCA85A");
        public static readonly Color Error = Color.Parse("#D96868");
        public static readonly Color Info = Color.Parse("#9C7FD1");

        public static IBrush SurfaceBrush => new SolidColorBrush(Surface);
        public static IBrush BorderBrush => new SolidColorBrush(Border);
        public static IBrush TextBrush => new SolidColorBrush(Text);

        /// <summary>
        /// Fırça anahtarının güncel rengini hex string olarak okur; böylece
        /// .cs tarafındaki runtime renkleri tema değişimini takip eder.
        /// Anahtar bulunamazsa verilen yedeği döner. Opak renkler
        /// 6-haneli `#RRGGBB` formatında döner (test karşılaştırmaları için stabil).
        /// </summary>
        public static string BrushHex(string brushKey, Color fallback)
        {
            var app = Application.Current;
            if (app is not null && app.Resources.TryGetValue(brushKey, out var v))
            {
                if (v is SolidColorBrush sb) return ToHex(sb.Color);
                if (v is Color c) return ToHex(c);
            }
            return ToHex(fallback);
        }

        private static string ToHex(Color c) =>
            c.A == 0xFF ? $"#{c.R:X2}{c.G:X2}{c.B:X2}" : c.ToString();
    }
}
