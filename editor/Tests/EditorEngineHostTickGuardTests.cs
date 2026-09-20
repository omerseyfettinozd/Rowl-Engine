using System.Reflection;
using System.Runtime.ExceptionServices;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Kod-inceleme bulgusu #2 — tick yolundaki dar JsonException filtresi +
/// try-dışı OnPropertyChanged + korumasız 60 Hz OnTick. Fırlatan bir
/// PropertyChanged abonesi eski kodda dispatcher threadinden kaçıyordu;
/// artık sayaçlı yutulmalı.
/// </summary>
public sealed class EditorEngineHostTickGuardTests
{
    [Fact]
    public void RefreshDialogueHistory_ThrowingSubscriber_DoesNotEscape()
    {
        using var host = CreateHostWithFakeWorker();
        host.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(EngineHost.DialogueHistory))
                throw new InvalidOperationException("boom");
        };

        Exception? escaped = Capture(() => host.RefreshDialogueHistory());

        Assert.Null(escaped);
    }

    [Fact]
    public void RefreshScriptRuntimeDiagnostics_ThrowingSubscriber_DoesNotEscape()
    {
        using var host = CreateHostWithFakeWorker();
        host.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(EngineHost.ScriptRuntimeDiagnostics))
                throw new InvalidOperationException("boom");
        };

        Exception? escaped = Capture(() => host.RefreshScriptRuntimeDiagnostics());

        Assert.Null(escaped);
    }

    [Fact]
    public void OnTick_PlayingWithThrowingSubscriber_DoesNotEscape()
    {
        using var host = CreateHostWithFakeWorker();
        typeof(EngineHost).GetProperty(nameof(EngineHost.IsPlaying))!
            .SetValue(host, true);
        host.InvalidateDialogueHistory();
        host.PropertyChanged += (_, _) => throw new InvalidOperationException("boom");

        Exception? escaped = Capture(() => InvokeOnTick(host));

        Assert.Null(escaped);
        Assert.True(host.IsPlaying);
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

    private static void InvokeOnTick(EngineHost host)
    {
        try
        {
            typeof(EngineHost).GetMethod("OnTick", BindingFlags.NonPublic | BindingFlags.Instance)!
                .Invoke(host, new object?[] { null, EventArgs.Empty });
        }
        catch (TargetInvocationException ex)
        {
            ExceptionDispatchInfo.Capture(ex.InnerException ?? ex).Throw();
        }
    }

    private static Exception? Capture(Action action)
    {
        try
        {
            action();
            return null;
        }
        catch (Exception ex)
        {
            return ex;
        }
    }
}
