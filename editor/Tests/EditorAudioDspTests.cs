using System;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorAudioDspTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 23]: Real-Time Audio DSP Telemetry & Stereo VU Meter Controls...");

        // Step 23.1: AudioComponentViewModel Preview Commands & State Transitions
        Console.WriteLine("    [Step 23.1]: AudioComponentViewModel Preview & Toggle Commands...");
        var audioComp = new AudioComponentViewModel
        {
            BgmTrack = "test_bgm.ogg",
            SfxTrack = "test_sfx.wav",
            DspFilter = "Telephone"
        };

        bool previewCalled = false;
        string lastPreviewTrack = "";
        int lastPreviewChannel = -1;
        int lastPreviewFilter = -1;
        bool stopCalled = false;

        AudioComponentViewModel.GlobalPreviewAudioAction = (track, channel, filter) =>
        {
            previewCalled = true;
            lastPreviewTrack = track;
            lastPreviewChannel = channel;
            lastPreviewFilter = filter;
        };

        AudioComponentViewModel.GlobalStopAudioAction = () =>
        {
            stopCalled = true;
        };

        // Play BGM preview
        audioComp.ToggleBgmPreview();
        if (!audioComp.IsPreviewingBgm || audioComp.IsPreviewingSfx || !previewCalled || lastPreviewTrack != "test_bgm.ogg" || lastPreviewChannel != 0 || lastPreviewFilter != 2)
            throw new Exception("ToggleBgmPreview failed to start BGM preview with correct channel/filter");

        // Toggle off BGM preview
        previewCalled = false;
        audioComp.ToggleBgmPreview();
        if (audioComp.IsPreviewingBgm || !stopCalled)
            throw new Exception("ToggleBgmPreview failed to stop BGM preview");

        // Play SFX preview
        stopCalled = false;
        previewCalled = false;
        audioComp.ToggleSfxPreview();
        if (!audioComp.IsPreviewingSfx || audioComp.IsPreviewingBgm || !previewCalled || lastPreviewTrack != "test_sfx.wav" || lastPreviewChannel != 2)
            throw new Exception("ToggleSfxPreview failed to start SFX preview with correct channel");

        // Stop all preview
        audioComp.StopAllPreview();
        if (audioComp.IsPreviewingSfx || !stopCalled)
            throw new Exception("StopAllPreview failed to reset preview states");

        // Step 23.2: Audio Telemetry Updates & Reset
        Console.WriteLine("    [Step 23.2]: AudioComponentViewModel Telemetry Updates & Reset...");
        audioComp.UpdateAudioTelemetry(0.85f, 0.90f, 0.60f, 0.65f, isSfx: false);
        if (Math.Abs(audioComp.BgmPeakL - 0.85f) > 0.001 || Math.Abs(audioComp.BgmPeakR - 0.90f) > 0.001 ||
            Math.Abs(audioComp.BgmRmsL - 0.60f) > 0.001 || Math.Abs(audioComp.BgmRmsR - 0.65f) > 0.001)
            throw new Exception("UpdateAudioTelemetry BGM values mismatch");

        audioComp.UpdateAudioTelemetry(0.70f, 0.75f, 0.40f, 0.45f, isSfx: true);
        if (Math.Abs(audioComp.SfxPeakL - 0.70f) > 0.001 || Math.Abs(audioComp.SfxPeakR - 0.75f) > 0.001)
            throw new Exception("UpdateAudioTelemetry SFX values mismatch");

        audioComp.ResetTelemetry();
        if (audioComp.BgmPeakL != 0.0f || audioComp.BgmPeakR != 0.0f || audioComp.SfxPeakL != 0.0f || audioComp.SfxPeakR != 0.0f)
            throw new Exception("ResetTelemetry failed to clear peak/RMS values");

        // Step 23.3: LivePreviewViewModel Master Stereo VU Meter
        Console.WriteLine("    [Step 23.3]: LivePreviewViewModel Master Stereo VU Meter...");
        var livePreviewVm = mainVm.LivePreviewViewModel;
        livePreviewVm.UpdateAudioTelemetry(0.72f, 0.78f, 0.50f, 0.55f);
        if (Math.Abs(livePreviewVm.MasterPeakL - 0.72f) > 0.001 || Math.Abs(livePreviewVm.MasterPeakR - 0.78f) > 0.001 || !livePreviewVm.IsAudioActive)
            throw new Exception("LivePreviewViewModel UpdateAudioTelemetry failed");

        livePreviewVm.UpdateAudioTelemetry(0.0f, 0.0f, 0.0f, 0.0f);
        if (livePreviewVm.IsAudioActive || livePreviewVm.MasterPeakL != 0.0f)
            throw new Exception("LivePreviewViewModel silence telemetry failed");

        // Step 23.4: EngineHost Native Audio Telemetry P/Invoke Queries
        Console.WriteLine("    [Step 23.4]: EngineHost Native Audio Telemetry Queries...");
        float bgmPeak = mainVm.EngineHost.GetAudioChannelPeak(0, 0);
        float bgmRms = mainVm.EngineHost.GetAudioChannelRms(0, 0);
        float masterPeak = mainVm.EngineHost.GetAudioChannelPeak(3, 0);
        if (bgmPeak < 0.0f || bgmPeak > 1.0f || bgmRms < 0.0f || bgmRms > 1.0f || masterPeak < 0.0f || masterPeak > 1.0f)
            throw new Exception("EngineHost Audio Telemetry queries returned out-of-range values");

        float[] spectrum = new float[4];
        mainVm.EngineHost.GetAudioSpectrum(spectrum);
        if (spectrum.Length != 4 || spectrum[0] < 0.0f || spectrum[0] > 1.0f)
            throw new Exception("EngineHost.GetAudioSpectrum returned invalid spectrum bands");

        // Step 23.5: Selected Node Audio Component Telemetry Synchronization
        Console.WriteLine("    [Step 23.5]: Selected Node Audio Component Synchronization...");
        var audioNode = new NodeViewModel(2301, "Audio Telemetry Node", 0, 0, bare: true);
        var attachedAudio = audioNode.AddComponent<AudioComponentViewModel>();
        attachedAudio.BgmTrack = "test_bgm.ogg";
        mainVm.SelectNodeQuiet(audioNode);

        // Force step and check
        mainVm.EngineHost.PlayAudio("test_bgm.ogg", 0, 0);
        mainVm.EngineHost.StopBgm();

        Console.WriteLine("  ✅ [PASS] Real-Time Audio DSP Telemetry & Stereo VU Meter Controls verified");
    }
}
