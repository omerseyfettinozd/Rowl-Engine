using System;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 5 Dilim 2 — pump maliyeti rozet açıklaması (saf, native çağrı
/// YOKTUR). Motorun <c>RowlEngine_GetBgmPumpStatsJson</c> çıktısını
/// (<c>count/last_us/avg_us/max_us</c>) Inspector rozetine çevirir.
/// Bozuk/eksik JSON ve sıfır-sayaç fail-closed kapanır (gizli rozet).
/// Görünürlük kararı <see cref="AudioMixerService"/> tek kaynağına
/// delege edilir (<c>TryParsePumpStats</c> + sayaç eşiği); şema kopyası
/// YOKTUR.
/// </summary>
internal sealed record MixerBadgeDescription(
    bool Visible, string Text);

internal static class MixerBadgeService
{
    public static MixerBadgeDescription Describe(string? pumpStatsJson)
    {
        if (string.IsNullOrWhiteSpace(pumpStatsJson))
            return new(false, string.Empty);
        try
        {
            // Tek karar kaynağı: AudioMixerService.TryParsePumpStats.
            // Örneklenmemiş sayaç (count==0, örn. stream yok / taze motor)
            // ve tutarsız iddia gizli rozet döner; burada şema kopyası
            // YOKTUR. Pump gözlemseldir (fail gate yok); rozet yalnızca
            // bilgi verir.
            if (!AudioMixerService.TryParsePumpStats(pumpStatsJson, out PumpStatsSnapshot snapshot) ||
                snapshot.Count <= 0)
                return new(false, string.Empty);
            return new(true, $"PUMP • avg {snapshot.AvgUs}µs • max {snapshot.MaxUs}µs");
        }
        catch (Exception)
        {
            return new(false, string.Empty);
        }
    }
}
