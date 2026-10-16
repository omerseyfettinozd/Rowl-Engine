using System;

namespace RowlEngine.Editor.Views
{
    /// <summary>
    /// Ana penceredeki sağ alt bildirim yığını (kurtarma / içe aktarma / işlem kartları)
    /// birlikte açılınca Denetçi ve Varlıklar panellerini örtmesin diye kart alanı pencere
    /// yüksekliğinin bir payıyla sınırlanır; fazlası kaydırılır. Saf hesap; test edilir.
    /// </summary>
    public static class NotificationStackMetrics
    {
        /// <summary>Kart alanının kabuk yüksekliğine oranı.</summary>
        public const double MaxHeightRatio = 0.4;

        /// <summary>Küçük pencerede bile en az bir kartın tamamı görünsün.</summary>
        public const double MinCardsHeight = 160;

        public static double MaxCardsHeight(double shellHeight)
        {
            if (!double.IsFinite(shellHeight) || shellHeight <= 0)
                return double.PositiveInfinity;
            return Math.Max(MinCardsHeight, Math.Floor(shellHeight * MaxHeightRatio));
        }
    }
}
