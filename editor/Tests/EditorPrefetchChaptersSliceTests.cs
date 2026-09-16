using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 4 — prefetch/chapter editör dilimi: linter kuralı (eksik
/// asset warning, error yok), rozet aynası, chapter-değişiminde prefetch
/// tetik kararı ve adaptör ölü-handle sessizliği. Headless, native çağrı
/// YOKTUR (sparse dosya + JSON fixture + kayıtçı fake motor).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorPrefetchChaptersSliceTests
{
    private const string ProgressJson =
        "{\"total_assets\":4,\"ready_assets\":3,\"missing_assets\":1," +
        "\"queued_assets\":0,\"ready_bytes\":1792,\"budget_bytes\":33554432," +
        "\"complete\":true,\"missing_paths\":[\"voice.ogg\"],\"last_diagnostic\":\"\"}";

    private static string MakeTempAssets()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlPrefetch_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(root, "Assets");
        Directory.CreateDirectory(assets);
        return assets;
    }

    private static void DeleteTempAssets(string assets)
    {
        try { Directory.Delete(Path.GetDirectoryName(assets)!, recursive: true); }
        catch (Exception) { }
    }

    private sealed class RecordingPlayerEngine : IPlayerEngine
    {
        public bool IsAvailable => true;
        public string DialogueText = string.Empty;
        public string ChapterId = string.Empty;
        public string ProgressJson = string.Empty;
        public int ProgressReads;
        public readonly List<(string? ChapterId, ulong Budget)> PrefetchCalls = new();

        public void SetPlayState(bool playing) { }
        public void SetPaused(bool paused) { }
        public void ResetToStartNode() { }
        public void Step(float dt) { }
        public void AdvanceNode(uint choiceIndex) { }
        public bool SelectChoice(string optionId) => false;
        public IReadOnlyList<string> GetActiveDialogueContentIds() => Array.Empty<string>();
        public bool HasChoices() => false;
        public IReadOnlyList<string> GetChoiceLabels() => Array.Empty<string>();
        public IReadOnlyList<string> GetChoiceOptionIds() => Array.Empty<string>();
        public string Speaker => string.Empty;
        public string Dialogue => DialogueText;
        public IReadOnlyList<DialogueHistoryEntry> History => Array.Empty<DialogueHistoryEntry>();
        public void RefreshHistory() { }
        public bool SaveSlot(int index) => false;
        public bool LoadSlot(int index) => false;
        public bool HasSlot(int index) => false;
        public bool DeleteSlot(int index) => false;
        public SaveSlotMetadata? GetSlotMetadata(int index) => null;
        public void SetMasterVolume(float value) { }
        public void SetBgmVolume(float value) { }
        public void SetVoiceVolume(float value) { }
        public void SetSfxVolume(float value) { }
        public void SetTextScale(float value) { }
        public void SetHighContrast(bool enabled) { }
        public void SetReducedMotion(bool enabled) { }

        public string GetCurrentChapterId() => ChapterId;
        public string GetPrefetchProgressJson()
        {
            ProgressReads++;
            return ProgressJson;
        }

        public void PrefetchChapterAssets(string? chapterId, ulong budgetBytes) =>
            PrefetchCalls.Add((chapterId, budgetBytes));
    }

    private static PlayerViewModel MakePlayer(RecordingPlayerEngine engine, out string directory)
    {
        directory = Path.Combine(Path.GetTempPath(), "RowlPrefetchProfile_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        return new PlayerViewModel(engine, new PlayerLoopService(new PlayerProfile(), directory));
    }

    private static void DeleteDirectory(string directory)
    {
        try { Directory.Delete(directory, recursive: true); }
        catch (Exception) { }
    }

    // ── Linter ──

    [Fact]
    public void Lint_MissingPrefetchAsset_WarnsWithoutError()
    {
        string assets = MakeTempAssets();
        try
        {
            var node = new NodeViewModel(1, "Audio 1", 0, 0, bare: true);
            var audio = node.AddComponent<AudioComponentViewModel>();
            audio.BgmTrack = "audio/theme_missing.ogg";
            var issues = ProjectLintService.Lint(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            var warning = issues.FirstOrDefault(i =>
                !i.IsError && i.Message.Contains("theme_missing.ogg") && i.Message.Contains("refetch"));
            Assert.NotNull(warning);
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void Lint_ExistingPrefetchAsset_StaysSilent()
    {
        string assets = MakeTempAssets();
        try
        {
            Directory.CreateDirectory(Path.Combine(assets, "audio"));
            File.WriteAllText(Path.Combine(assets, "audio", "theme_ok.ogg"), "x");
            var node = new NodeViewModel(1, "Audio 1", 0, 0, bare: true);
            var audio = node.AddComponent<AudioComponentViewModel>();
            audio.BgmTrack = "audio/theme_ok.ogg";
            var issues = ProjectLintService.Lint(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            Assert.DoesNotContain(issues, i => i.Message.Contains("theme_ok.ogg"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void LintOptions_PrefetchDefaultsToTrue()
    {
        Assert.True(new ProjectLintOptions().CheckPrefetch);
    }

    [Fact]
    public void Lint_ChapterFiles_NeverFlaggedUnused()
    {
        string assets = MakeTempAssets();
        try
        {
            string chapters = Path.Combine(assets, "json", "chapters");
            Directory.CreateDirectory(chapters);
            File.WriteAllText(Path.Combine(chapters, "chapter_index.json"), "{\"chapters\":[]}");
            File.WriteAllText(
                Path.Combine(chapters, "ch9.json"), "{\"chapter_id\":\"ch9\",\"nodes\":[]}");
            var node = new NodeViewModel(1, "N1", 0, 0, bare: true);
            var issues = ProjectLintService.Lint(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            Assert.DoesNotContain(issues, i =>
                (i.AssetPath ?? string.Empty).Contains("chapters"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    // ── Rozet aynası ──

    [Fact]
    public void ViewModel_PrefetchBadge_DefaultsToHidden()
    {
        var engine = new RecordingPlayerEngine();
        var vm = MakePlayer(engine, out string directory);
        try
        {
            Assert.False(vm.IsPrefetchBadgeVisible);
            Assert.Equal(string.Empty, vm.PrefetchBadgeText);
        }
        finally
        {
            DeleteDirectory(directory);
        }
    }

    [Fact]
    public void ViewModel_UpdatePrefetchBadge_MirrorsService()
    {
        var engine = new RecordingPlayerEngine();
        var vm = MakePlayer(engine, out string directory);
        try
        {
            vm.UpdatePrefetchBadge(ProgressJson);
            Assert.True(vm.IsPrefetchBadgeVisible);
            Assert.Contains("3/4", vm.PrefetchBadgeText);

            vm.UpdatePrefetchBadge("{ broken");
            Assert.False(vm.IsPrefetchBadgeVisible);
            Assert.Equal(string.Empty, vm.PrefetchBadgeText);
        }
        finally
        {
            DeleteDirectory(directory);
        }
    }

    [Fact]
    public void ViewModel_RefreshPrefetchBadge_ThrottlesToPollInterval()
    {
        var engine = new RecordingPlayerEngine { ProgressJson = ProgressJson };
        var vm = MakePlayer(engine, out string directory);
        try
        {
            vm.RefreshPrefetchBadge();
            vm.RefreshPrefetchBadge();
            Assert.Equal(1, engine.ProgressReads);
            Assert.True(vm.IsPrefetchBadgeVisible);

            vm.RefreshPrefetchBadge(force: true);
            Assert.Equal(2, engine.ProgressReads);
        }
        finally
        {
            DeleteDirectory(directory);
        }
    }

    // ── Chapter-değişiminde prefetch tetik kararı ──

    [Fact]
    public void CheckChapterPrefetch_ChapterChange_TriggersWithDefaultBudget()
    {
        var engine = new RecordingPlayerEngine { ChapterId = "ch1" };
        var vm = MakePlayer(engine, out string directory);
        try
        {
            vm.CheckChapterPrefetch();
            Assert.Single(engine.PrefetchCalls);
            Assert.Equal("ch1", engine.PrefetchCalls[0].ChapterId);
            Assert.Equal(PrefetchChaptersService.DefaultBudgetBytes, engine.PrefetchCalls[0].Budget);

            // Aynı chapter tekrar tetiklemez.
            vm.CheckChapterPrefetch();
            Assert.Single(engine.PrefetchCalls);

            engine.ChapterId = "ch2";
            vm.CheckChapterPrefetch();
            Assert.Equal(2, engine.PrefetchCalls.Count);
            Assert.Equal("ch2", engine.PrefetchCalls[1].ChapterId);
        }
        finally
        {
            DeleteDirectory(directory);
        }
    }

    [Fact]
    public void CheckChapterPrefetch_EmptyOrInvalidChapter_StaysSilent()
    {
        var engine = new RecordingPlayerEngine { ChapterId = string.Empty };
        var vm = MakePlayer(engine, out string directory);
        try
        {
            vm.CheckChapterPrefetch();
            engine.ChapterId = new string('x', 129);
            vm.CheckChapterPrefetch();
            Assert.Empty(engine.PrefetchCalls);
        }
        finally
        {
            DeleteDirectory(directory);
        }
    }

    [Fact]
    public void Tick_DialogueChange_TriggersChapterPrefetch()
    {
        var engine = new RecordingPlayerEngine { ChapterId = "ch1", DialogueText = "Bir." };
        var vm = MakePlayer(engine, out string directory);
        try
        {
            Assert.True(vm.NewGame());
            vm.Tick(0.016f);
            Assert.Single(engine.PrefetchCalls);

            engine.DialogueText = "İki.";
            engine.ChapterId = "ch2";
            vm.Tick(0.016f);
            Assert.Equal(2, engine.PrefetchCalls.Count);
            Assert.Equal("ch2", engine.PrefetchCalls[1].ChapterId);
        }
        finally
        {
            DeleteDirectory(directory);
        }
    }

    [Fact]
    public void Adapter_DeadHandle_PrefetchForwardingIsSilentNoOp()
    {
        // Native yok: başlatılmamış EngineHost'ta 10 çağrı da throw
        // atmadan sessizce yoksayılır (fail-closed).
        using var host = new EngineHost();
        var adapter = new EngineHostPlayerAdapter(host);
        Assert.False(adapter.IsAvailable);
        var exception = Record.Exception(() =>
        {
            adapter.LoadChapterIndexJson("{\"chapters\":[]}");
            adapter.LoadChapterIndexJson(string.Empty);
            adapter.AppendChapterFileJson("{\"chapter_id\":\"ch1\",\"nodes\":[]}");
            adapter.LoadChapter("ch1");
            adapter.LoadChapter("bad\0id");
            adapter.UnloadChapter("ch1");
            adapter.UnloadChapter(string.Empty);
            adapter.PrefetchChapterAssets("ch1", 0);
            adapter.PrefetchChapterAssets(null, 0);
            adapter.PrefetchChapterAssets(new string('x', 5000), 0);
            adapter.PumpPrefetch(4.0f);
        });
        Assert.Null(exception);
        Assert.Equal(string.Empty, adapter.GetLoadedChaptersJson());
        Assert.Equal(string.Empty, adapter.GetPrefetchProgressJson());
        Assert.Equal(string.Empty, adapter.GetCurrentChapterId());
        Assert.False(adapter.IsChapterBoundaryNode(42));
        Assert.Equal(0, adapter.PumpPrefetch(float.NaN));
    }
}
