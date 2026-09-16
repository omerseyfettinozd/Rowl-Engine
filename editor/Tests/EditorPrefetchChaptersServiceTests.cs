using System;
using System.Collections.Generic;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 4 — prefetch/chapter saf katmanı: bütçe clamp aynası,
/// ilerleme JSON parse (hazır/eksik/byte, bozuk → fail-closed default),
/// komşuluk kararı (aktif±1 + prefetch penceresi + tetik kararı),
/// chapter-id validate, null-delege/throw fail-closed, Describe (bozuk →
/// gizli) ve paket referans kümesi. Headless, native çağrı YOKTUR.
/// </summary>
public sealed class EditorPrefetchChaptersServiceTests
{
    private const string ProgressJson =
        "{\"total_assets\":4,\"ready_assets\":3,\"missing_assets\":1," +
        "\"queued_assets\":0,\"ready_bytes\":1792,\"budget_bytes\":33554432," +
        "\"complete\":true,\"missing_paths\":[\"voice.ogg\"],\"last_diagnostic\":\"\"}";

    private const string IncompleteProgressJson =
        "{\"total_assets\":4,\"ready_assets\":2,\"missing_assets\":0," +
        "\"queued_assets\":2,\"ready_bytes\":512,\"budget_bytes\":33554432," +
        "\"complete\":false,\"missing_paths\":[],\"last_diagnostic\":\"deferred\"}";

    private static readonly IReadOnlyList<string> FiveChapters =
        new[] { "ch1", "ch2", "ch3", "ch4", "ch5" };

    // ── Bütçe clamp ──

    [Fact]
    public void ClampBudget_Zero_SelectsDefault()
    {
        Assert.Equal(
            PrefetchChaptersService.DefaultBudgetBytes,
            PrefetchChaptersService.ClampBudgetBytes(0));
        Assert.Equal(32UL * 1024 * 1024, PrefetchChaptersService.DefaultBudgetBytes);
    }

    [Fact]
    public void ClampBudget_SmallValue_PassesThrough()
    {
        Assert.Equal(250UL, PrefetchChaptersService.ClampBudgetBytes(250));
        Assert.Equal(
            PrefetchChaptersService.MaxBudgetBytes,
            PrefetchChaptersService.ClampBudgetBytes(PrefetchChaptersService.MaxBudgetBytes));
    }

    [Fact]
    public void ClampBudget_Oversized_ClampsToCapNeverRejects()
    {
        Assert.Equal(128UL * 1024 * 1024, PrefetchChaptersService.MaxBudgetBytes);
        Assert.Equal(
            PrefetchChaptersService.MaxBudgetBytes,
            PrefetchChaptersService.ClampBudgetBytes(PrefetchChaptersService.MaxBudgetBytes + 1));
        Assert.Equal(
            PrefetchChaptersService.MaxBudgetBytes,
            PrefetchChaptersService.ClampBudgetBytes(1024UL * 1024 * 1024 * 1024));
    }

    // ── Pump clamp ──

    [Fact]
    public void ClampPump_NonPositiveOrNonFinite_SelectsDefault()
    {
        Assert.Equal(4.0, PrefetchChaptersService.DefaultPumpMilliseconds);
        Assert.Equal(4.0, PrefetchChaptersService.ClampPumpMilliseconds(0.0));
        Assert.Equal(4.0, PrefetchChaptersService.ClampPumpMilliseconds(-2.5));
        Assert.Equal(4.0, PrefetchChaptersService.ClampPumpMilliseconds(double.NaN));
        Assert.Equal(4.0, PrefetchChaptersService.ClampPumpMilliseconds(double.PositiveInfinity));
    }

    [Fact]
    public void ClampPump_CapsAtMax()
    {
        Assert.Equal(50.0, PrefetchChaptersService.MaxPumpMilliseconds);
        Assert.Equal(10.0, PrefetchChaptersService.ClampPumpMilliseconds(10.0));
        Assert.Equal(50.0, PrefetchChaptersService.ClampPumpMilliseconds(50.0));
        Assert.Equal(50.0, PrefetchChaptersService.ClampPumpMilliseconds(500.0));
    }

    // ── İlerleme parse ──

    [Fact]
    public void ParseProgress_ValidJson_MapsAllFields()
    {
        Assert.True(PrefetchChaptersService.TryParseProgress(ProgressJson, out var snapshot));
        Assert.Equal(4, snapshot.TotalAssets);
        Assert.Equal(3, snapshot.ReadyAssets);
        Assert.Equal(1, snapshot.MissingAssets);
        Assert.Equal(0, snapshot.QueuedAssets);
        Assert.Equal(1792, snapshot.ReadyBytes);
        Assert.Equal(33554432, snapshot.BudgetBytes);
        Assert.True(snapshot.Complete);
        Assert.Equal(new[] { "voice.ogg" }, snapshot.MissingPaths);
        Assert.Equal(string.Empty, snapshot.LastDiagnostic);
    }

