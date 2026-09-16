using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 1 — audio streaming editör dilimi: rozet servisi, BGM
/// linter kuralı ve Inspector rozet props'ları. Headless, native çağrı
/// YOKTUR (sparse dosya + JSON fixture'ları).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAudioStreamingSliceTests
{
    private static string MakeTempAssets()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlStream_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(root, "Assets");
        foreach (string sub in new[] { "", "audio" })
            Directory.CreateDirectory(Path.Combine(assets, sub));
        return assets;
    }

    private static void DeleteTempAssets(string assets)
    {
        try { Directory.Delete(Path.GetDirectoryName(assets)!, recursive: true); }
        catch (Exception) { }
    }

    private static void WriteSparse(string path, long bytes)
    {
        using var stream = File.Create(path);
        stream.SetLength(bytes);
    }

    private static NodeViewModel AudioNode(ulong id, string bgmTrack)
    {
        var node = new NodeViewModel(id, "Audio " + id, 0, 0, bare: true);
        var audio = node.AddComponent<AudioComponentViewModel>();
        audio.BgmTrack = bgmTrack;
        return node;
    }

    [Fact]
    public void BadgeService_StreamJson_IsVisibleWithDetail()
    {
        var description = AudioStreamingBadgeService.Describe(
            "{\"mode\":\"stream\",\"duration_seconds\":22675.7," +
            "\"threshold_seconds\":380.43573696145125," +
            "\"threshold_bytes\":67108864,\"buffered_seconds\":0.5," +
            "\"reason\":\"over_threshold\",\"channel\":0," +
            "\"asset\":\"audio/long_theme.ogg\"}");
        Assert.True(description.Visible);
        Assert.Equal("stream", description.Mode);
        Assert.Contains("STREAM", description.Text);
    }

    [Fact]
    public void BadgeService_MemoryJson_StaysCollapsed()
    {
        var description = AudioStreamingBadgeService.Describe(
            "{\"mode\":\"memory\",\"duration_seconds\":1.0," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"under_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/short_tone.ogg\"}");
        Assert.False(description.Visible);
        Assert.Equal("memory", description.Mode);
        Assert.Equal(string.Empty, description.Text);
    }

    [Fact]
    public void BadgeService_BrokenInput_FailsClosed()
    {
        Assert.Equal("unknown", AudioStreamingBadgeService.Describe(null).Mode);
        Assert.False(AudioStreamingBadgeService.Describe(null).Visible);
        Assert.False(AudioStreamingBadgeService.Describe("").Visible);
        var garbage = AudioStreamingBadgeService.Describe("{ not json");
        Assert.False(garbage.Visible);
        Assert.Equal("unknown", garbage.Mode);
        var weirdMode = AudioStreamingBadgeService.Describe("{\"mode\":\"tape\"}");
        Assert.False(weirdMode.Visible);
        Assert.Equal("unknown", weirdMode.Mode);
    }

    [Fact]
    public void BadgeService_InconsistentStreamClaim_StaysCollapsed()
    {
        // mode=stream ama duration null: IsStream=false → rozet gizli.
        const string nullDuration =
            "{\"mode\":\"stream\",\"duration_seconds\":null," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/long_theme.ogg\"}";
        Assert.False(AudioStreamingService.Parse(nullDuration).IsStream);
        var nullDescription = AudioStreamingBadgeService.Describe(nullDuration);
        Assert.False(nullDescription.Visible);
        Assert.Equal(string.Empty, nullDescription.Text);

        // mode=stream ama duration eşik altı: IsStream=false → rozet gizli.
        const string underThreshold =
            "{\"mode\":\"stream\",\"duration_seconds\":1.0," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.0,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/short.ogg\"}";
        Assert.False(AudioStreamingService.Parse(underThreshold).IsStream);
        Assert.False(AudioStreamingBadgeService.Describe(underThreshold).Visible);

        // mode=stream ama reason memory: IsStream=false → rozet gizli.
        const string wrongReason =
            "{\"mode\":\"stream\",\"duration_seconds\":22675.7," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.5,\"reason\":\"under_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/long_theme.ogg\"}";
        Assert.False(AudioStreamingService.Parse(wrongReason).IsStream);
        Assert.False(AudioStreamingBadgeService.Describe(wrongReason).Visible);

        // Tutarlı stream iddiası hâlâ görünür (IsStream ile aynı karar).
        const string consistent =
            "{\"mode\":\"stream\",\"duration_seconds\":22675.7," +
            "\"threshold_seconds\":380.43,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.5,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/long_theme.ogg\"}";
        Assert.True(AudioStreamingService.Parse(consistent).IsStream);
        Assert.True(AudioStreamingBadgeService.Describe(consistent).Visible);
    }

    [Fact]
    public void Lint_LongAudio_OverBudgetOgg_WarnsStreaming()
    {
        string assets = MakeTempAssets();
        try
        {
            WriteSparse(Path.Combine(assets, "audio", "long_theme.ogg"), 65L * 1024 * 1024);
            WriteSparse(Path.Combine(assets, "audio", "short_tone.ogg"), 1024);
            var nodes = new List<NodeViewModel>
            {
                AudioNode(1, "audio/long_theme.ogg"),
                AudioNode(2, "audio/short_tone.ogg"),
            };
            var issues = ProjectLintService.Lint(
                nodes, new List<ConnectionViewModel>(), assets);
            var streaming = issues.Where(i => i.Message.Contains("64 MiB")).ToList();
            Assert.Single(streaming);
            Assert.False(streaming[0].IsError);
            Assert.Equal(1UL, streaming[0].NodeId);
            Assert.Contains("streaming path", streaming[0].Message);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_LongAudio_OverBudgetWav_PointsAtRamFallback()
    {
        string assets = MakeTempAssets();
        try
        {
            WriteSparse(Path.Combine(assets, "audio", "huge_claim.wav"), 100L * 1024 * 1024);
            var nodes = new List<NodeViewModel> { AudioNode(1, "huge_claim.wav") };
            var issues = ProjectLintService.Lint(
                nodes, new List<ConnectionViewModel>(), assets);
            var streaming = issues.Where(i => i.Message.Contains("64 MiB")).ToList();
            Assert.Single(streaming);
            Assert.Contains("RAM decode path", streaming[0].Message);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_LongAudio_MissingFileAndDisabledOption_StaySilent()
    {
        string assets = MakeTempAssets();
        try
        {
            var nodes = new List<NodeViewModel> { AudioNode(1, "audio/ghost.ogg") };
            var missing = ProjectLintService.Lint(
                nodes, new List<ConnectionViewModel>(), assets);
            Assert.DoesNotContain(missing, i => i.Message.Contains("64 MiB"));

            WriteSparse(Path.Combine(assets, "audio", "long_theme.ogg"), 65L * 1024 * 1024);
            var nodesPresent = new List<NodeViewModel> { AudioNode(1, "audio/long_theme.ogg") };
            var disabled = ProjectLintService.Lint(
                nodesPresent, new List<ConnectionViewModel>(), assets,
                lintOptions: new ProjectLintOptions(CheckLongAudio: false));
            Assert.DoesNotContain(disabled, i => i.Message.Contains("64 MiB"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void ViewModel_UpdateStreamingBadge_MirrorsService()
    {
        var audio = new AudioComponentViewModel();
        Assert.Equal("unknown", audio.StreamMode);
        Assert.False(audio.IsStreamBadgeVisible);

        audio.UpdateStreamingBadge("{\"mode\":\"stream\",\"duration_seconds\":100.0," +
            "\"threshold_seconds\":10.0,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.5,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/long_theme.ogg\"}");
        Assert.Equal("stream", audio.StreamMode);
        Assert.True(audio.IsStreamBadgeVisible);
        Assert.Contains("STREAM", audio.StreamBadgeText);

        audio.UpdateStreamingBadge("{ broken");
        Assert.Equal("unknown", audio.StreamMode);
        Assert.False(audio.IsStreamBadgeVisible);
        Assert.Equal(string.Empty, audio.StreamBadgeText);
    }

    [Fact]
    public void ViewModel_InconsistentStreamClaim_StaysCollapsed()
    {
        var audio = new AudioComponentViewModel();

        // mode=stream + null süre: IsStream false, rozet gizli, metin boş.
        audio.UpdateStreamingBadge("{\"mode\":\"stream\",\"duration_seconds\":null," +
            "\"threshold_seconds\":10.0,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.5,\"reason\":\"over_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/long_theme.ogg\"}");
        Assert.False(audio.IsStreamBadgeVisible);
        Assert.Equal(string.Empty, audio.StreamBadgeText);

        // mode=stream + eşik-altı süre: yine gizli.
        audio.UpdateStreamingBadge("{\"mode\":\"stream\",\"duration_seconds\":5.0," +
            "\"threshold_seconds\":10.0,\"threshold_bytes\":67108864," +
            "\"buffered_seconds\":0.5,\"reason\":\"under_threshold\"," +
            "\"channel\":0,\"asset\":\"audio/short.ogg\"}");
        Assert.False(audio.IsStreamBadgeVisible);
        Assert.Equal(string.Empty, audio.StreamBadgeText);
    }

    [Fact]
    public void LintOptions_CheckLongAudio_DefaultsToTrue()
    {
        Assert.True(new ProjectLintOptions().CheckLongAudio);
    }
}
