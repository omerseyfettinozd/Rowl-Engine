using System.Diagnostics;
using System.Reflection;
using System.Text;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Editör paused-preview dilimi kilidi (T3 recon 6 madde): StartTransition
/// IoError teşhisi, idle-upkeep dt&gt;0 sürüşü + last-result günlüğü, paused
/// setter Step+copy sunumu, spectrum WrongThread sıfırlaması. Sahte worker
/// (ölü handle: Clear/Step/setter no-op, GetLastResultCode InvalidHandle,
/// spectrum native zero-fill) + Debug dinleyicisiyle headless koşar.
/// </summary>
public sealed class EditorPausedPreviewStepTests
{
    [Fact]
    public void StartTransition_DeadHandle_EmitsStartTransitionDiagnostic()
    {
        using var host = CreateHostWithFakeWorker();

        string output = CaptureDebug(() => host.StartTransition("crossfade", 1.0f, null));

        Assert.Contains("StartTransition", output);
    }

    [Fact]
    public void IdleUpkeep_DeadHandle_EmitsUpkeepDiagnostic()
    {
        using var host = CreateHostWithFakeWorker();
        BackdateIdleUpkeep(host);

        string output = CaptureDebug(() => InvokeOnTick(host));

        Assert.Contains("idle upkeep", output);
    }

    [Fact]
    public void GetAudioSpectrum_DeadHandle_ZeroFillsStaleBands()
    {
        using var host = CreateHostWithFakeWorker();
        float[] bands = { 0.9f, 0.8f, 0.7f, 0.6f };

        host.GetAudioSpectrum(bands);

        Assert.All(bands, band => Assert.Equal(0.0f, band));
    }

    [Fact]
    public void PausedCameraFxSetters_PresentWithoutThrow()
    {
        using var host = CreateHostWithFakeWorker();

        // paused (!IsPlaying): Step+copy yolu; playing: doğrudan geçiş.
        foreach (bool playing in new[] { false, true })
        {
            typeof(EngineHost).GetProperty(nameof(EngineHost.IsPlaying))!
                .SetValue(host, playing);
            host.SetCamera(10f, 20f, 1.5f);
            host.ResetCamera();
            host.CameraPanTo(5f, 5f, 1.0f);
            host.CameraZoomTo(2.0f, 1.0f);
            host.TriggerCameraShake(0.5f, 0.3f);
            host.TriggerCameraShakePreset("soft");
            host.TriggerCameraShakeProfile(0.5f, 0.3f, 8f, 1f, 1f, 0f);
            host.StartTransition("crossfade", 0.4f, null);
            host.TriggerScreenFlash(255, 0, 0, 0.2f);
            host.TriggerScreenFlashHex("#FF0000", 0.2f);
            host.SetScreenTint(10, 20, 30, 0.5f);
            host.SetScreenTintHex("#0A141E", 0.5f);
            host.ClearScreenTint();
            host.SetVignette(0.4f);
        }

        Assert.False(host.IsCameraMoving());
        Assert.False(host.IsTransitionActive());
        Assert.False(host.IsScreenFlashActive());
    }

    private static EngineHost CreateHostWithFakeWorker()
    {
        var host = new EngineHost();
        var worker = new OffscreenRuntimeWorker(
            createHandle: () => new IntPtr(1234),
            destroyHandle: _ => { });
        typeof(EngineHost).GetField("_runtime", BindingFlags.NonPublic | BindingFlags.Instance)!
            .SetValue(host, worker);
        Assert.True(host.IsInitialized);
        return host;
    }

    private static void BackdateIdleUpkeep(EngineHost host)
        => typeof(EngineHost).GetField("_lastIdleUpkeep", BindingFlags.NonPublic | BindingFlags.Instance)!
            .SetValue(host, DateTime.UtcNow - TimeSpan.FromSeconds(5));

    private static void InvokeOnTick(EngineHost host)
        => typeof(EngineHost).GetMethod("OnTick", BindingFlags.NonPublic | BindingFlags.Instance)!
            .Invoke(host, new object?[] { null, EventArgs.Empty });

    private sealed class CaptureListener : TraceListener
    {
        public readonly StringBuilder Output = new();
        public override void Write(string? message) { lock (Output) Output.Append(message); }
        public override void WriteLine(string? message) { lock (Output) Output.AppendLine(message); }
    }

    private static string CaptureDebug(Action action)
    {
        var listener = new CaptureListener();
        Trace.Listeners.Add(listener);
        try
        {
            action();
        }
        finally
        {
            Trace.Listeners.Remove(listener);
        }
        lock (listener.Output) return listener.Output.ToString();
    }
}
