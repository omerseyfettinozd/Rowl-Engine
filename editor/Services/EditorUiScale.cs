using System;

namespace RowlEngine.Editor.Services;

/// <summary>Faz 8 D1: arayüz ölçeği canlı durumu (pencere LayoutTransform ile dinler).</summary>
public static class EditorUiScale
{
    // Oturmuş ölçek (hep izinli değer).
    public static double Current { get; private set; } = 1.0;

    // Ölçek değişince yükselir.
    public static event Action<double>? Changed;

    // Kural: NaN/sonsuz → 1.0, yoksa en yakın izinli.
    public static double Snap(double scale)
    {
        if (double.IsNaN(scale) || double.IsInfinity(scale))
            return 1.0;
        var allowed = EditorSettingsProfile.AllowedUiScales;
        double best = allowed[0];
        double bestDist = Math.Abs(scale - best);
        for (int i = 1; i < allowed.Count; i++)
        {
            double d = Math.Abs(scale - allowed[i]);
            if (d < bestDist)
            {
                bestDist = d;
                best = allowed[i];
            }
        }
        return best;
    }

    // Oturt, değişmişse yay.
    public static void Apply(double scale)
    {
        double snapped = Snap(scale);
        if (snapped != Current)
        {
            Current = snapped;
            Changed?.Invoke(snapped);
        }
    }
}
