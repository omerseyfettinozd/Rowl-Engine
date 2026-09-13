using System;
using Avalonia.Threading;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Owns a single Avalonia dispatcher timer behind UI-thread scheduling
/// policy kept by the caller. Debounce mode (autoReset: false) stops before
/// invoking the callback; periodic mode keeps firing until stopped.
/// Restart ordering is always stop-then-start, matching prior call sites.
/// </summary>
internal sealed class EditorUiTimer : IDisposable
{
    private readonly DispatcherTimer _timer;
    private readonly bool _autoReset;
    private bool _disposed;

    internal EditorUiTimer(TimeSpan interval, Action onTick, bool autoReset = false)
    {
        _autoReset = autoReset;
        _timer = new DispatcherTimer { Interval = interval };
        _timer.Tick += (_, _) =>
        {
            if (!_autoReset) _timer.Stop();
            onTick();
        };
    }

    internal TimeSpan Interval
    {
        get => _timer.Interval;
        set => _timer.Interval = value;
    }

    internal bool IsEnabled => _timer.IsEnabled;

    internal void Start()
    {
        _timer.Stop();
        _timer.Start();
    }

    internal void Stop() => _timer.Stop();

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _timer.Stop();
    }
}
