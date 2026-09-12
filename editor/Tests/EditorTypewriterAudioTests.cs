using System;
using System.Collections.Generic;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorTypewriterAudioTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 25]: Typewriter Character Voice Blips & Dialogue Audio Effects...");

        // Step 25.1: DialogueComponentViewModel Default Values & Property Mutation
        Console.WriteLine("    [Step 25.1]: DialogueComponentViewModel Defaults & Property Mutation...");
        var dlgComp = new DialogueComponentViewModel();
        if (dlgComp.VoiceBlipPitch != 1.0 || dlgComp.VoiceBlipVariance != 0.08 ||
            dlgComp.VoiceBlipCadence != 1 || !dlgComp.VoiceBlipSkipPunctuation ||
            dlgComp.VoiceBlipChannel != "Voice" || dlgComp.VoiceBlipVolume != 0.85)
        {
            throw new Exception("DialogueComponentViewModel voice blip defaults mismatch");
        }

        dlgComp.VoiceBlipPitch = 1.35;
        dlgComp.VoiceBlipVariance = 0.12;
        dlgComp.VoiceBlipCadence = 3;
        dlgComp.VoiceBlipSkipPunctuation = false;
        dlgComp.VoiceBlipVolume = 0.85;
        dlgComp.VoiceBlipChannel = "Sfx";

        // Step 25.2: DialogueComponentViewModel Serialization & Deserialization Round-Trip
        Console.WriteLine("    [Step 25.2]: Serialization & Deserialization Round-Trip...");
        var dlgData = dlgComp.Serialize();
        var dlgCompRestored = new DialogueComponentViewModel();
        dlgCompRestored.Deserialize(new Dictionary<string, object?>(dlgData.ToDictionary(k => k.Key, v => (object?)v.Value)));
        if (Math.Abs(dlgCompRestored.VoiceBlipPitch - 1.35) > 0.001 ||
            Math.Abs(dlgCompRestored.VoiceBlipVariance - 0.12) > 0.001 ||
            dlgCompRestored.VoiceBlipCadence != 3 ||
            dlgCompRestored.VoiceBlipSkipPunctuation != false ||
            Math.Abs(dlgCompRestored.VoiceBlipVolume - 0.85) > 0.001 ||
            dlgCompRestored.VoiceBlipChannel != "Sfx")
        {
            throw new Exception("DialogueComponentViewModel voice blip serialization round-trip failed");
        }

        // Step 25.3: CharacterComponentViewModel Defaults & Serialization
        Console.WriteLine("    [Step 25.3]: CharacterComponentViewModel Voice Blip Defaults & Serialization...");
        var charComp = new CharacterComponentViewModel();
        if (charComp.VoiceBlipPitch != 1.0 || charComp.VoiceBlipVariance != 0.08 ||
            charComp.VoiceBlipCadence != 1 || !string.IsNullOrEmpty(charComp.VoiceBlipSound))
        {
            throw new Exception("CharacterComponentViewModel voice blip defaults mismatch");
        }
        charComp.VoiceBlipSound = "audio/char_blip.wav";
        charComp.VoiceBlipPitch = 0.85;
        charComp.VoiceBlipVariance = 0.03;
        charComp.VoiceBlipCadence = 2;
        var charData = charComp.Serialize();
        var charCompRestored = new CharacterComponentViewModel();
        charCompRestored.Deserialize(new Dictionary<string, object?>(charData.ToDictionary(k => k.Key, v => (object?)v.Value)));
        if (charCompRestored.VoiceBlipSound != "audio/char_blip.wav" ||
            Math.Abs(charCompRestored.VoiceBlipPitch - 0.85) > 0.001 ||
            Math.Abs(charCompRestored.VoiceBlipVariance - 0.03) > 0.001 ||
            charCompRestored.VoiceBlipCadence != 2)
        {
            throw new Exception("CharacterComponentViewModel voice blip serialization round-trip failed");
        }

        // Step 25.4: NodeViewModel Proxy Synchronization
        Console.WriteLine("    [Step 25.4]: NodeViewModel Voice Blip Proxy Synchronization...");
        var testNode = new NodeViewModel(2501, "Voice Blip Test Node", 0, 0, bare: true);
        var attachedDlg = testNode.AddComponent<DialogueComponentViewModel>();
        testNode.VoiceBlipPitch = 1.45;
        testNode.VoiceBlipVariance = 0.2;
        testNode.VoiceBlipCadence = 4;
        testNode.VoiceBlipSkipPunctuation = false;
        testNode.VoiceBlipVolume = 0.7;
        testNode.VoiceBlipChannel = "Sfx";

        if (Math.Abs(attachedDlg.VoiceBlipPitch - 1.45) > 0.001 ||
            Math.Abs(attachedDlg.VoiceBlipVariance - 0.2) > 0.001 ||
            attachedDlg.VoiceBlipCadence != 4 ||
            attachedDlg.VoiceBlipSkipPunctuation != false ||
            Math.Abs(attachedDlg.VoiceBlipVolume - 0.7) > 0.001 ||
            attachedDlg.VoiceBlipChannel != "Sfx")
        {
            throw new Exception("NodeViewModel proxy property write failed to update DialogueComponent");
        }

        // Step 25.5: StoryGraphSerializer Component Generation & Verification
        Console.WriteLine("    [Step 25.5]: StoryGraphSerializer Preview Components with Voice Blip Data...");
        attachedDlg.DialogueText = "Voice Blip Serializer Verification";
        string jsonPreview = StoryGraphSerializer.SerializePreviewComponents(testNode);
        if ((!jsonPreview.Contains("\"voice_blip_pitch\":1.45") && !jsonPreview.Contains("\"voice_blip_pitch\": 1.45")) ||
            (!jsonPreview.Contains("\"voice_blip_cadence\":4") && !jsonPreview.Contains("\"voice_blip_cadence\": 4")) ||
            (!jsonPreview.Contains("\"voice_blip_skip_punctuation\":false") && !jsonPreview.Contains("\"voice_blip_skip_punctuation\": false")))
        {
            throw new Exception("StoryGraphSerializer.SerializePreviewComponents missing voice blip fields");
        }

        // Step 25.6: EngineHost Native P/Invoke Integration & Blip Counting
        Console.WriteLine("    [Step 25.6]: EngineHost Native Voice Blips & Audio Engine Telemetry...");
        mainVm.EngineHost.ResetVoiceBlipCount();
        if (mainVm.EngineHost.GetVoiceBlipCount() != 0)
            throw new Exception("EngineHost ResetVoiceBlipCount failed");

        // Preview voice blip triggering
        mainVm.EngineHost.PlayVoiceBlip(string.Empty, 1.2f, 0.8f, 1);
        if (mainVm.EngineHost.GetVoiceBlipCount() != 1)
            throw new Exception($"Expected 1 voice blip after manual trigger, got {mainVm.EngineHost.GetVoiceBlipCount()}");

        // Set dialogue voice blip and step scene with typewriter enabled
        string componentSyncVoiceJson = """
        [
            {
                "type": "dialogue",
                "enabled": true,
                "data": {
                    "speaker": "Evelyn",
                    "dialogue": "Hello world from C#!",
                    "typewriter_enabled": true,
                    "typewriter_speed": 40.0,
                    "voice_blip_sound": "",
                    "voice_blip_pitch": 1.25,
                    "voice_blip_variance": 0.05,
                    "voice_blip_cadence": 1,
                    "voice_blip_skip_punctuation": true,
                    "voice_blip_volume": 0.9,
                    "voice_blip_channel": 1
                }
            }
        ]
        """;
        mainVm.EngineHost.SetPlayState(true);
        mainVm.EngineHost.ResetVoiceBlipCount();
        mainVm.EngineHost.UpdateSceneFromComponents(componentSyncVoiceJson);
        mainVm.EngineHost.Step(0.15f); // Step 150ms -> ~6 chars revealed -> triggers voice blips
        uint currentBlips = mainVm.EngineHost.GetVoiceBlipCount();
        if (currentBlips == 0)
            throw new Exception("Typewriter dialogue playback did not trigger any voice blips in EngineHost");
        mainVm.EngineHost.SetPlayState(false);

        // Step 25.7: EngineHost Dialogue Voice Blip Volume Getter/Setter
        Console.WriteLine("    [Step 25.7]: EngineHost Voice Blip Volume Getter/Setter...");
        mainVm.EngineHost.SetDialogueVoiceBlipVolume(0.42f);
        if (Math.Abs(mainVm.EngineHost.GetDialogueVoiceBlipVolume() - 0.42f) > 0.001f)
            throw new Exception("EngineHost SetDialogueVoiceBlipVolume / GetDialogueVoiceBlipVolume failed");

        // Step 25.8: Character Default Voice Blip Fallback Inheritance in EngineHost
        Console.WriteLine("    [Step 25.8]: Character Voice Blip Fallback Inheritance in EngineHost...");
        string inheritJson = """
        [
            {
                "type": "character",
                "enabled": true,
                "data": {
                    "sprite": "spr_evelyn.png",
                    "voice_blip_sound": "",
                    "voice_blip_pitch": 1.65,
                    "voice_blip_cadence": 3
                }
            },
            {
                "type": "dialogue",
                "enabled": true,
                "data": {
                    "speaker": "Evelyn",
                    "dialogue": "Inheriting voice pitch from character!",
                    "typewriter_enabled": true,
                    "typewriter_speed": 40.0
                }
            }
        ]
        """;
        mainVm.EngineHost.UpdateSceneFromComponents(inheritJson);
        if (Math.Abs(mainVm.EngineHost.GetDialogueVoiceBlipPitch() - 1.65f) > 0.001f ||
            mainVm.EngineHost.GetDialogueVoiceBlipCadence() != 3)
        {
            throw new Exception("EngineHost failed to inherit procedural voice blip pitch/cadence from character");
        }

        // Step 25.9: Punctuation Skipping Defense
        Console.WriteLine("    [Step 25.9]: Punctuation-Only Fast-Forward Suppression...");
        string punctuationOnlyJson = """
        [
            {
                "type": "dialogue",
                "enabled": true,
                "data": {
                    "speaker": "Evelyn",
                    "dialogue": "......",
                    "typewriter_enabled": true,
                    "typewriter_speed": 20.0,
                    "voice_blip_skip_punctuation": true
                }
            }
        ]
        """;
        mainVm.EngineHost.SetPlayState(true);
        mainVm.EngineHost.ResetVoiceBlipCount();
        mainVm.EngineHost.UpdateSceneFromComponents(punctuationOnlyJson);
        mainVm.EngineHost.Step(0.2f);
        if (mainVm.EngineHost.GetVoiceBlipCount() != 0)
            throw new Exception("EngineHost triggered voice blips on punctuation-only dialogue when skip_punctuation was enabled");
        mainVm.EngineHost.SetPlayState(false);

        // Step 25.10: StoryGraphSerializer Active Story JSON Serialization
        Console.WriteLine("    [Step 25.10]: StoryGraphSerializer.SerializeActiveStory Voice Blip Round-Trip...");
        string activeStoryJson = StoryGraphSerializer.SerializeActiveStory(testNode);
        if (!activeStoryJson.Contains("\"voice_blip_pitch\": 1.45") && !activeStoryJson.Contains("\"voice_blip_pitch\":1.45"))
            throw new Exception("SerializeActiveStory did not include voice_blip_pitch");
        if (!activeStoryJson.Contains("\"voice_blip_volume\": 0.7") && !activeStoryJson.Contains("\"voice_blip_volume\":0.7"))
            throw new Exception("SerializeActiveStory did not include voice_blip_volume");

        Console.WriteLine("  ✅ [PASS] Typewriter Character Voice Blips & Dialogue Audio Effects verified");
    }
}
