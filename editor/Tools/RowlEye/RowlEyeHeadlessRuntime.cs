using System;
using System.Threading;
using Avalonia.Threading;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tools.RowlEye;

/// <summary>
/// Dedicated cross-platform headless Avalonia UI thread with continuous MainLoop pumping.
/// Ensures deadlock-free Dispatcher.UIThread.InvokeAsync calls from any thread (ThreadPool, HttpListener, etc.).
/// </summary>
public static class RowlEyeHeadlessRuntime
{
    private static readonly object SyncRoot = new();
    private static Thread? _uiThread;
    private static CancellationTokenSource? _cts;
    private static readonly ManualResetEventSlim _ready = new(false);
    private static bool _initialized;

    public static void EnsureInitialized()
    {
        if (_initialized) return;

        lock (SyncRoot)
        {
            if (_initialized) return;

            NativeEnvironment.EnsureDisplayFreeDrivers();

            _cts = new CancellationTokenSource();
            _ready.Reset();

            _uiThread = new Thread(() =>
            {
                try
                {
                    if (Avalonia.Application.Current == null)
                    {
                        Program.BuildAvaloniaAppHeadless().SetupWithoutStarting();
                    }

                    _ready.Set();

                    while (!_cts.Token.IsCancellationRequested)
                    {
                        try
                        {
                            Dispatcher.UIThread.RunJobs();
                        }
                        catch { }
                        Thread.Sleep(5);
                    }
                }
                catch { }
            })
            {
                IsBackground = true,
                Name = "RowlEye-HeadlessUI"
            };

            if (OperatingSystem.IsWindows())
            {
                try
                {
                    _uiThread.SetApartmentState(ApartmentState.STA);
                }
                catch { }
            }

            _uiThread.Start();
            bool signaled = _ready.Wait(TimeSpan.FromSeconds(15));
            if (!signaled)
            {
                throw new TimeoutException("RowlEyeHeadlessRuntime failed to initialize Avalonia UI thread within 15 seconds.");
            }

            _initialized = true;
        }
    }
}
