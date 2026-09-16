using System;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 5 Dilim 1 — StreamInfo JSON rozet açıklaması (saf, native çağrı
/// YOKTUR). Motorun <c>RowlEngine_GetStreamInfoJson</c> çıktısını
/// (<c>mode/duration_seconds/threshold_seconds/threshold_bytes/
/// buffered_seconds/reason/channel/asset</c>) Inspector rozetine çevirir.
/// Bozuk/eksik JSON fail-closed kapanır (gizli rozet, <c>unknown</c> modu).
/// Görünürlük kararı <see cref="AudioStreamingService"/> tek kaynağına
/// delege edilir (<c>StreamInfoSnapshot.IsStream</c>); eşik formülü kopyası
/// YOKTUR.
/// </summary>
internal sealed record StreamingBadgeDescription(
    bool Visible, string Mode, string Text);

internal static class AudioStreamingBadgeService
{
    public static StreamingBadgeDescription Describe(string? streamInfoJson)
    {
        if (string.IsNullOrWhiteSpace(streamInfoJson))
            return new(false, "unknown", string.Empty);
        try
        {
            // Tek karar kaynağı: AudioStreamingService.Parse + IsStream
            // (mode==stream && reason==over_threshold && finite strict
            // duration>threshold). Tutarsız stream iddiası (ör. duration
            // null) gizli rozet döner; burada eşik formülü kopyası YOKTUR.
            StreamInfoSnapshot snapshot = AudioStreamingService.Parse(streamInfoJson);
            if (!snapshot.IsStream)
                return new(false, snapshot.Mode, string.Empty);
            double duration = snapshot.DurationSeconds;
            double buffered = snapshot.BufferedSeconds;
            string detail = duration > 0
                ? $"{duration:0}s audio"
                : "long audio";
            if (buffered >= 0)
                detail += $" • buf {buffered:0.0}s";
            return new(true, "stream", $"STREAM • {detail}");
        }
        catch (Exception)
        {
            return new(false, "unknown", string.Empty);
        }
    }
}
