using System;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Faz 5 Dilim 2 — mixer fade eğrisi (native <c>FadeCurve</c> aynası).</summary>
public enum MixerFadeCurve
{
    Linear = 0,
    EqualPower = 1,
}

/// <summary>
/// Faz 5 Dilim 2 — BGM pump maliyeti snapshot'ı. Native
/// <c>RowlEngine_GetBgmPumpStatsJson</c> çıktısı
/// (<c>count/last_us/avg_us/max_us</c>, tamamı non-negatif tam sayı).
/// </summary>
public sealed record PumpStatsSnapshot(
    long Count,
    long LastUs,
    long AvgUs,
    long MaxUs)
{
    public static readonly PumpStatsSnapshot Unknown = new(0, 0, 0, 0);
}

/// <summary>
/// Faz 5 Dilim 2 — mixer/polyphony/bed/pump karar yüzeyi (saf +
/// polling-free, native çağrı YOKTUR). Motorun Dilim 2 C API çıkışlarını
/// (fade eğrisi, SFX havuz derinliği, ambience bed hacimleri, BGM pump
/// JSON'u) tip güvenli kararlara çevirir.
/// <c>EngineHost</c>'a dokunmaz: canlı okuma, çağrıcının verdiği delege
/// üzerinden yapılır (null delege = ölü handle, fail-closed). Kendi
/// timer/thread'i yoktur; yoklama kadansını çağrıcı seçer.
/// Bozuk/eksik girdi fail-closed kapanır (throw yok).
/// </summary>
public static class AudioMixerService
{
    /// <summary>SFX havuz derinliği tabanı (native <c>kMinDepth</c>).</summary>
    public const int MinPoolDepth = 1;

    /// <summary>SFX havuz derinliği tavanı (native <c>kMaxDepth</c>).</summary>
    public const int MaxPoolDepth = 16;

    /// <summary>SFX havuz varsayılan derinliği (native <c>kDefaultDepth</c>).</summary>
    public const int DefaultPoolDepth = 8;

    /// <summary>
    /// Voice-ducking zayıflatması (native <c>m_duckingFactor</c>, -6 dB).
    /// Yalnız BGM'e uygulanır (<c>master*bgm*duck</c>).
    /// </summary>
    public const float DuckingFactor = 0.5f;

    /// <summary>Legacy ambience yatağı (tek-yatak yolu).</summary>
    public const int BedA = 0;

    /// <summary>Yeni ikinci ambience yatağı.</summary>
    public const int BedB = 1;

    /// <summary>Kazanç tutarlılık toleransı (float çarpım payı).</summary>
    public const float GainTolerance = 1e-5f;

    // ── Mixer kazançları ─────────────────────────────────────────────
    //
    // Native matematik aynası (stream_mixer.hpp): her bus'ta
    // master*bus, duck YALNIZ BGM'de (master*bgm*duckGain).

    /// <summary>Bus efektif kazancı: <c>master*bus</c> (duck YOK).</summary>
    /// <remarks>NOT (fix turu 1): "ui" bus'ı için bu değer yalnız
    /// decode-bake kararlarında geçerlidir; canlı Ui stream kazancı
    /// native'de <c>master*sfx</c> zincirindedir (audio_engine.cpp).
    /// Bu metot hiçbir prod karar yolunda çağrılmaz (yalnız
    /// <see cref="IsGainConsistent"/> + testler).</remarks>
    public static float EffectiveBusGain(float master, float bus) => master * bus;

    /// <summary>
    /// BGM efektif kazancı: duck kapalıyken <c>master*bgm</c>, voice
    /// aktifken <c>master*bgm*duck</c>.
    /// </summary>
    public static float EffectiveBgmGain(float master, float bgm, bool duckActive) =>
        duckActive ? master * bgm * DuckingFactor : master * bgm;

    /// <summary>
    /// Kazanç okuma tutarlılığı: gözlenen kazanç beklenen
    /// <c>master*bus</c> formülüne uyar mı? <c>bus</c> "bgm" ve
    /// <c>duckActive</c> ise duck'lı beklenti kullanılır; diğer
    /// bus'larda duck bayrağı YOK sayılır (duck yalnız BGM).
    /// Non-finite gözlenen değer her zaman tutarsızdır.
    /// </summary>
    public static bool IsGainConsistent(
        string bus, float master, float busVolume, bool duckActive, float observedGain,
        float tolerance = GainTolerance)
    {
        if (!float.IsFinite(observedGain) || tolerance < 0)
            return false;
        float expected = string.Equals(bus, "bgm", StringComparison.OrdinalIgnoreCase) && duckActive
            ? EffectiveBgmGain(master, busVolume, true)
            : EffectiveBusGain(master, busVolume);
        if (!float.IsFinite(expected))
            return false;
        return Math.Abs(observedGain - expected) <= tolerance;
    }

