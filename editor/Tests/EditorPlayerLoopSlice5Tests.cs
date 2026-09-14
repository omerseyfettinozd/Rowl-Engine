using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 2 Dilim 5 — player shell integration over a fake engine.</summary>
public sealed class EditorPlayerLoopSlice5Tests
{
    // 1×1 transparent PNG (thumbnail decode path).
    private const string TinyPngBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";

    private sealed class FakePlayerEngine : IPlayerEngine
    {
        public bool IsAvailable { get; set; } = true;
        public bool Paused { get; private set; }
        public bool PlayState { get; private set; }
        public int Advances { get; private set; }
        public readonly List<string> SelectedChoices = new();
        public readonly List<DialogueHistoryEntry> Backlog = new();
        public readonly Dictionary<int, SaveSlotMetadata> Slots = new();
        public readonly List<float> SteppedDts = new();

        public string CurrentSpeaker = "Evelyn";
        public string CurrentDialogue = "Hello.";
        public List<string> ActiveIds = new() { "11111111-2222-3333-4444-555555555555" };
        public List<(string OptionId, string Label)> ChoiceButtons = new();

        public float MasterVolume, BgmVolume, VoiceVolume, SfxVolume;

        public void SetPlayState(bool playing) => PlayState = playing;
        public void SetPaused(bool paused) => Paused = paused;
        public void ResetToStartNode() { CurrentDialogue = "Hello."; Backlog.Clear(); }
        public void Step(float dt) => SteppedDts.Add(dt);

        public void AdvanceNode(uint choiceIndex)
        {
            Advances++;
            Backlog.Add(new DialogueHistoryEntry
            {
                node_id = (ulong)(100 + Advances),
                speaker = CurrentSpeaker,
                dialogue = CurrentDialogue,
                read = true,
                content_id = ActiveIds.FirstOrDefault() ?? string.Empty,
            });
            CurrentDialogue = $"Line {Advances + 1}.";
            ActiveIds = new List<string> { $"00000000-0000-0000-0000-00000000000{Advances}" };
        }

        public bool SelectChoice(string optionId)
        {
            if (string.IsNullOrWhiteSpace(optionId))
                return false;
            SelectedChoices.Add(optionId);
            AdvanceNode(0);
            return true;
        }

        public IReadOnlyList<string> GetActiveDialogueContentIds() => ActiveIds;
        public bool HasChoices() => ChoiceButtons.Count > 0;
        public IReadOnlyList<string> GetChoiceLabels() => ChoiceButtons.Select(c => c.Label).ToList();
        public IReadOnlyList<string> GetChoiceOptionIds() => ChoiceButtons.Select(c => c.OptionId).ToList();
        public string Speaker => CurrentSpeaker;
        public string Dialogue => CurrentDialogue;
        public IReadOnlyList<DialogueHistoryEntry> History => Backlog;
        public void RefreshHistory() { }

        public bool SaveSlot(int index)
        {
            Slots[index] = new SaveSlotMetadata
            {
                Slot = index,
                SavedAt = $"2026-09-14T10:00:{index:00}Z",
                Summary = CurrentDialogue,
                HasThumbnail = false,
            };
            return true;
        }

        public bool LoadSlot(int index) => Slots.ContainsKey(index);
        public bool HasSlot(int index) => Slots.ContainsKey(index);
        public bool DeleteSlot(int index) => Slots.Remove(index);
        public SaveSlotMetadata? GetSlotMetadata(int index) =>
            Slots.TryGetValue(index, out var metadata) ? metadata : null;

        public void SetMasterVolume(float value) => MasterVolume = value;
        public void SetBgmVolume(float value) => BgmVolume = value;
        public void SetVoiceVolume(float value) => VoiceVolume = value;
        public void SetSfxVolume(float value) => SfxVolume = value;
        public float TextScale { get; private set; } = 1f;
        public bool HighContrast { get; private set; }
        public bool ReducedMotion { get; private set; }
        public void SetTextScale(float value) => TextScale = value;
        public void SetHighContrast(bool enabled) => HighContrast = enabled;
        public void SetReducedMotion(bool enabled) => ReducedMotion = enabled;
    }

