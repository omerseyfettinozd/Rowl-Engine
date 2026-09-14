using System;
using System.Collections.Generic;
using System.IO;
using Avalonia.Input;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 3 Dilim 5 — accessibility: text-scale snapping/sanitizing, profile
/// persistence of the new keys, engine application through fakes, input
/// remap compatibility and preference surfacing. No native library needed.
/// </summary>
public sealed class EditorAccessibilitySlice5Tests
{
    private sealed class FakePlayerEngine : IPlayerEngine
    {
        public bool IsAvailable => true;
        public float TextScale { get; private set; } = 1f;
        public bool HighContrast { get; private set; }
        public bool ReducedMotion { get; private set; }

        public void SetPlayState(bool playing) { }
        public void SetPaused(bool paused) { }
        public void ResetToStartNode() { }
        public void Step(float dt) { }
        public void AdvanceNode(uint choiceIndex) { }
        public bool SelectChoice(string optionId) => false;
        public IReadOnlyList<string> GetActiveDialogueContentIds() =>
            Array.Empty<string>();
        public bool HasChoices() => false;
        public IReadOnlyList<string> GetChoiceLabels() => Array.Empty<string>();
        public IReadOnlyList<string> GetChoiceOptionIds() => Array.Empty<string>();
        public string Speaker => string.Empty;
        public string Dialogue => string.Empty;
        public IReadOnlyList<DialogueHistoryEntry> History =>
            Array.Empty<DialogueHistoryEntry>();
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
        public void SetTextScale(float value) => TextScale = value;
        public void SetHighContrast(bool enabled) => HighContrast = enabled;
        public void SetReducedMotion(bool enabled) => ReducedMotion = enabled;
    }

