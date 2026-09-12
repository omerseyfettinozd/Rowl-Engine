using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorAudioPickerTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 19]: EditorAudioAssetPickerService & EditorModalDialogCoordinator Isolation...");

        // Step 19.1: EnsureAssetsAudioFolder
        Console.WriteLine("    [Step 19.1]: EnsureAssetsAudioFolder Directory Creation...");
        string testAudioFolder = EditorAudioAssetPickerService.EnsureAssetsAudioFolder(Path.Combine(testProjectRoot, "Assets"));
        if (!Directory.Exists(testAudioFolder) || !testAudioFolder.EndsWith("audio"))
            throw new Exception($"EnsureAssetsAudioFolder failed to create or return audio folder: {testAudioFolder}");

        // Step 19.2: ApplyAudioTrackToComponent
        Console.WriteLine("    [Step 19.2]: ApplyAudioTrackToComponent BGM, SFX and Volume Clamping...");
        var audioComp = new AudioComponentViewModel();
        bool bgmSet = EditorAudioAssetPickerService.ApplyAudioTrackToComponent(audioComp, "theme_epic.ogg", "bgm", 0.75f);
        if (!bgmSet || audioComp.BgmTrack != "theme_epic.ogg" || Math.Abs(audioComp.Volume - 0.75f) > 0.001f)
            throw new Exception("ApplyAudioTrackToComponent failed to set BGM track and volume.");

        bool sfxSet = EditorAudioAssetPickerService.ApplyAudioTrackToComponent(audioComp, "laser.wav", "sfx", 1.8f);
        if (!sfxSet || audioComp.SfxTrack != "laser.wav" || Math.Abs(audioComp.Volume - 1.0f) > 0.001f)
            throw new Exception("ApplyAudioTrackToComponent failed to set SFX track or clamp volume to 1.0.");

        bool clampedLow = EditorAudioAssetPickerService.ApplyAudioTrackToComponent(audioComp, "click.wav", "sfx", -0.5f);
        if (!clampedLow || Math.Abs(audioComp.Volume - 0.0f) > 0.001f)
            throw new Exception("ApplyAudioTrackToComponent failed to clamp negative volume to 0.0.");

        bool invalidTrack = EditorAudioAssetPickerService.ApplyAudioTrackToComponent(audioComp, "voice.wav", "invalid_type");
        if (invalidTrack)
            throw new Exception("ApplyAudioTrackToComponent should return false for unsupported track type.");

        // Step 19.3: ImportAudioFile
        Console.WriteLine("    [Step 19.3]: ImportAudioFile Single-File Ingestion...");
        string tempAudioSrcDir = Path.Combine(Path.GetTempPath(), $"RowlAudioSrc_{Guid.NewGuid():N}");
        Directory.CreateDirectory(tempAudioSrcDir);
        try
        {
            string sampleWav = Path.Combine(tempAudioSrcDir, "ui_chime.wav");
            File.WriteAllBytes(sampleWav, new byte[] { 0x52, 0x49, 0x46, 0x46 });
            string importedAudioName = EditorAssetImportService.ImportAudioFile(sampleWav, Path.Combine(testProjectRoot, "Assets"));
            if (importedAudioName != "ui_chime.wav" || !File.Exists(Path.Combine(testAudioFolder, "ui_chime.wav")))
                throw new Exception($"ImportAudioFile failed to ingest {importedAudioName} into Assets/audio.");

            // Self-copy idempotence check
            string reimported = EditorAssetImportService.ImportAudioFile(Path.Combine(testAudioFolder, "ui_chime.wav"), Path.Combine(testProjectRoot, "Assets"));
            if (reimported != "ui_chime.wav")
                throw new Exception("ImportAudioFile failed self-copy idempotence check.");
        }
        finally
        {
            try { Directory.Delete(tempAudioSrcDir, true); } catch { }
        }

        // Step 19.4: AudioComponentViewModel Serialization / Deserialization
        Console.WriteLine("    [Step 19.4]: AudioComponentViewModel Serialization & Round-Trip...");
        var serialized = audioComp.Serialize();
        if (!serialized.ContainsKey("bgm_track") || !serialized.ContainsKey("sfx_track") || !serialized.ContainsKey("volume"))
            throw new Exception("AudioComponentViewModel.Serialize missing BGM, SFX or Volume keys.");

        var roundTripComp = new AudioComponentViewModel();
        roundTripComp.Deserialize(new Dictionary<string, object?>
        {
            ["dsp_filter"] = "CaveReverb",
            ["bgm_track"] = "ambient.ogg",
            ["sfx_track"] = "water_drop.wav",
            ["volume"] = 0.65f,
            ["bgm_transition"] = "crossfade",
            ["bgm_transition_duration_seconds"] = 3.5f
        });

        if (roundTripComp.DspFilter != "CaveReverb" ||
            roundTripComp.BgmTrack != "ambient.ogg" ||
            roundTripComp.SfxTrack != "water_drop.wav" ||
            Math.Abs(roundTripComp.Volume - 0.65f) > 0.001f ||
            roundTripComp.BgmTransition != "crossfade" ||
            Math.Abs(roundTripComp.BgmTransitionDurationSeconds - 3.5f) > 0.001f)
        {
            throw new Exception("AudioComponentViewModel.Deserialize roundtrip mismatch.");
        }

        // Step 19.5: EditorModalDialogCoordinator Unsaved Changes Resolution Gating
        Console.WriteLine("    [Step 19.5]: EditorModalDialogCoordinator Unsaved Gating...");
        bool blockedTransition = EditorModalDialogCoordinator.OpenProjectHubAsync(
            new Window(),
            () => Task.FromResult(false)).GetAwaiter().GetResult();
        if (blockedTransition)
            throw new Exception("OpenProjectHubAsync should abort transition when unsaved changes resolution returns false.");

        Console.WriteLine("  ✅ [PASS] EditorAudioAssetPickerService & EditorModalDialogCoordinator Isolation verified");
    }
}