    // ── Fade eğrisi ─────────────────────────────────────────────────

    /// <summary>
    /// Eğri adı/int girdisini parse eder. "linear"/"0" → Linear,
    /// "equalpower"/"equal_power"/"equal-power"/"1" → EqualPower
    /// (case-insensitive, kırpılmış); bozuk girdi fail-closed
    /// Linear döner (strict varsayılan).
    /// </summary>
    public static MixerFadeCurve ParseFadeCurve(string? name)
    {
        string normalized = (name ?? string.Empty).Trim().ToLowerInvariant();
        return normalized switch
        {
            "equalpower" or "equal_power" or "equal-power" or "1" => MixerFadeCurve.EqualPower,
            _ => MixerFadeCurve.Linear,
        };
    }

    /// <summary>
    /// Ham eğri int'ini görünen ada çevirir; 1 dışı her değer
    /// fail-closed "Linear" döner.
    /// </summary>
    public static string FadeCurveName(int curve) =>
        curve == (int)MixerFadeCurve.EqualPower ? "EqualPower" : "Linear";

    /// <summary>
    /// Ham <c>RowlEngine_GetFadeCurve</c> çıktısını karara çevirir.
    /// Null delege (ölü handle) ve throw fail-closed Linear (0) döner;
    /// 0/1 dışı ham değer normalize edilir.
    /// </summary>
    public static int ReadFadeCurve(Func<int>? readRaw)
    {
        if (readRaw is null)
            return (int)MixerFadeCurve.Linear;
        try
        {
            return readRaw() == (int)MixerFadeCurve.EqualPower
                ? (int)MixerFadeCurve.EqualPower
                : (int)MixerFadeCurve.Linear;
        }
        catch (Exception)
        {
            return (int)MixerFadeCurve.Linear;
        }
    }

    // ── SFX polyphony derinliği ──────────────────────────────────────

    /// <summary>Havuz derinliğini [1,16] aralığına clamp'ler.</summary>
    public static int ClampPoolDepth(int depth) => Math.Clamp(depth, MinPoolDepth, MaxPoolDepth);

    /// <summary>
    /// Derinlik adayı: non-finite girdi ignore edilir (son geçerli değer
    /// korunur), finite girdi truncate + [1,16] clamp ile uygulanır
    /// (C++ int cast semantiği).
    /// </summary>
    public static int AcceptPoolDepth(int current, double candidate) =>
        double.IsFinite(candidate) ? ClampPoolDepth((int)candidate) : current;

    /// <summary>
    /// Ham <c>RowlEngine_GetSfxPoolDepth</c> çıktısını karara çevirir.
    /// Null delege/throw fail-closed <see cref="DefaultPoolDepth"/> döner;
    /// ham değer [1,16] aralığına normalize edilir.
    /// </summary>
    public static int ReadPoolDepth(Func<int>? readRaw)
    {
        if (readRaw is null)
            return DefaultPoolDepth;
        try
        {
            return ClampPoolDepth(readRaw());
        }
        catch (Exception)
        {
            return DefaultPoolDepth;
        }
    }

    /// <summary>
    /// Ham <c>RowlEngine_GetSfxActiveVoices</c> çıktısını karara çevirir.
    /// Null delege/throw fail-closed 0 döner; negatif ham değer 0'a
    /// sabitlenir.
    /// </summary>
    public static int ReadActiveVoices(Func<int>? readRaw)
    {
        if (readRaw is null)
            return 0;
        try
        {
            return Math.Max(0, readRaw());
        }
        catch (Exception)
        {
            return 0;
        }
    }

    // ── Ambience bed'leri ────────────────────────────────────────────

    /// <summary>Geçerli yatak: 0 = BedA (legacy), 1 = BedB.</summary>
    public static bool IsValidAmbienceBed(int bed) => bed is BedA or BedB;

    /// <summary>
    /// Bed hacim adayı: [0,1] clamp + non-finite ignore (son geçerli
    /// değer korunur). Native setter semantiği aynası.
    /// </summary>
    public static float AcceptBedVolume(float current, float candidate)
    {
        if (!float.IsFinite(candidate))
            return current;
        return Math.Clamp(candidate, 0.0f, 1.0f);
    }

