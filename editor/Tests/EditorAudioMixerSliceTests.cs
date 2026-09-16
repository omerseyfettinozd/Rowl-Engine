using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 2 — audio mixer editör dilimi: VM aynası, pump rozeti,
/// polyphony linter kuralı ve mixer desk bağlantısı. Headless, native
/// çağrı YOKTUR (sparse dosya + JSON fixture + kayıtçı fake motor).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorAudioMixerSliceTests
{
    private const string PumpJson =
        "{\"count\":64,\"last_us\":12,\"avg_us\":11,\"max_us\":40}";

    private const string ZeroPumpJson =
        "{\"count\":0,\"last_us\":0,\"avg_us\":0,\"max_us\":0}";

    private static string MakeTempAssets()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlMixer_" + Guid.NewGuid().ToString("N"));
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

    private static NodeViewModel AudioNode(ulong id)
    {
        var node = new NodeViewModel(id, "Audio " + id, 0, 0, bare: true);
        node.AddComponent<AudioComponentViewModel>();
        return node;
    }

    private sealed class RecordingPlayerEngine : IPlayerEngine
    {
        public bool IsAvailable => true;
        public float AmbienceVolume = float.NaN;
        public float UiVolume = float.NaN;
        public int FadeCurve = -1;
        public int PoolDepth = -1;

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
        public string Dialogue => string.Empty;
        public IReadOnlyList<RowlEngine.Editor.Native.DialogueHistoryEntry> History =>
            Array.Empty<RowlEngine.Editor.Native.DialogueHistoryEntry>();
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
        public void SetAmbienceVolume(float value) => AmbienceVolume = value;
        public void SetUiVolume(float value) => UiVolume = value;
        public void SetFadeCurve(int curve) => FadeCurve = curve;
        public void SetSfxPoolDepth(int depth) => PoolDepth = depth;
        public void SetTextScale(float value) { }
        public void SetHighContrast(bool enabled) { }
        public void SetReducedMotion(bool enabled) { }
    }

    // ── Rozet: örneklenmiş görünür / sıfır-sayaç gizli / bozuk gizli ──

    [Fact]
    public void MixerBadge_SampledJson_IsVisibleWithDetail()
    {
        var description = MixerBadgeService.Describe(PumpJson);
        Assert.True(description.Visible);
        Assert.Contains("PUMP", description.Text);
    }

    [Fact]
    public void MixerBadge_ZeroCount_StaysCollapsed()
    {
        var description = MixerBadgeService.Describe(ZeroPumpJson);
        Assert.False(description.Visible);
        Assert.Equal(string.Empty, description.Text);
    }

    [Fact]
    public void MixerBadge_BrokenInput_FailsClosed()
    {
        Assert.False(MixerBadgeService.Describe(null).Visible);
        Assert.False(MixerBadgeService.Describe("").Visible);
        var garbage = MixerBadgeService.Describe("{ not json");
        Assert.False(garbage.Visible);
        Assert.Equal(string.Empty, garbage.Text);
        // Eksik alanlı iddia da gizlenir.
        Assert.False(MixerBadgeService.Describe("{\"count\":64}").Visible);
    }

    // ── View-model aynası (fix turu 1: per-node mixer knob'ları
    // kaldırıldı; global config profil kaynağındadır) ──

    [Fact]
    public void ViewModel_MixerDefaults_AreFailClosed()
    {
        var audio = new AudioComponentViewModel();
        Assert.False(audio.IsPumpBadgeVisible);
        Assert.Equal(string.Empty, audio.PumpBadgeText);
    }

    [Fact]
    public void ViewModel_UpdateMixerBadge_MirrorsService()
    {
        var audio = new AudioComponentViewModel();
        audio.UpdateMixerBadge(PumpJson);
        Assert.True(audio.IsPumpBadgeVisible);
        Assert.Contains("PUMP", audio.PumpBadgeText);

        audio.UpdateMixerBadge(ZeroPumpJson);
        Assert.False(audio.IsPumpBadgeVisible);
        Assert.Equal(string.Empty, audio.PumpBadgeText);

        audio.UpdateMixerBadge("{ broken");
        Assert.False(audio.IsPumpBadgeVisible);
        Assert.Equal(string.Empty, audio.PumpBadgeText);
    }

    [Fact]
    public void ViewModel_Serialize_RoundTripsLegacyKeys()
    {
        var audio = new AudioComponentViewModel
        {
            BgmTrack = "audio/theme.ogg",
            SfxTrack = "audio/click.wav",
            BgmTransition = "crossfade",
            BgmTransitionDurationSeconds = 2.5f,
        };
        var data = audio.Serialize();
        var restored = new AudioComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?>(data.ToDictionary(
            pair => pair.Key, pair => (object?)pair.Value)));

        Assert.Equal("audio/theme.ogg", restored.BgmTrack);
        Assert.Equal("audio/click.wav", restored.SfxTrack);
        Assert.Equal("crossfade", restored.BgmTransition);
        Assert.Equal(2.5f, restored.BgmTransitionDurationSeconds, precision: 5);
    }

    [Fact]
    public void ViewModel_Deserialize_LegacyMixerKeys_IgnoredFailClosed()
    {
        // Eski projelerdeki per-node mixer anahtarları sessizce
        // yoksayılır (global config artık profil kaynağındadır).
        var restored = new AudioComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?>
        {
            ["fade_curve"] = "EqualPower",
            ["sfx_pool_depth"] = 12,
            ["ambience_bed_b"] = "audio/night_bed.ogg",
            ["ambience_bed_b_volume"] = 0.4,
            ["ambience_crossfade_seconds"] = 2.5,
            ["ambience_crossfade_curve"] = "EqualPower",
        });
        var data = restored.Serialize();
        Assert.DoesNotContain("fade_curve", data.Keys);
        Assert.DoesNotContain("sfx_pool_depth", data.Keys);
        Assert.DoesNotContain("ambience_bed_b", data.Keys);
    }

    [Fact]
    public void ViewModel_Deserialize_BrokenTransitionValues_FailClosed()
    {
        var restored = new AudioComponentViewModel();
        restored.Deserialize(new Dictionary<string, object?>
        {
            ["bgm_transition"] = "tape",
            ["bgm_transition_duration_seconds"] = -5.0,
        });

        Assert.Equal("project_default", restored.BgmTransition);
        Assert.Equal(0.0f, restored.BgmTransitionDurationSeconds, precision: 5);
    }

    // ── Linter (fix turu 1: per-node mixer kuralı + BedB
    // muafiyeti kaldırıldı; global config profildedir) ──

    [Fact]
    public void Lint_UnreferencedAmbienceFile_IsFlaggedUnused()
    {
        string assets = MakeTempAssets();
        try
        {
            WriteSparse(Path.Combine(assets, "audio", "night_bed.ogg"), 2048);
            var node = new NodeViewModel(1, "Audio 1", 0, 0, bare: true);
            node.AddComponent<AudioComponentViewModel>();
            var issues = ProjectLintService.Lint(
                new List<NodeViewModel> { node }, new List<ConnectionViewModel>(), assets);
            Assert.Contains(issues, i => i.Message.Contains("night_bed"));
        }
        finally
        {
            DeleteTempAssets(assets);
        }
    }

    [Fact]
    public void LintOptions_MixerDefaultsToTrue()
    {
        Assert.True(new ProjectLintOptions().CheckLongAudio);
    }

    // ── Mixer masası: 6 bus + global config aynı setter yolundan akar ──

    [Fact]
    public void ApplyVolumes_RoutesAmbienceAndUiThroughEngineSeam()
    {
        var engine = new RecordingPlayerEngine();
        string directory = Path.Combine(Path.GetTempPath(), "RowlMixerProfile_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(directory);
            var profile = new PlayerProfile { AmbienceVolume = 0.3f, UiVolume = 1.5f };
            var vm = new PlayerViewModel(engine, new PlayerLoopService(profile, directory));
            vm.ApplyVolumes();
            Assert.Equal(0.3f, engine.AmbienceVolume, precision: 5);
            // Aralık-dışı servis clamp'iyle iner ([0,1]).
            Assert.Equal(1.0f, engine.UiVolume, precision: 5);

            // Non-finite fail-closed varsayılana düşer.
            var nanProfile = new PlayerProfile { AmbienceVolume = float.NaN, UiVolume = float.NaN };
            var nanVm = new PlayerViewModel(engine, new PlayerLoopService(nanProfile, directory));
            nanVm.ApplyVolumes();
            Assert.Equal(1.0f, engine.AmbienceVolume, precision: 5);
            Assert.Equal(1.0f, engine.UiVolume, precision: 5);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); }
            catch (Exception) { }
        }
    }

    [Fact]
    public void ApplyVolumes_RoutesGlobalMixerConfigThroughEngineSeam()
    {
        var engine = new RecordingPlayerEngine();
        string directory = Path.Combine(Path.GetTempPath(), "RowlMixerConfig_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(directory);
            var profile = new PlayerProfile { MixerFadeCurve = "EqualPower", SfxPoolDepth = 12 };
            var vm = new PlayerViewModel(engine, new PlayerLoopService(profile, directory));
            vm.ApplyVolumes();
            Assert.Equal(1, engine.FadeCurve);
            Assert.Equal(12, engine.PoolDepth);

            // Bozuk/aralık-dışı profil fail-closed varsayılana iner.
            var broken = new PlayerProfile { MixerFadeCurve = "tape", SfxPoolDepth = 99 };
            var brokenVm = new PlayerViewModel(engine, new PlayerLoopService(broken, directory));
            brokenVm.ApplyVolumes();
            Assert.Equal(0, engine.FadeCurve);
            Assert.Equal(16, engine.PoolDepth);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); }
            catch (Exception) { }
        }
    }

    [Fact]
    public void Adapter_DeadHandle_VolumeAndConfigForwardingIsSilentNoOp()
    {
        // Native yok: başlatılmamış EngineHost'ta 4 setter da throw
        // atmadan sessizce yoksayılır (fail-closed).
        using var host = new EngineHost();
        var adapter = new EngineHostPlayerAdapter(host);
        Assert.False(adapter.IsAvailable);
        var exception = Record.Exception(() =>
        {
            adapter.SetAmbienceVolume(float.NaN);
            adapter.SetUiVolume(4.0f);
            adapter.SetFadeCurve(7);
            adapter.SetSfxPoolDepth(99);
        });
        Assert.Null(exception);
    }

    [Fact]
    public void Profile_Sanitize_ClampsNewBuses()
    {
        var profile = new PlayerProfile { AmbienceVolume = 4.0f, UiVolume = float.NaN };
        profile.Sanitized();
        Assert.Equal(1.0f, profile.AmbienceVolume);
        Assert.Equal(1.0f, profile.UiVolume);
        Assert.Equal(1.0f, new PlayerProfile().AmbienceVolume);
        Assert.Equal(1.0f, new PlayerProfile().UiVolume);
    }

    [Fact]
    public void Profile_Sanitize_NormalizesGlobalMixerConfig()
    {
        var profile = new PlayerProfile { MixerFadeCurve = "tape", SfxPoolDepth = 99 };
        profile.Sanitized();
        Assert.Equal("Linear", profile.MixerFadeCurve);
        Assert.Equal(16, profile.SfxPoolDepth);
        Assert.Equal("Linear", new PlayerProfile().MixerFadeCurve);
        Assert.Equal(8, new PlayerProfile().SfxPoolDepth);
    }

    [Fact]
    public void ProfileStore_RoundTripsGlobalMixerConfig()
    {
        string directory = Path.Combine(Path.GetTempPath(), "RowlMixerStore_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(directory);
            var profile = new PlayerProfile { MixerFadeCurve = "EqualPower", SfxPoolDepth = 12 };
            Assert.Null(PlayerProfileStore.Save(directory, profile));
            var loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loaded.Status);
            Assert.Equal("EqualPower", loaded.Profile.MixerFadeCurve);
            Assert.Equal(12, loaded.Profile.SfxPoolDepth);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); }
            catch (Exception) { }
        }
    }

    [Fact]
    public void ProfileStore_RoundTripsAmbienceAndUiVolumes()
    {
        string directory = Path.Combine(Path.GetTempPath(), "RowlMixerBusStore_" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(directory);
            var profile = new PlayerProfile { AmbienceVolume = 0.3f, UiVolume = 0.7f };
            Assert.Null(PlayerProfileStore.Save(directory, profile));
            var loaded = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loaded.Status);
            Assert.Equal(0.3f, loaded.Profile.AmbienceVolume, precision: 5);
            Assert.Equal(0.7f, loaded.Profile.UiVolume, precision: 5);

            // Eksik anahtarlar default 1'e düşer.
            File.WriteAllText(
                Path.Combine(directory, PlayerProfileStore.FileName),
                "{\"version\":1}");
            var missing = PlayerProfileStore.Load(directory);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, missing.Status);
            Assert.Equal(1.0f, missing.Profile.AmbienceVolume, precision: 5);
            Assert.Equal(1.0f, missing.Profile.UiVolume, precision: 5);
        }
        finally
        {
            try { Directory.Delete(directory, recursive: true); }
            catch (Exception) { }
        }
    }
}
