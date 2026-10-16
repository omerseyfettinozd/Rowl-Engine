using System;
using Avalonia;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 8 sonrası görsel düzeltme: mini harita kartı sağ üstte durur; Node paneli
    /// daralınca (ör. Assets Store / Düğüm kütüphanesi sağda açıkken) sol üstteki araç
    /// satırıyla (kırıntı + düğüm komutları) yatayda çakışmasın diye satırın altına iner.
    /// Altına inince alttaki görünüm kapsülüne değecekse gizlenir. Saf hesap; test edilir.
    /// </summary>
    public static class MinimapPlacement
    {
        /// <summary>Kartın panel kenarlarından boşluğu (XAML Margin="16" ile aynı).</summary>
        public const double EdgeMargin = 16;

        /// <summary>Araç satırı ile kart arasındaki en az boşluk.</summary>
        public const double Gap = 8;

        /// <summary>Alttaki zoom / harita kapsülü için ayrılan yükseklik (16 kenar + 38 kapsül + 8).</summary>
        public const double BottomReserve = 62;

        public readonly record struct Result(double Top, bool IsVisible, bool IsDropped);

        /// <param name="host">Node tuvalinin boyutu.</param>
        /// <param name="toolRow">Sol üst araç satırının aynı koordinat düzlemindeki sınırları.</param>
        /// <param name="card">Mini harita kartının (kenar boşluğu hariç) boyutu.</param>
        public static Result Resolve(Size host, Rect toolRow, Size card)
        {
            double cardLeft = host.Width - EdgeMargin - card.Width;
            bool rowPresent = toolRow.Width > 0 && toolRow.Height > 0;
            if (!rowPresent || toolRow.Right + Gap <= cardLeft)
                return new Result(EdgeMargin, true, false);

            double top = Math.Max(EdgeMargin, toolRow.Bottom + Gap);
            bool fits = top + card.Height <= host.Height - BottomReserve
                        && card.Width + 2 * EdgeMargin <= host.Width;
            return new Result(top, fits, true);
        }
    }
}