    /// <summary>
    /// Ham <c>RowlEngine_GetAmbienceBedVolume</c> çıktısını karara çevirir.
    /// Null delege/throw/non-finite fail-closed <paramref name="fallback"/>
    /// döner.
    /// </summary>
    public static float ReadBedVolume(Func<float>? readRaw, float fallback = 0.0f)
    {
        if (readRaw is null)
            return fallback;
        try
        {
            float value = readRaw();
            return float.IsFinite(value) ? value : fallback;
        }
        catch (Exception)
        {
            return fallback;
        }
    }

    /// <summary>
    /// Ham <c>RowlEngine_IsAmbienceBedPlaying</c> çıktısını bool karara
    /// çevirir. Null delege (ölü handle) ve throw fail-closed
    /// <c>false</c> döner.
    /// </summary>
    public static bool ReadIsBedPlaying(Func<int>? readRaw)
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
    /// Ham <c>RowlEngine_IsAmbienceCrossfadeActive</c> çıktısını bool karara
    /// çevirir. Null delege ve throw fail-closed <c>false</c> döner.
    /// </summary>
    public static bool ReadIsCrossfadeActive(Func<int>? readRaw)
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

    // ── Pump maliyeti ────────────────────────────────────────────────

    /// <summary>
    /// Pump JSON'unu snapshot'a çevirir. Null/boş/bozuk girdi, kökte
    /// obje-dışılık, eksik/negatif/kesirli/yanlış-tipli alan fail-closed
    /// <see cref="PumpStatsSnapshot.Unknown"/> döner (throw yok).
    /// </summary>
    public static PumpStatsSnapshot ParsePumpStats(string? pumpStatsJson) =>
        TryParsePumpStats(pumpStatsJson, out PumpStatsSnapshot snapshot)
            ? snapshot
            : PumpStatsSnapshot.Unknown;

    /// <summary>
    /// <see cref="ParsePumpStats"/> ile aynı katı eşlemeyi yapar; bozuk
    /// JSON/null girdi için <c>false</c> +
    /// <see cref="PumpStatsSnapshot.Unknown"/> döner. Sıfır-sayaçlı
    /// geçerli JSON (taze motor) <c>true</c> döner.
    /// </summary>
    public static bool TryParsePumpStats(string? pumpStatsJson, out PumpStatsSnapshot snapshot)
    {
        snapshot = PumpStatsSnapshot.Unknown;
        if (string.IsNullOrWhiteSpace(pumpStatsJson))
            return false;
        try
        {
            using var document = JsonDocument.Parse(pumpStatsJson);
            JsonElement root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return false;
            snapshot = new PumpStatsSnapshot(
                ReadNonNegative(root, "count"),
                ReadNonNegative(root, "last_us"),
                ReadNonNegative(root, "avg_us"),
                ReadNonNegative(root, "max_us"));
            return true;
        }
        catch (Exception)
        {
            snapshot = PumpStatsSnapshot.Unknown;
            return false;
        }
    }

    /// <summary>
    /// Verilen okuyucudan pump snapshot'ı alır (polling-free: çağrıcı ne
    /// zaman isterse çağırır). Null delege/throw/boş JSON fail-closed
    /// <see cref="PumpStatsSnapshot.Unknown"/> döner.
    /// </summary>
    public static PumpStatsSnapshot ReadPumpSnapshot(Func<string?>? readJson)
    {
        if (readJson is null)
            return PumpStatsSnapshot.Unknown;
        try
        {
            return ParsePumpStats(readJson());
        }
        catch (Exception)
        {
            return PumpStatsSnapshot.Unknown;
        }
    }

    /// <summary>
    /// Ham <c>RowlEngine_GetBgmPumpAvgMicroseconds</c> çıktısını karara
    /// çevirir. Null delege ve throw fail-closed 0 döner.
    /// </summary>
    public static ulong ReadPumpAvgMicroseconds(Func<ulong>? readRaw)
    {
        if (readRaw is null)
            return 0;
        try
        {
            return readRaw();
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>Rozet metni; tek kaynak <see cref="MixerBadgeService"/>.</summary>
    public static string ReadPumpBadgeText(Func<string?>? readJson)
    {
        if (readJson is null)
            return string.Empty;
        try
        {
            return MixerBadgeService.Describe(readJson()).Text;
        }
        catch (Exception)
        {
            return string.Empty;
        }
    }

    private static long ReadNonNegative(JsonElement root, string property)
    {
        if (!root.TryGetProperty(property, out JsonElement element))
            throw new JsonException($"Missing pump field '{property}'.");
        if (element.ValueKind != JsonValueKind.Number || !element.TryGetInt64(out long value))
            throw new JsonException($"Pump field '{property}' is not an integer.");
        if (value < 0)
            throw new JsonException($"Pump field '{property}' is negative.");
        return value;
    }
}