    [Fact]
    public void ParseProgress_IncompleteJson_MapsCounters()
    {
        var snapshot = PrefetchChaptersService.ParseProgress(IncompleteProgressJson);
        Assert.False(snapshot.Complete);
        Assert.Equal(2, snapshot.ReadyAssets);
        Assert.Equal(2, snapshot.QueuedAssets);
        Assert.Empty(snapshot.MissingPaths);
        Assert.Equal("deferred", snapshot.LastDiagnostic);
    }

    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("{ not json")]
    [InlineData("[1,2]")]
    [InlineData("\"str\"")]
    [InlineData("{}")]
    public void ParseProgress_BrokenInput_FailsClosedToUnknown(string? json)
    {
        Assert.False(PrefetchChaptersService.TryParseProgress(json, out var snapshot));
        Assert.Equal(PrefetchProgressSnapshot.Unknown, snapshot);
        Assert.Equal(PrefetchProgressSnapshot.Unknown, PrefetchChaptersService.ParseProgress(json));
    }

    [Fact]
    public void ParseProgress_MissingField_FailsClosed()
    {
        Assert.False(PrefetchChaptersService.TryParseProgress("{\"total_assets\":4}", out _));
    }

    [Fact]
    public void ParseProgress_NegativeCounter_FailsClosed()
    {
        string json = ProgressJson.Replace("\"ready_assets\":3", "\"ready_assets\":-1");
        Assert.False(PrefetchChaptersService.TryParseProgress(json, out var snapshot));
        Assert.Equal(PrefetchProgressSnapshot.Unknown, snapshot);
    }

    [Fact]
    public void ParseProgress_FractionalCounter_FailsClosed()
    {
        string json = ProgressJson.Replace("\"ready_assets\":3", "\"ready_assets\":2.5");
        Assert.False(PrefetchChaptersService.TryParseProgress(json, out _));
    }

    [Fact]
    public void ParseProgress_WrongTypes_FailClosed()
    {
        string badComplete = ProgressJson.Replace("\"complete\":true", "\"complete\":\"yes\"");
        Assert.False(PrefetchChaptersService.TryParseProgress(badComplete, out _));
        string badPaths = ProgressJson.Replace("[\"voice.ogg\"]", "\"voice.ogg\"");
        Assert.False(PrefetchChaptersService.TryParseProgress(badPaths, out _));
        string badPathsEntry = ProgressJson.Replace("\"voice.ogg\"", "7");
        Assert.False(PrefetchChaptersService.TryParseProgress(badPathsEntry, out _));
        string badDiagnostic = ProgressJson.Replace("\"last_diagnostic\":\"\"", "\"last_diagnostic\":7");
        Assert.False(PrefetchChaptersService.TryParseProgress(badDiagnostic, out _));
    }

    // ── Komşuluk ──

    [Fact]
    public void ResidentWindow_MiddleChapter_IsActivePlusMinusOne()
    {
        Assert.Equal(
            new[] { "ch2", "ch3", "ch4" },
            PrefetchChaptersService.ComputeResidentWindow(FiveChapters, "ch3"));
    }

    [Fact]
    public void ResidentWindow_EdgeChapters_ClampAtEnds()
    {
        Assert.Equal(
            new[] { "ch1", "ch2" },
            PrefetchChaptersService.ComputeResidentWindow(FiveChapters, "ch1"));
        Assert.Equal(
            new[] { "ch4", "ch5" },
            PrefetchChaptersService.ComputeResidentWindow(FiveChapters, "ch5"));
    }

    [Fact]
    public void ResidentWindow_SingleChapter_IsItself()
    {
        Assert.Equal(
            new[] { "solo" },
            PrefetchChaptersService.ComputeResidentWindow(new[] { "solo" }, "solo"));
    }

    [Fact]
    public void ResidentWindow_UnknownOrEmpty_FailsClosedEmpty()
    {
        Assert.Empty(PrefetchChaptersService.ComputeResidentWindow(FiveChapters, "ch9"));
        Assert.Empty(PrefetchChaptersService.ComputeResidentWindow(FiveChapters, null));
        Assert.Empty(PrefetchChaptersService.ComputeResidentWindow(FiveChapters, string.Empty));
        Assert.Empty(PrefetchChaptersService.ComputeResidentWindow(null, "ch1"));
        Assert.Empty(PrefetchChaptersService.ComputeResidentWindow(
            Array.Empty<string>(), "ch1"));
    }

    [Fact]
    public void PrefetchWindow_IsRequestedPlusSuccessor()
    {
        Assert.Equal(
            new[] { "ch3", "ch4" },
            PrefetchChaptersService.ComputePrefetchWindow(FiveChapters, "ch3"));
        Assert.Equal(
            new[] { "ch5" },
            PrefetchChaptersService.ComputePrefetchWindow(FiveChapters, "ch5"));
        Assert.Empty(PrefetchChaptersService.ComputePrefetchWindow(FiveChapters, "ch9"));
        Assert.Empty(PrefetchChaptersService.ComputePrefetchWindow(null, "ch1"));
    }

    [Fact]
    public void DecideTrigger_ChapterChange_ReturnsNewId()
    {
        Assert.Equal(
            "ch2",
            PrefetchChaptersService.DecidePrefetchTrigger(FiveChapters, "ch1", "ch2"));
        Assert.Equal(
            "ch1",
            PrefetchChaptersService.DecidePrefetchTrigger(FiveChapters, null, "ch1"));
    }

    [Fact]
    public void DecideTrigger_NoChangeOrInvalid_StaysSilent()
    {
        Assert.Null(PrefetchChaptersService.DecidePrefetchTrigger(FiveChapters, "ch1", "ch1"));
        Assert.Null(PrefetchChaptersService.DecidePrefetchTrigger(FiveChapters, "ch1", null));
        Assert.Null(PrefetchChaptersService.DecidePrefetchTrigger(FiveChapters, "ch1", string.Empty));
        Assert.Null(PrefetchChaptersService.DecidePrefetchTrigger(
            FiveChapters, "ch1", new string('x', 129)));
        Assert.Null(PrefetchChaptersService.DecidePrefetchTrigger(FiveChapters, "ch1", "ch9"));
    }

    [Fact]
    public void DecideTrigger_EmptyOrderList_AllowsTrigger()
    {
        // Sıra bilinmiyorsa (legacy) id yine tetiklenir; native çözer.
        Assert.Equal(
            "ch2",
            PrefetchChaptersService.DecidePrefetchTrigger(
                Array.Empty<string>(), "ch1", "ch2"));
    }

    // ── Chapter-id validate ──

    [Fact]
    public void ChapterId_ValidNames_Pass()
    {
        Assert.True(PrefetchChaptersService.IsChapterIdValid("ch1"));
        Assert.True(PrefetchChaptersService.IsChapterIdValid("bölüm-1"));
        Assert.True(PrefetchChaptersService.IsChapterIdValid(new string('x', 128)));
    }

    [Fact]
    public void ChapterId_BrokenNames_Rejected()
    {
        Assert.False(PrefetchChaptersService.IsChapterIdValid(null));
        Assert.False(PrefetchChaptersService.IsChapterIdValid(string.Empty));
        Assert.False(PrefetchChaptersService.IsChapterIdValid("ch\01"));
        Assert.False(PrefetchChaptersService.IsChapterIdValid(new string('x', 129)));
        // 65 × 2-bayt UTF-8 = 130 bayt > 128 tavanı.
        Assert.False(PrefetchChaptersService.IsChapterIdValid(new string('é', 65)));
    }

    // ── Null/throw fail-closed ──

    [Fact]
    public void ReadProgress_NullOrThrowing_ReturnsUnknown()
    {
        Assert.Equal(
            PrefetchProgressSnapshot.Unknown,
            PrefetchChaptersService.ReadProgressSnapshot(null));
        Assert.Equal(
            PrefetchProgressSnapshot.Unknown,
            PrefetchChaptersService.ReadProgressSnapshot(() => throw new InvalidOperationException()));
        Assert.Equal(
            PrefetchProgressSnapshot.Unknown,
            PrefetchChaptersService.ReadProgressSnapshot(() => "{ broken"));
    }

    [Fact]
    public void ReadLoadedChapters_NullOrThrowing_ReturnsEmpty()
    {
        Assert.Equal(string.Empty, PrefetchChaptersService.ReadLoadedChaptersJson(null));
        Assert.Equal(
            string.Empty,
            PrefetchChaptersService.ReadLoadedChaptersJson(() => throw new InvalidOperationException()));
        Assert.Equal(
            "{\"active\":\"ch1\"}",
            PrefetchChaptersService.ReadLoadedChaptersJson(() => "{\"active\":\"ch1\"}"));
    }

    [Fact]
    public void ReadBoundary_NullOrThrowing_ReturnsFalse()
    {
        Assert.False(PrefetchChaptersService.ReadIsChapterBoundaryNode(null));
        Assert.False(PrefetchChaptersService.ReadIsChapterBoundaryNode(
            () => throw new InvalidOperationException()));
        Assert.True(PrefetchChaptersService.ReadIsChapterBoundaryNode(() => 1));
        Assert.True(PrefetchChaptersService.ReadIsChapterBoundaryNode(() => 7));
        Assert.False(PrefetchChaptersService.ReadIsChapterBoundaryNode(() => 0));
    }

    [Fact]
    public void ReadPump_NullOrThrowingOrNegative_ReturnsZero()
    {
        Assert.Equal(0, PrefetchChaptersService.ReadPumpNewlyReady(null));
        Assert.Equal(0, PrefetchChaptersService.ReadPumpNewlyReady(
            () => throw new InvalidOperationException()));
        Assert.Equal(0, PrefetchChaptersService.ReadPumpNewlyReady(() => -3));
        Assert.Equal(2, PrefetchChaptersService.ReadPumpNewlyReady(() => 2));
    }

    // ── Describe ──

    [Fact]
    public void Describe_CompleteJson_ShowsReadyBadge()
    {
        var description = PrefetchChaptersService.Describe(ProgressJson);
        Assert.True(description.Visible);
        Assert.Contains("3/4", description.Text);
        Assert.Contains("1 eksik", description.Text);
        Assert.Contains("1792", description.Text);
    }

    [Fact]
    public void Describe_IncompleteJson_ShowsProgressBadge()
    {
        var description = PrefetchChaptersService.Describe(IncompleteProgressJson);
        Assert.True(description.Visible);
        Assert.Contains("2/4", description.Text);
    }

    [Fact]
    public void Describe_BrokenOrEmpty_StaysHidden()
    {
        Assert.Equal(
            PrefetchChaptersDescription.Hidden,
            PrefetchChaptersService.Describe(null));
        Assert.Equal(
            PrefetchChaptersDescription.Hidden,
            PrefetchChaptersService.Describe(string.Empty));
        Assert.Equal(
            PrefetchChaptersDescription.Hidden,
            PrefetchChaptersService.Describe("{ broken"));
        Assert.Equal(
            PrefetchChaptersDescription.Hidden,
            PrefetchChaptersService.Describe("{\"total_assets\":1}"));
        string zeroTotal = IncompleteProgressJson.Replace("\"total_assets\":4", "\"total_assets\":0");
        Assert.Equal(
            PrefetchChaptersDescription.Hidden,
            PrefetchChaptersService.Describe(zeroTotal));
    }

    [Fact]
    public void ReadBadgeText_MirrorsDescribe()
    {
        Assert.NotEqual(
            string.Empty,
            PrefetchChaptersService.ReadBadgeText(() => ProgressJson));
        Assert.Equal(string.Empty, PrefetchChaptersService.ReadBadgeText(null));
        Assert.Equal(
            string.Empty,
            PrefetchChaptersService.ReadBadgeText(() => throw new InvalidOperationException()));
    }

    // ── Paket referans kümesi ──

    [Fact]
    public void ChapterPackageFile_MatchesChaptersPrefixJson()
    {
        Assert.True(PrefetchChaptersService.IsChapterPackageFile("json/chapters/ch2.json"));
        Assert.True(PrefetchChaptersService.IsChapterPackageFile("json/chapters/chapter_index.json"));
        Assert.False(PrefetchChaptersService.IsChapterPackageFile("json/full_story_graph.json"));
        Assert.False(PrefetchChaptersService.IsChapterPackageFile("images/bg.png"));
        Assert.False(PrefetchChaptersService.IsChapterPackageFile("json/chapters/notes.txt"));
        Assert.False(PrefetchChaptersService.IsChapterPackageFile(null));
        Assert.False(PrefetchChaptersService.IsChapterPackageFile(string.Empty));
    }

    [Fact]
    public void CollectChapterPackageRefs_FiltersDiskList()
    {
        var refs = PrefetchChaptersService.CollectChapterPackageRefs(new[]
        {
            "json/chapters/chapter_index.json",
            "json/chapters/ch1.json",
            "json/full_story_graph.json",
            "images/bg.png",
        });
        Assert.Equal(
            new[] { "json/chapters/chapter_index.json", "json/chapters/ch1.json" },
            refs);
        Assert.Empty(PrefetchChaptersService.CollectChapterPackageRefs(null));
    }
}
