using System;
using Avalonia;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Mini harita kartı sol altta, kendi aç/kapa düğmesinin (zoom kapsülü satırı) hemen
    /// üstünde durur. Eskiden sağ üstteydi ve ilk düğüm sırasının üstüne biniyordu (düğümler
    /// sol üstten sağa dizilir). Panel kısalınca kart sol üstteki araç satırına (kırıntı +
    /// düğüm komutları) değecekse gizlenir. Saf hesap; test edilir.
    /// </summary>
    public static class MinimapPlacement
    {
        /// <summary>Kartın panel kenarlarından boşluğu (XAML Margin="16" ile aynı).</summary>
        public const double EdgeMargin = 16;

        /// <summary>Araç satırı ile kart arasındaki en az boşluk.</summary>
        public const double Gap = 8;

        /// <summary>Alttaki zoom / harita kapsülü için ayrılan yükseklik (16 kenar + 38 kapsül + 8).</summary>
        public const double BottomReserve = 62;

        public readonly record struct Result(double Top, bool IsVisible);

        /// <param name="host">Node tuvalinin boyutu.</param>
        /// <param name="toolRow">Sol üst araç satırının aynı koordinat düzlemindeki sınırları.</param>
        /// <param name="card">Mini harita kartının (kenar boşluğu hariç) boyutu.</param>
        public static Result Resolve(Size host, Rect toolRow, Size card)
        {
            double top = host.Height - BottomReserve - card.Height;
            bool rowPresent = toolRow.Width > 0 && toolRow.Height > 0;
            double minTop = rowPresent ? Math.Max(EdgeMargin, toolRow.Bottom + Gap) : EdgeMargin;
            bool fits = top >= minTop && card.Width + 2 * EdgeMargin <= host.Width;
            return new Result(top, fits);
        }
    }
}
