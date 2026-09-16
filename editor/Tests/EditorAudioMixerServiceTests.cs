using System;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 2 — <see cref="AudioMixerService"/> dilim testleri:
/// kazanç tutarlılığı, eğri parse, havuz derinliği, bed hacmi, pump
/// snapshot ve null-handle davranışı. Headless, native çağrı YOKTUR
/// (JSON fixture + delege enjeksiyonu).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAudioMixerServiceTests
{
    private const string PumpJson =
        "{\"count\":64,\"last_us\":12,\"avg_us\":11,\"max_us\":40}";

    // ── Mixer kazanç tutarlılığı (gain == master*bus, duck yalnız BGM) ──

    [Fact]
    public void Gain_Consistent_WithoutDuck_AllSixBuses()
    {
        const float master = 0.8f;
        Assert.True(AudioMixerService.IsGainConsistent("master", master, 1.0f, false, master));
        Assert.True(AudioMixerService.IsGainConsistent("bgm", master, 0.9f, false, master * 0.9f));
        Assert.True(AudioMixerService.IsGainConsistent("voice", master, 0.7f, false, master * 0.7f));
        Assert.True(AudioMixerService.IsGainConsistent("sfx", master, 0.6f, false, master * 0.6f));
        Assert.True(AudioMixerService.IsGainConsistent("ambience", master, 0.5f, false, master * 0.5f));
        Assert.True(AudioMixerService.IsGainConsistent("ui", master, 1.0f, false, master));
    }

    [Fact]
    public void Gain_DuckAppliesOnlyToBgm()
    {
        const float master = 0.8f;
        const float bgm = 0.9f;
        float ducked = master * bgm * AudioMixerService.DuckingFactor;

        // Duck'lı BGM: duck'lı beklenti tutarlı, ducksız tutarsız.
        Assert.True(AudioMixerService.IsGainConsistent("bgm", master, bgm, true, ducked));
        Assert.False(AudioMixerService.IsGainConsistent("bgm", master, bgm, true, master * bgm));

        // Duck'sız BGM: ducksız beklenti tutarlı, duck'lı tutarsız.
        Assert.True(AudioMixerService.IsGainConsistent("BGM", master, bgm, false, master * bgm));
        Assert.False(AudioMixerService.IsGainConsistent("bgm", master, bgm, false, ducked));

        // Diğer bus'lar duck bayrağını YOK sayar (duck yalnız BGM).
        const float sfx = 0.6f;
        Assert.True(AudioMixerService.IsGainConsistent("sfx", master, sfx, true, master * sfx));
        Assert.False(AudioMixerService.IsGainConsistent("sfx", master, sfx, true, master * sfx * AudioMixerService.DuckingFactor));
        Assert.True(AudioMixerService.IsGainConsistent("voice", master, sfx, true, master * sfx));
        Assert.True(AudioMixerService.IsGainConsistent("ambience", master, sfx, true, master * sfx));
        Assert.True(AudioMixerService.IsGainConsistent("ui", master, sfx, true, master * sfx));
    }

    [Fact]
    public void Gain_NonFiniteOrNegativeTolerance_IsInconsistent()
    {
        Assert.False(AudioMixerService.IsGainConsistent("bgm", 0.8f, 0.9f, false, float.NaN));
        Assert.False(AudioMixerService.IsGainConsistent("bgm", 0.8f, 0.9f, false, float.PositiveInfinity));
        Assert.False(AudioMixerService.IsGainConsistent("bgm", 0.8f, 0.9f, false, 0.72f, -1.0f));
        Assert.Equal(0.5f, AudioMixerService.DuckingFactor);
    }

    // ── Fade eğrisi (bozuk girdi → Linear fail-closed) ──

    [Fact]
    public void Curve_Parse_ValidForms()
    {
        Assert.Equal(MixerFadeCurve.Linear, AudioMixerService.ParseFadeCurve("Linear"));
        Assert.Equal(MixerFadeCurve.Linear, AudioMixerService.ParseFadeCurve("linear"));
        Assert.Equal(MixerFadeCurve.Linear, AudioMixerService.ParseFadeCurve("0"));
        Assert.Equal(MixerFadeCurve.EqualPower, AudioMixerService.ParseFadeCurve("EqualPower"));
        Assert.Equal(MixerFadeCurve.EqualPower, AudioMixerService.ParseFadeCurve(" equalpower "));
        Assert.Equal(MixerFadeCurve.EqualPower, AudioMixerService.ParseFadeCurve("equal_power"));
        Assert.Equal(MixerFadeCurve.EqualPower, AudioMixerService.ParseFadeCurve("EQUAL-POWER"));
        Assert.Equal(MixerFadeCurve.EqualPower, AudioMixerService.ParseFadeCurve("1"));
    }

    [Fact]
    public void Curve_Parse_BrokenInput_FailsClosedLinear()
    {
        foreach (string? broken in new string?[] { null, "", "   ", "tape", "2", "-1", "quad", "linearx" })
            Assert.Equal(MixerFadeCurve.Linear, AudioMixerService.ParseFadeCurve(broken));

        // Strict varsayılan Linear'dır (EqualPower asla varsayılan değil).
        Assert.Equal("Linear", AudioMixerService.FadeCurveName(99));
        Assert.Equal("Linear", AudioMixerService.FadeCurveName(-1));
        Assert.Equal("Linear", AudioMixerService.FadeCurveName(0));
        Assert.Equal("EqualPower", AudioMixerService.FadeCurveName(1));
    }

    [Fact]
    public void Curve_Read_NullHandle_FailsClosedWithoutThrow()
    {
        Assert.Equal(0, AudioMixerService.ReadFadeCurve(null));
        Assert.Equal(0, AudioMixerService.ReadFadeCurve(() => throw new InvalidOperationException("dead")));
        Assert.Equal(1, AudioMixerService.ReadFadeCurve(() => 1));
        Assert.Equal(0, AudioMixerService.ReadFadeCurve(() => 0));
        Assert.Equal(0, AudioMixerService.ReadFadeCurve(() => 7));
    }

    // ── SFX polyphony derinliği ([1,16] clamp + non-finite ignore) ──

    [Fact]
    public void Depth_Clamp_Bounds()
    {
        Assert.Equal(8, AudioMixerService.DefaultPoolDepth);
        Assert.Equal(1, AudioMixerService.ClampPoolDepth(0));
        Assert.Equal(1, AudioMixerService.ClampPoolDepth(-50));
        Assert.Equal(1, AudioMixerService.ClampPoolDepth(1));
        Assert.Equal(8, AudioMixerService.ClampPoolDepth(8));
        Assert.Equal(16, AudioMixerService.ClampPoolDepth(16));
        Assert.Equal(16, AudioMixerService.ClampPoolDepth(17));
        Assert.Equal(16, AudioMixerService.ClampPoolDepth(int.MaxValue));
    }

    [Fact]
    public void Depth_Accept_NonFiniteIgnored()
    {
        Assert.Equal(12, AudioMixerService.AcceptPoolDepth(8, 12.0));
        Assert.Equal(16, AudioMixerService.AcceptPoolDepth(8, 99.0));
        Assert.Equal(1, AudioMixerService.AcceptPoolDepth(8, -3.0));
        Assert.Equal(8, AudioMixerService.AcceptPoolDepth(8, 8.9));
        Assert.Equal(8, AudioMixerService.AcceptPoolDepth(8, double.NaN));
        Assert.Equal(8, AudioMixerService.AcceptPoolDepth(8, double.PositiveInfinity));
        Assert.Equal(8, AudioMixerService.AcceptPoolDepth(8, double.NegativeInfinity));
    }

    [Fact]
    public void Depth_Read_NullHandle_FailsClosedWithoutThrow()
    {
        Assert.Equal(AudioMixerService.DefaultPoolDepth, AudioMixerService.ReadPoolDepth(null));
        Assert.Equal(AudioMixerService.DefaultPoolDepth,
            AudioMixerService.ReadPoolDepth(() => throw new InvalidOperationException("dead")));
        Assert.Equal(5, AudioMixerService.ReadPoolDepth(() => 5));
        Assert.Equal(1, AudioMixerService.ReadPoolDepth(() => 0));
        Assert.Equal(16, AudioMixerService.ReadPoolDepth(() => 99));

        Assert.Equal(0, AudioMixerService.ReadActiveVoices(null));
        Assert.Equal(0, AudioMixerService.ReadActiveVoices(() => throw new InvalidOperationException("dead")));
        Assert.Equal(3, AudioMixerService.ReadActiveVoices(() => 3));
        Assert.Equal(0, AudioMixerService.ReadActiveVoices(() => -2));
    }

    // ── Ambience bed hacmi + yatak geçerliliği ──

    [Fact]
    public void BedVolume_Accept_ClampsAndIgnoresNonFinite()
    {
        Assert.Equal(0.3f, AudioMixerService.AcceptBedVolume(0.5f, 0.3f), precision: 5);
        Assert.Equal(0.0f, AudioMixerService.AcceptBedVolume(0.5f, -2.0f));
        Assert.Equal(1.0f, AudioMixerService.AcceptBedVolume(0.5f, 7.0f));
        Assert.Equal(0.5f, AudioMixerService.AcceptBedVolume(0.5f, float.NaN));
        Assert.Equal(0.5f, AudioMixerService.AcceptBedVolume(0.5f, float.PositiveInfinity));

        Assert.True(AudioMixerService.IsValidAmbienceBed(0));
        Assert.True(AudioMixerService.IsValidAmbienceBed(1));
        Assert.False(AudioMixerService.IsValidAmbienceBed(-1));
        Assert.False(AudioMixerService.IsValidAmbienceBed(2));
        Assert.False(AudioMixerService.IsValidAmbienceBed(99));
    }

    [Fact]
    public void Bed_Read_NullHandle_FailsClosedWithoutThrow()
    {
        Assert.Equal(0.0f, AudioMixerService.ReadBedVolume(null));
        Assert.Equal(0.25f, AudioMixerService.ReadBedVolume(null, 0.25f));
        Assert.Equal(0.0f, AudioMixerService.ReadBedVolume(() => throw new InvalidOperationException("dead")));
        Assert.Equal(0.7f, AudioMixerService.ReadBedVolume(() => 0.7f), precision: 5);
        Assert.Equal(0.0f, AudioMixerService.ReadBedVolume(() => float.NaN));

        Assert.False(AudioMixerService.ReadIsBedPlaying(null));
        Assert.False(AudioMixerService.ReadIsBedPlaying(() => throw new InvalidOperationException("dead")));
        Assert.True(AudioMixerService.ReadIsBedPlaying(() => 1));
        Assert.False(AudioMixerService.ReadIsBedPlaying(() => 0));

        Assert.False(AudioMixerService.ReadIsCrossfadeActive(null));
        Assert.False(AudioMixerService.ReadIsCrossfadeActive(() => throw new InvalidOperationException("dead")));
        Assert.True(AudioMixerService.ReadIsCrossfadeActive(() => 1));
        Assert.False(AudioMixerService.ReadIsCrossfadeActive(() => 0));
    }

    // ── Pump maliyeti (non-negatif, eksik alan → fail-closed default) ──

    [Fact]
    public void Pump_Parse_ValidJson_RoundTripsAllKeys()
    {
        var snapshot = AudioMixerService.ParsePumpStats(PumpJson);
        Assert.Equal(64L, snapshot.Count);
        Assert.Equal(12L, snapshot.LastUs);
        Assert.Equal(11L, snapshot.AvgUs);
        Assert.Equal(40L, snapshot.MaxUs);

        Assert.True(AudioMixerService.TryParsePumpStats(PumpJson, out var reparsed));
        Assert.Equal(snapshot, reparsed);

        // Sıfır-sayaçlı geçerli JSON (taze motor) true döner.
        Assert.True(AudioMixerService.TryParsePumpStats(
            "{\"count\":0,\"last_us\":0,\"avg_us\":0,\"max_us\":0}", out var fresh));
        Assert.Equal(PumpStatsSnapshot.Unknown, fresh);
    }

    [Fact]
    public void Pump_Parse_MissingOrBadField_FailsClosedDefault()
    {
        // Eksik alan → default.
        Assert.Equal(PumpStatsSnapshot.Unknown,
            AudioMixerService.ParsePumpStats("{\"count\":64,\"last_us\":12,\"avg_us\":11}"));
        // Negatif → default.
        Assert.Equal(PumpStatsSnapshot.Unknown,
            AudioMixerService.ParsePumpStats("{\"count\":-1,\"last_us\":12,\"avg_us\":11,\"max_us\":40}"));
        // Kesirli → default.
        Assert.Equal(PumpStatsSnapshot.Unknown,
            AudioMixerService.ParsePumpStats("{\"count\":64.5,\"last_us\":12,\"avg_us\":11,\"max_us\":40}"));
        // Yanlış tip → default.
        Assert.Equal(PumpStatsSnapshot.Unknown,
            AudioMixerService.ParsePumpStats("{\"count\":\"64\",\"last_us\":12,\"avg_us\":11,\"max_us\":40}"));
        // Kök obje değil → default.
        Assert.Equal(PumpStatsSnapshot.Unknown, AudioMixerService.ParsePumpStats("[1,2]"));
        Assert.Equal(PumpStatsSnapshot.Unknown, AudioMixerService.ParsePumpStats("\"pump\""));

        Assert.False(AudioMixerService.TryParsePumpStats(
            "{\"count\":64,\"last_us\":12,\"avg_us\":11}", out var missing));
        Assert.Equal(PumpStatsSnapshot.Unknown, missing);
        Assert.False(AudioMixerService.TryParsePumpStats(null, out var broken));
        Assert.Equal(PumpStatsSnapshot.Unknown, broken);
        Assert.False(AudioMixerService.TryParsePumpStats("{ not json", out _));
    }

    [Fact]
    public void Pump_Parse_BrokenInput_FailsClosedUnknown()
    {
        foreach (string? broken in new string?[] { null, "", "   ", "{ not json", "[1,2]", "\"pump\"" })
            Assert.Equal(PumpStatsSnapshot.Unknown, AudioMixerService.ParsePumpStats(broken));
    }

    [Fact]
    public void Pump_Read_NullHandle_FailsClosedWithoutThrow()
    {
        Assert.Equal(PumpStatsSnapshot.Unknown, AudioMixerService.ReadPumpSnapshot(null));
        Assert.Equal(PumpStatsSnapshot.Unknown,
            AudioMixerService.ReadPumpSnapshot(() => throw new InvalidOperationException("dead")));
        Assert.Equal(PumpStatsSnapshot.Unknown, AudioMixerService.ReadPumpSnapshot(() => string.Empty));

        var live = AudioMixerService.ReadPumpSnapshot(() => PumpJson);
        Assert.Equal(64L, live.Count);

        Assert.Equal(0UL, AudioMixerService.ReadPumpAvgMicroseconds(null));
        Assert.Equal(0UL, AudioMixerService.ReadPumpAvgMicroseconds(() => throw new InvalidOperationException("dead")));
        Assert.Equal(42UL, AudioMixerService.ReadPumpAvgMicroseconds(() => 42UL));
    }

    // ── Rozet metni (bozuk JSON → gizli, throw yok) ──

    [Fact]
    public void BadgeText_LiveDelegates_MirrorPumpDecision()
    {
        string badge = AudioMixerService.ReadPumpBadgeText(() => PumpJson);
        Assert.Contains("PUMP", badge);

        Assert.Equal(string.Empty, AudioMixerService.ReadPumpBadgeText(null));
        Assert.Equal(string.Empty,
            AudioMixerService.ReadPumpBadgeText(() => throw new InvalidOperationException("dead")));
        Assert.Equal(string.Empty,
            AudioMixerService.ReadPumpBadgeText(() => "{ broken"));
        Assert.Equal(string.Empty,
            AudioMixerService.ReadPumpBadgeText(
                () => "{\"count\":0,\"last_us\":0,\"avg_us\":0,\"max_us\":0}"));
    }
}
