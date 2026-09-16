using System;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 5 Dilim 1 — StreamInfo karar yüzeyi (saf + polling-free, native çağrı
/// YOKTUR). Motorun <c>RowlEngine_GetStreamInfoJson</c> çıktısını
/// (<c>mode/duration_seconds/threshold_seconds/threshold_bytes/
/// buffered_seconds/reason/channel/asset</c>) tip güvenli snapshot'a çevirir.
///
/// <c>EngineHost</c>'a dokunmaz: canlı okuma, çağrıcının verdiği delege
/// üzerinden yapılır (null delege = ölü handle, fail-closed). Kendi
/// timer/thread'i yoktur; yoklama kadansını çağrıcı seçer (Inspector rozeti
/// mevcut telemetri tick'inde en fazla 4 Hz yoklar). Bozuk/eksik JSON
/// fail-closed kapanır (<see cref="StreamInfoSnapshot.Unknown"/>).
/// Karar rozeti metni için <see cref="AudioStreamingBadgeService"/> tek
/// kaynaktır (mantık kopyası yok).
/// </summary>
public sealed record StreamInfoSnapshot(
    string Mode,
    double DurationSeconds,
    double ThresholdSeconds,
    long ThresholdBytes,
    double BufferedSeconds,
    string Reason,
    int Channel,
    string Asset)
{
    public static readonly StreamInfoSnapshot Unknown = new(
        "unknown", -1.0, 0.0, AudioStreamingService.ThresholdBytes,
        0.0, "unknown_header", 0, string.Empty);

    /// <summary>Native yönlendirme kararı: stream ⟺ over_threshold + strict &gt;.</summary>
    public bool IsStream =>
        Mode == "stream" && Reason == "over_threshold" &&
        double.IsFinite(DurationSeconds) && double.IsFinite(ThresholdSeconds) &&
        DurationSeconds > ThresholdSeconds;
}

public static class AudioStreamingService
{
    /// <summary>64 MiB decode bütçesi; StreamInfo threshold_bytes her zaman budur.</summary>
    public const long ThresholdBytes = 64L * 1024 * 1024;

    /// <summary>
    /// StreamInfo JSON'unu snapshot'a çevirir. Null/boş/bozuk girdi,
    /// tanınmayan mod ya da eksik sayı alanı fail-closed
    /// <see cref="StreamInfoSnapshot.Unknown"/> döner (throw yok).
    /// Wire-format null kuralı: native non-finite double için çıplak
    /// NaN/Infinity yerine JSON null yazar; duration/threshold/buffered
    /// alanlarında explicit null nullable double üzerinden
    /// <see cref="double.NaN"/> eşlenir (kayıp/yanlış tip hâlâ fallback).
    /// </summary>
    public static StreamInfoSnapshot Parse(string? streamInfoJson)
    {
        if (string.IsNullOrWhiteSpace(streamInfoJson))
            return StreamInfoSnapshot.Unknown;
        try
        {
            using var document = JsonDocument.Parse(streamInfoJson);
            JsonElement root = document.RootElement;
            string mode = ReadString(root, "mode");
            if (mode is not ("stream" or "memory" or "unknown"))
                mode = "unknown";
            string reason = ReadString(root, "reason");
            if (reason is not ("under_threshold" or "over_threshold" or "no_bgm" or "unknown_header"))
                reason = mode == "stream" ? "over_threshold" : "unknown_header";
            return new StreamInfoSnapshot(
                mode,
                ReadFinite(root, "duration_seconds", -1.0),
                ReadFinite(root, "threshold_seconds", 0.0),
                ReadThresholdBytes(root),
                ReadFinite(root, "buffered_seconds", 0.0),
                reason,
                ReadChannel(root),
                ReadString(root, "asset"));
        }
        catch (Exception)
        {
            return StreamInfoSnapshot.Unknown;
        }
    }

    /// <summary>
    /// <see cref="Parse"/> ile aynı null-wire eşlemesini yapar
    /// (explicit null → <see cref="double.NaN"/>); bozuk JSON/null girdi
    /// için <c>false</c> + <see cref="StreamInfoSnapshot.Unknown"/> döner.
    /// </summary>
    public static bool TryParse(string? streamInfoJson, out StreamInfoSnapshot snapshot)
    {
        if (string.IsNullOrWhiteSpace(streamInfoJson))
        {
            snapshot = StreamInfoSnapshot.Unknown;
            return false;
        }
        try
        {
            using var document = JsonDocument.Parse(streamInfoJson);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                snapshot = StreamInfoSnapshot.Unknown;
                return false;
            }
            snapshot = Parse(streamInfoJson);
            return true;
        }
        catch (Exception)
        {
            snapshot = StreamInfoSnapshot.Unknown;
            return false;
        }
    }

    /// <summary>
    /// Eşik-karar tutarlılığı: snapshot <c>stream</c> diyorsa duration
    /// strictly threshold üstünde ve reason over_threshold olmalıdır
    /// (native <c>assessLongAudio</c> operatörü; eşitlik RAM'dir).
    /// </summary>
    public static bool IsDecisionConsistent(StreamInfoSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.Mode == "stream")
            return snapshot.IsStream;
        return !snapshot.IsStream;
    }

    /// <summary>
    /// Ham <c>RowlEngine_IsStreaming</c> çıktısını bool karara çevirir.
    /// Null delege (ölü handle) ve throw fail-closed <c>false</c> döner.
    /// </summary>
    public static bool ReadIsStreaming(Func<int>? readRaw)
    {
        if (readRaw is null)
            return false;
        try
        {
            return readRaw() != 0;
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>
    /// Verilen okuyucudan StreamInfo snapshot'ı alır (polling-free: çağrıcı
    /// ne zaman isterse çağırır). Null delege/throw/boş JSON fail-closed
    /// <see cref="StreamInfoSnapshot.Unknown"/> döner.
    /// </summary>
    public static StreamInfoSnapshot ReadSnapshot(Func<string?>? readJson)
    {
        if (readJson is null)
            return StreamInfoSnapshot.Unknown;
        try
        {
            return Parse(readJson());
        }
        catch (Exception)
        {
            return StreamInfoSnapshot.Unknown;
        }
    }

    /// <summary>Rozet metni; tek kaynak <see cref="AudioStreamingBadgeService"/>.</summary>
    public static string ReadBadgeText(Func<string?>? readJson)
    {
        if (readJson is null)
            return string.Empty;
        try
        {
            return AudioStreamingBadgeService.Describe(readJson()).Text;
        }
        catch (Exception)
        {
            return string.Empty;
        }
    }

    /// <summary>
    /// Native setter aynası: [0,1] clamp + non-finite ignore (son geçerli
    /// değer korunur). Ölü handle'da çağrıcı yazmaz; bu yardımcı yalnızca
    /// saf matematiği pinler.
    /// </summary>
    public static float AcceptVolume(float current, float candidate)
    {
        if (!float.IsFinite(candidate))
            return current;
        return Math.Clamp(candidate, 0.0f, 1.0f);
    }

    /// <summary>
    /// PlayAudio kanal haritası (C API doc aynası): 0=Bgm, 1=Voice, 2=Sfx,
    /// 3=Ambience, 4=Ui; negatif/&gt;4 else-Sfx dalına düşer.
    /// </summary>
    public static string ChannelName(int channel) => channel switch
    {
        0 => "Bgm",
        1 => "Voice",
        2 => "Sfx",
        3 => "Ambience",
        4 => "Ui",
        _ => "Sfx",
    };

    private static string ReadString(JsonElement root, string property) =>
        root.TryGetProperty(property, out JsonElement element) &&
        element.ValueKind == JsonValueKind.String
            ? element.GetString() ?? string.Empty
            : string.Empty;

    private static double ReadFinite(JsonElement root, string property, double fallback)
    {
        if (!root.TryGetProperty(property, out JsonElement element))
            return fallback;
        // Wire-format null kuralı: native non-finite double için çıplak
        // NaN/Infinity yerine JSON null yazar; explicit null nullable
        // double üzerinden NaN eşlenir (IsStream fail-closed kalır).
        if (element.ValueKind == JsonValueKind.Null)
            return double.NaN;
        double? number = element.ValueKind == JsonValueKind.Number &&
            element.TryGetDouble(out double raw)
                ? raw
                : (double?)null;
        if (number.HasValue)
            return double.IsFinite(number.Value) ? number.Value : double.NaN;
        return fallback;
    }

    private static long ReadThresholdBytes(JsonElement root)
    {
        if (root.TryGetProperty("threshold_bytes", out JsonElement element) &&
            element.ValueKind == JsonValueKind.Number &&
            element.TryGetInt64(out long value) && value > 0)
            return value;
        return ThresholdBytes;
    }

    private static int ReadChannel(JsonElement root)
    {
        if (root.TryGetProperty("channel", out JsonElement element) &&
            element.ValueKind == JsonValueKind.Number &&
            element.TryGetInt32(out int value))
            return value;
        return 0;
    }
}
