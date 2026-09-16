using System;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 1 — <see cref="AudioStreamingService"/> dilim testleri:
/// StreamInfo parse, eşik-karar tutarlılığı, null-handle davranışı.
/// Headless, native çağrı YOKTUR (JSON fixture + delege enjeksiyonu).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAudioStreamingServiceTests
{
    private const string StreamJson =
        "{\"mode\":\"stream\",\"duration_seconds\":594.43," +
        "\"threshold_seconds\":380.43573696145125," +
        "\"threshold_bytes\":67108864,\"buffered_seconds\":1.37," +
        "\"reason\":\"over_threshold\",\"channel\":0," +
        "\"asset\":\"audio/boss_theme.ogg\"}";

    private const string MemoryJson =
        "{\"mode\":\"memory\",\"duration_seconds\":29.0," +
        "\"threshold_seconds\":30.0,\"threshold_bytes\":67108864," +
        "\"buffered_seconds\":0.0,\"reason\":\"under_threshold\"," +
        "\"channel\":0,\"asset\":\"audio/short_tone.ogg\"}";

    [Fact]
    public void Parse_StreamJson_RoundTripsAllKeys()
    {
        var snapshot = AudioStreamingService.Parse(StreamJson);
        Assert.Equal("stream", snapshot.Mode);
        Assert.Equal(594.43, snapshot.DurationSeconds, precision: 5);
        Assert.Equal(380.43573696145125, snapshot.ThresholdSeconds, precision: 5);
        Assert.Equal(67108864L, snapshot.ThresholdBytes);
        Assert.Equal(1.37, snapshot.BufferedSeconds, precision: 5);
        Assert.Equal("over_threshold", snapshot.Reason);
        Assert.Equal(0, snapshot.Channel);
        Assert.Equal("audio/boss_theme.ogg", snapshot.Asset);
        Assert.True(snapshot.IsStream);
        Assert.True(AudioStreamingService.IsDecisionConsistent(snapshot));
    }

    [Fact]
    public void Parse_MemoryJson_NeverStreams()
    {
        var snapshot = AudioStreamingService.Parse(MemoryJson);
        Assert.Equal("memory", snapshot.Mode);
        Assert.False(snapshot.IsStream);
        Assert.True(AudioStreamingService.IsDecisionConsistent(snapshot));
    }

    [Fact]
    public void Parse_BrokenInput_FailsClosedUnknown()
    {
        foreach (string? broken in new string?[] { null, "", "   ", "{ not json", "[1,2]", "\"tape\"" })
        {
            var snapshot = AudioStreamingService.Parse(broken);
            Assert.Equal("unknown", snapshot.Mode);
            Assert.False(snapshot.IsStream);
        }

        var weirdMode = AudioStreamingService.Parse("{\"mode\":\"tape\"}");
        Assert.Equal("unknown", weirdMode.Mode);
        Assert.False(weirdMode.IsStream);
    }

    [Fact]
    public void Parse_NullNonFinite_MapsToNaN()
    {
        const string nullWire =
            "{\"mode\":\"stream\",\"duration_seconds\":null," +
            "\"threshold_seconds\":null,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":null,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/boss_theme.ogg\"}";
        var snapshot = AudioStreamingService.Parse(nullWire);
        Assert.True(double.IsNaN(snapshot.DurationSeconds));
        Assert.True(double.IsNaN(snapshot.ThresholdSeconds));
        Assert.True(double.IsNaN(snapshot.BufferedSeconds));
        // Non-finite wire değerleri asla stream değildir (strict > finite ister).
        Assert.False(snapshot.IsStream);
        Assert.False(AudioStreamingService.IsDecisionConsistent(snapshot));

        // TryParse aynı null-wire eşlemesini yapar.
        Assert.True(AudioStreamingService.TryParse(nullWire, out var reparsed));
        Assert.True(double.IsNaN(reparsed.DurationSeconds));
        Assert.True(double.IsNaN(reparsed.ThresholdSeconds));
        Assert.True(double.IsNaN(reparsed.BufferedSeconds));
        Assert.False(reparsed.IsStream);

        Assert.False(AudioStreamingService.TryParse(null, out var broken));
        Assert.Equal(StreamInfoSnapshot.Unknown, broken);
        Assert.False(AudioStreamingService.TryParse("{ not json", out _));
    }

    [Fact]
    public void StreamInfo_UnknownHeader_NeverStreams()
    {
        var snapshot = AudioStreamingService.Parse(
            "{\"mode\":\"unknown\",\"duration_seconds\":-1," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"unknown_header\"," +
            "\"channel\":0,\"asset\":\"audio/corrupt.ogg\"}");
        Assert.Equal("unknown", snapshot.Mode);
        Assert.Equal(-1.0, snapshot.DurationSeconds);
        Assert.False(snapshot.IsStream);
        Assert.True(AudioStreamingService.IsDecisionConsistent(snapshot));
    }

    [Fact]
    public void Decision_ThresholdEdges_StrictGreaterThan()
    {
        // 30.0/30.0 silent MEMORY: eşitlik stream değildir.
        var atEdge = AudioStreamingService.Parse(
            "{\"mode\":\"memory\",\"duration_seconds\":30.0," +
            "\"threshold_seconds\":30.0,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"under_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/edge.ogg\"}");
        Assert.False(atEdge.IsStream);

        // Eşik+1ms stream.
        var justOver = AudioStreamingService.Parse(
            "{\"mode\":\"stream\",\"duration_seconds\":30.001," +
            "\"threshold_seconds\":30.0,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/edge.ogg\"}");
        Assert.True(justOver.IsStream);

        // -1 duration (header tanınmadı) fail-closed: asla stream değil.
        var unknownHeader = AudioStreamingService.Parse(
            "{\"mode\":\"unknown\",\"duration_seconds\":-1," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"unknown_header\"," +
            "\"channel\":0,\"asset\":\"audio/corrupt.ogg\"}");
        Assert.False(unknownHeader.IsStream);
        Assert.True(AudioStreamingService.IsDecisionConsistent(unknownHeader));
    }

    [Fact]
    public void Decision_InconsistentClaim_Detected()
    {
        // mode=stream ama duration eşik altı: tutarsız (native bunu üretmez).
        var inconsistent = AudioStreamingService.Parse(
            "{\"mode\":\"stream\",\"duration_seconds\":1.0," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/short.ogg\"}");
        Assert.False(inconsistent.IsStream);
        Assert.False(AudioStreamingService.IsDecisionConsistent(inconsistent));
    }

    [Fact]
    public void Query_NullHandle_FailsClosedWithoutThrow()
    {
        Assert.False(AudioStreamingService.ReadIsStreaming(null));
        Assert.Equal(StreamInfoSnapshot.Unknown, AudioStreamingService.ReadSnapshot(null));
        Assert.Equal(string.Empty, AudioStreamingService.ReadBadgeText(null));

        Assert.False(AudioStreamingService.ReadIsStreaming(() => throw new InvalidOperationException("dead")));
        Assert.Equal(StreamInfoSnapshot.Unknown,
            AudioStreamingService.ReadSnapshot(() => throw new InvalidOperationException("dead")));
        Assert.Equal(string.Empty,
            AudioStreamingService.ReadBadgeText(() => throw new InvalidOperationException("dead")));

        var empty = AudioStreamingService.ReadSnapshot(() => string.Empty);
        Assert.Equal("unknown", empty.Mode);
        Assert.False(empty.IsStream);
    }

    [Fact]
    public void Query_LiveDelegates_MirrorNativeDecision()
    {
        Assert.True(AudioStreamingService.ReadIsStreaming(() => 1));
        Assert.False(AudioStreamingService.ReadIsStreaming(() => 0));

        var snapshot = AudioStreamingService.ReadSnapshot(() => StreamJson);
        Assert.Equal("stream", snapshot.Mode);
        Assert.True(snapshot.IsStream);

        string badge = AudioStreamingService.ReadBadgeText(() => StreamJson);
        Assert.Contains("STREAM", badge);
        Assert.Equal(string.Empty, AudioStreamingService.ReadBadgeText(() => MemoryJson));
    }

    [Fact]
    public void Volume_AcceptVolume_ClampsAndIgnoresNonFinite()
    {
        Assert.Equal(0.0f, AudioStreamingService.AcceptVolume(0.5f, -2.0f));
        Assert.Equal(1.0f, AudioStreamingService.AcceptVolume(0.5f, 7.0f));
        Assert.Equal(0.5f, AudioStreamingService.AcceptVolume(0.5f, float.NaN));
        Assert.Equal(0.5f, AudioStreamingService.AcceptVolume(0.5f, float.PositiveInfinity));
        Assert.Equal(0.25f, AudioStreamingService.AcceptVolume(0.5f, 0.25f), precision: 5);
    }

    [Fact]
    public void ChannelMap_UnknownFallsBackToSfx()
    {
        Assert.Equal("Bgm", AudioStreamingService.ChannelName(0));
        Assert.Equal("Voice", AudioStreamingService.ChannelName(1));
        Assert.Equal("Sfx", AudioStreamingService.ChannelName(2));
        Assert.Equal("Ambience", AudioStreamingService.ChannelName(3));
        Assert.Equal("Ui", AudioStreamingService.ChannelName(4));
        Assert.Equal("Sfx", AudioStreamingService.ChannelName(99));
        Assert.Equal("Sfx", AudioStreamingService.ChannelName(-1));
    }
}