    private static (PlayerViewModel ViewModel, FakePlayerEngine Engine, string Directory) CreateShell(
        PlayerProfile? profile = null)
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlA11y_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var engine = new FakePlayerEngine();
        var loop = new PlayerLoopService(profile ?? new PlayerProfile(), directory);
        return (new PlayerViewModel(engine, loop), engine, directory);
    }

    // ── Text-scale steps ─────────────────────────────────────────────

    [Fact]
    public void SnapTextScale_PinsToOfferedSteps()
    {
        Assert.Equal(1f, PlayerProfile.SnapTextScale(1f));
        Assert.Equal(1.25f, PlayerProfile.SnapTextScale(1.3f));
        Assert.Equal(1.5f, PlayerProfile.SnapTextScale(1.4f));
        Assert.Equal(1.5f, PlayerProfile.SnapTextScale(2f));
        Assert.Equal(1f, PlayerProfile.SnapTextScale(0f));
        Assert.Equal(1f, PlayerProfile.SnapTextScale(float.NaN));
        Assert.Equal(new[] { 1f, 1.25f, 1.5f }, PlayerProfile.AllowedTextScales);
    }

    [Fact]
    public void Sanitize_SnapsScaleAndKeepsBools()
    {
        var profile = new PlayerProfile
        {
            TextScale = 1.4f,
            HighContrast = true,
            ReducedMotion = true,
        };
        profile.Sanitized();
        Assert.Equal(1.5f, profile.TextScale);
        Assert.True(profile.HighContrast);
        Assert.True(profile.ReducedMotion);
    }

    [Fact]
    public void CycleTextScale_WrapsAround()
    {
        Assert.Equal(1.25f, AccessibilityService.CycleTextScale(1f));
        Assert.Equal(1.5f, AccessibilityService.CycleTextScale(1.25f));
        Assert.Equal(1f, AccessibilityService.CycleTextScale(1.5f));
        Assert.Equal(1.5f, AccessibilityService.CycleTextScale(1.3f));
    }

    [Fact]
    public void CapabilityBit_MatchesNativeHeader()
    {
        Assert.Equal(1024UL, AccessibilityService.NativeCapabilityAccessibility);
    }

    // ── Persistence ──────────────────────────────────────────────────

    [Fact]
    public void ProfileStore_RoundtripsAccessibilityKeys()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlA11yStore_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var profile = new PlayerProfile
            {
                TextScale = 1.5f,
                HighContrast = true,
                ReducedMotion = true,
            };
            Assert.Null(PlayerProfileStore.Save(directory, profile));
            PlayerProfileStore.LoadResult loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(1.5f, loaded.Profile.TextScale);
            Assert.True(loaded.Profile.HighContrast);
            Assert.True(loaded.Profile.ReducedMotion);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); } catch (Exception) { }
        }
    }

    [Fact]
    public void ProfileStore_LegacyFileFallsBackToDefaults()
    {
        string directory = Path.Combine(Path.GetTempPath(), $"RowlA11yLegacy_{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            File.WriteAllText(
                Path.Combine(directory, PlayerProfileStore.FileName),
                "{\"version\": 1, \"language\": \"tr\"}");
            PlayerProfileStore.LoadResult loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(1f, loaded.Profile.TextScale);
            Assert.False(loaded.Profile.HighContrast);
            Assert.False(loaded.Profile.ReducedMotion);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); } catch (Exception) { }
        }
    }

    // ── Engine application ───────────────────────────────────────────

    [Fact]
    public void ApplyToEngine_PushesSnappedProfileAndIgnoresNulls()
    {
        var engine = new FakePlayerEngine();
        AccessibilityService.ApplyToEngine(engine, new PlayerProfile
        {
            TextScale = 1.4f,
            HighContrast = true,
            ReducedMotion = true,
        });
        Assert.Equal(1.5f, engine.TextScale);
        Assert.True(engine.HighContrast);
        Assert.True(engine.ReducedMotion);

        AccessibilityService.ApplyToEngine(null, new PlayerProfile());
        AccessibilityService.ApplyToEngine(engine, null);
        Assert.Equal(1.5f, engine.TextScale);
    }

    [Fact]
    public void PlayerViewModel_AppliesAccessibilityOnStartAndSave()
    {
        var (viewModel, engine, directory) = CreateShell(new PlayerProfile
        {
            TextScale = 1.25f,
            HighContrast = true,
            ReducedMotion = true,
        });
        try
        {
            Assert.Equal(1.25f, engine.TextScale);
            Assert.True(engine.HighContrast);
            Assert.True(engine.ReducedMotion);
            Assert.Equal(new[] { 1f, 1.25f, 1.5f }, viewModel.AvailableTextScales);

            viewModel.Profile.TextScale = 1.5f;
            viewModel.Profile.ReducedMotion = false;
            viewModel.SavePreferences();
            Assert.Equal(1.5f, engine.TextScale);
            Assert.False(engine.ReducedMotion);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); } catch (Exception) { }
        }
    }

    [Fact]
    public void PlayerViewModel_InputCommandsDriveAccessibility()
    {
        var (viewModel, engine, directory) = CreateShell();
        try
        {
            viewModel.HandleCommand(PlayerInputCommand.CycleTextScale);
            Assert.Equal(1.25f, viewModel.Profile.TextScale);
            Assert.Equal(1.25f, engine.TextScale);
            viewModel.HandleCommand(PlayerInputCommand.ToggleHighContrast);
            Assert.True(viewModel.Profile.HighContrast);
            Assert.True(engine.HighContrast);
            viewModel.HandleCommand(PlayerInputCommand.ToggleHighContrast);
            Assert.False(viewModel.Profile.HighContrast);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); } catch (Exception) { }
        }
    }

    // ── Input remap compatibility ────────────────────────────────────

    [Fact]
    public void InputMapper_DefaultsExposeAccessibilityKeys()
    {
        Assert.Equal(PlayerInputCommand.CycleTextScale,
            PlayerInputMapper.MapKey(Key.T));
        Assert.Equal(PlayerInputCommand.ToggleHighContrast,
            PlayerInputMapper.MapKey(Key.H));
        Assert.Null(PlayerInputMapper.MapKey(Key.T, ctrl: true));
        Assert.Null(PlayerInputMapper.MapKey(Key.H, shift: true));
    }

    [Fact]
    public void InputMapper_CustomRemapOverridesDefaults()
    {
        var bindings = PlayerInputBindings.Default();
        bindings.Keys[Key.T] = PlayerInputCommand.TogglePause;
        Assert.Equal(PlayerInputCommand.TogglePause,
            PlayerInputMapper.MapKey(Key.T, bindings: bindings));
        bindings.Reset();
        Assert.Equal(PlayerInputCommand.CycleTextScale,
            PlayerInputMapper.MapKey(Key.T, bindings: bindings));
    }
}