    private static (PlayerViewModel ViewModel, FakePlayerEngine Engine, string Directory) CreateShell(
        PlayerSkipMode skip = PlayerSkipMode.Off)
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlSlice5_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var engine = new FakePlayerEngine();
        var loop = new PlayerLoopService(
            new PlayerProfile { SkipMode = skip }, directory);
        return (new PlayerViewModel(engine, loop), engine, directory);
    }

    // ── Full loop ──────────────────────────────────────────────

    [Fact]
    public void FullLoop_NewGameAdvancePauseSaveResumeBacklogQuit()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            Assert.True(vm.NewGame());
            Assert.Equal(PlayerState.Playing, vm.Machine.Current);
            Assert.False(engine.Paused);
            Assert.True(engine.PlayState);

            vm.OnAdvanceInput();
            Assert.Equal(1, engine.Advances);
            Assert.True(vm.Profile.IsRead("11111111-2222-3333-4444-555555555555"));

            Assert.True(vm.RequestIntent(PlayerIntent.OpenPause).Allowed);
            Assert.True(engine.Paused);

            Assert.True(vm.RequestIntent(PlayerIntent.OpenSave).Allowed);
            Assert.True(vm.SaveToSlot(3));
            Assert.True(engine.HasSlot(3));

            Assert.True(vm.RequestIntent(PlayerIntent.CloseOverlay).Allowed);
            Assert.Equal(PlayerState.Pause, vm.Machine.Current);
            Assert.True(vm.RequestIntent(PlayerIntent.Resume).Allowed);
            Assert.False(engine.Paused);

            Assert.Single(vm.HistoryEntries);

            Assert.True(vm.RequestIntent(PlayerIntent.QuitToTitle).Allowed == false);
            // QuitToTitle is pause-only: go through pause.
            Assert.True(vm.RequestIntent(PlayerIntent.OpenPause).Allowed);
            Assert.True(vm.RequestIntent(PlayerIntent.QuitToTitle).Allowed);
            Assert.Equal(PlayerState.Title, vm.Machine.Current);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void Continue_FailsGracefullyWithoutSlots_PicksLatestOtherwise()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            Assert.False(vm.Continue());
            Assert.NotEmpty(vm.LastError);
            Assert.Equal(PlayerState.Title, vm.Machine.Current);

            engine.Slots[5] = new SaveSlotMetadata { Slot = 5, SavedAt = "2026-09-14T10:00:05Z" };
            engine.Slots[2] = new SaveSlotMetadata { Slot = 2, SavedAt = "2026-09-14T10:05:00Z" };
            Assert.True(vm.Continue());
            Assert.Equal(PlayerState.Playing, vm.Machine.Current);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── Choices ────────────────────────────────────────────────

    [Fact]
    public void Choices_BlockAdvanceAndResolveByOptionId()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            vm.NewGame();
            engine.ChoiceButtons.Add(("go-left", "Go left"));
            engine.ChoiceButtons.Add(("go-right", "Go right"));

            vm.OnAdvanceInput();
            Assert.Equal(0, engine.Advances);

            vm.RefreshPresentation();
            Assert.Equal(2, vm.Choices.Count);
            Assert.Equal("Go right", vm.Choices[1].Label);

            vm.SelectChoice("go-right");
            Assert.Equal(new[] { "go-right" }, engine.SelectedChoices);
            Assert.Equal(1, engine.Advances);

            vm.Tick(0.016f);
            Assert.True(vm.Machine.ChoicesPending);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── Drivers ────────────────────────────────────────────────

    [Fact]
    public void Tick_SkipsReadLinesAndHaltsOnUnreadOrMenus()
    {
        var (vm, engine, directory) = CreateShell(PlayerSkipMode.ReadOnly);
        try
        {
            vm.NewGame();
            vm.Tick(0.016f);
            Assert.Equal(0, engine.Advances); // unread: halt

            vm.Profile.MarkRead("11111111-2222-3333-4444-555555555555");
            vm.Tick(0.016f);
            Assert.Equal(1, engine.Advances);

            engine.ChoiceButtons.Add(("x", "X"));
            vm.Tick(0.016f);
            Assert.Equal(1, engine.Advances); // choices halt

            engine.ChoiceButtons.Clear();
            vm.RequestIntent(PlayerIntent.OpenPause);
            int before = engine.Advances;
            vm.Tick(1.0f);
            Assert.Equal(before, engine.Advances); // menus halt
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void Tick_AutoAdvancesAfterReadingTime()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            vm.NewGame();
            vm.Profile.AutoEnabled = true;
            vm.Profile.AutoAdvanceDelay = 0.0f;
            vm.Profile.TextSpeedMultiplier = 100.0f;
            vm.AutoDriver.SetEnabled(true);

            vm.Tick(0.016f); // text changed: arm
            vm.Tick(0.016f); // stable: complete
            int before = engine.Advances;
            vm.Tick(5.0f); // wait elapsed (short line, fast speed)
            Assert.True(engine.Advances > before);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void Toggles_CycleSkipAndFlipAutoWithPersistence()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            Assert.Equal(PlayerSkipMode.Off, vm.Profile.SkipMode);
            vm.HandleCommand(PlayerInputCommand.ToggleSkip);
            Assert.Equal(PlayerSkipMode.ReadOnly, vm.Profile.SkipMode);
            vm.HandleCommand(PlayerInputCommand.ToggleSkip);
            Assert.Equal(PlayerSkipMode.All, vm.Profile.SkipMode);
            vm.HandleCommand(PlayerInputCommand.ToggleSkip);
            Assert.Equal(PlayerSkipMode.Off, vm.Profile.SkipMode);

            Assert.False(vm.Profile.AutoEnabled);
            vm.HandleCommand(PlayerInputCommand.ToggleAuto);
            Assert.True(vm.Profile.AutoEnabled);
            Assert.True(vm.AutoDriver.Enabled);

            var reloaded = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerSkipMode.Off, reloaded.Profile.SkipMode);
            Assert.True(reloaded.Profile.AutoEnabled);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── Input wiring & exit ────────────────────────────────────

    [Fact]
    public void HandleCommand_WiresPauseBacklogQuicksave()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            vm.NewGame();
            vm.HandleCommand(PlayerInputCommand.TogglePause);
            Assert.Equal(PlayerState.Pause, vm.Machine.Current);
            Assert.True(engine.Paused);
            vm.HandleCommand(PlayerInputCommand.TogglePause);
            Assert.Equal(PlayerState.Playing, vm.Machine.Current);

            vm.HandleCommand(PlayerInputCommand.OpenBacklog);
            Assert.Equal(PlayerState.Backlog, vm.Machine.Current);

            vm.HandleCommand(PlayerInputCommand.CancelBack);
            Assert.Equal(PlayerState.Playing, vm.Machine.Current);

            vm.HandleCommand(PlayerInputCommand.QuickSave);
            Assert.True(engine.HasSlot(0));
            engine.CurrentDialogue = "Changed.";
            vm.HandleCommand(PlayerInputCommand.QuickLoad);
            Assert.Equal(PlayerState.Playing, vm.Machine.Current);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void TitleLoadFlow_LoadsSlotAndEntersPlaying()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            engine.Slots[4] = new SaveSlotMetadata { Slot = 4, SavedAt = "2026-09-14T10:00:04Z" };
            Assert.True(vm.RequestIntent(PlayerIntent.OpenLoad).Allowed);
            Assert.Equal(PlayerState.Load, vm.Machine.Current);
            Assert.True(vm.LoadFromSlot(4));
            Assert.Equal(PlayerState.Playing, vm.Machine.Current);
            Assert.True(engine.PlayState);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void ExitFlow_SetsExitRequestedForApplication()
    {
        var (vm, _, directory) = CreateShell();
        try
        {
            vm.NewGame();
            vm.RequestIntent(PlayerIntent.RequestExitToApp);
            Assert.Equal(PlayerState.ConfirmExit, vm.Machine.Current);
            vm.RequestIntent(PlayerIntent.ConfirmExit);
            Assert.True(vm.ExitRequested);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    // ── Slot picker ────────────────────────────────────────────

    [Fact]
    public void Slots_PageOccupiedEntriesAndLatest()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            Assert.Equal(9, PlayerSaveSlotsViewModel.TotalPages);
            Assert.Equal(12, vm.Slots.Entries.Count);
            Assert.All(vm.Slots.Entries, entry => Assert.False(entry.Occupied));

            engine.Slots[0] = new SaveSlotMetadata
            {
                Slot = 0,
                SavedAt = "2026-09-14T10:00:00Z",
                ChapterTitle = "Arrivals",
                Summary = "Hello.",
                HasThumbnail = true,
                ThumbnailPngBase64 = TinyPngBase64,
            };
            engine.Slots[50] = new SaveSlotMetadata { Slot = 50, SavedAt = "2026-09-15T10:00:00Z" };
            vm.Slots.Refresh();

            var first = vm.Slots.Entries[0];
            Assert.True(first.Occupied);
            Assert.Equal("Arrivals", first.Chapter);
            Assert.Equal("Hello.", first.Summary);
            Assert.True(first.HasThumbnailImage);
            Assert.Equal(Convert.FromBase64String(TinyPngBase64), first.ThumbnailPng);

            Assert.Equal(50, vm.Slots.FindLatestOccupied());
            vm.Slots.Page = 4;
            vm.Slots.Refresh();
            Assert.True(vm.Slots.Entries.Any(entry => entry.Index == 50 && entry.Occupied));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    [Fact]
    public void Slots_CorruptThumbnailDegradesToNull()
    {
        var (vm, engine, directory) = CreateShell();
        try
        {
            engine.Slots[1] = new SaveSlotMetadata
            {
                Slot = 1,
                SavedAt = "2026-09-14T10:00:00Z",
                HasThumbnail = true,
                ThumbnailPngBase64 = "!!!not-base64!!!",
            };
            vm.Slots.Refresh();
            var entry = vm.Slots.Entries.First(e => e.Index == 1);
            Assert.True(entry.Occupied);
            Assert.Null(entry.ThumbnailPng);
            Assert.False(entry.HasThumbnailImage);
            Assert.True(vm.Slots.ThumbnailDecodeErrors > 0);
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }
}
