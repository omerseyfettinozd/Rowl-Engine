using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Avalonia.Threading;
using RowlEngine.Editor.Tools.RowlEye.Ai;

namespace RowlEngine.Editor.Tools.RowlEye.Engine;

/// <summary>
/// Background watchdog monitoring Avalonia UI thread responsiveness.
/// Automatically emits CRITICAL anomaly events when the UI thread hangs or deadlocks.
/// </summary>
public sealed class RowlEyeWatchdog : IDisposable
{
    private static readonly Lazy<RowlEyeWatchdog> InstanceLazy = new(() => new RowlEyeWatchdog());
    public static RowlEyeWatchdog Instance => InstanceLazy.Value;

    private readonly object _lock = new();
    private CancellationTokenSource? _cts;
    private Task? _monitorTask;
    private long _lastHeartbeatTicks = Stopwatch.GetTimestamp();
    private bool _isHung = false;
    private int _hungCount = 0;

    /// <summary>
    /// Threshold in milliseconds after which the UI thread is considered hung (default 2500ms).
    /// </summary>
    public int HangThresholdMs { get; set; } = 2500;

    /// <summary>
    /// Checks whether the UI thread is currently deemed hung.
    /// </summary>
    public bool IsHung
    {
        get
        {
            lock (_lock) return _isHung;
        }
    }

    /// <summary>
    /// Total count of hang occurrences detected.
    /// </summary>
    public int HungCount
    {
        get
        {
            lock (_lock) return _hungCount;
        }
    }

    /// <summary>
    /// Age in milliseconds since the last successful UI thread heartbeat.
    /// </summary>
    public double LastHeartbeatAgeMs
    {
        get
        {
            long elapsedTicks = Stopwatch.GetTimestamp() - Interlocked.Read(ref _lastHeartbeatTicks);
            return (elapsedTicks * 1000.0) / Stopwatch.Frequency;
        }
    }

    /// <summary>
    /// Starts the background watchdog loop if not already running.
    /// </summary>
    public void Start(int checkIntervalMs = 500)
    {
        lock (_lock)
        {
            if (_cts != null && !_cts.IsCancellationRequested) return;

            _cts = new CancellationTokenSource();
            _lastHeartbeatTicks = Stopwatch.GetTimestamp();
            _isHung = false;
            _monitorTask = Task.Run(() => MonitorLoopAsync(checkIntervalMs, _cts.Token));
        }
    }

    /// <summary>
    /// Stops the watchdog loop.
    /// </summary>
    public void Stop()
    {
        lock (_lock)
        {
            _cts?.Cancel();
            _cts?.Dispose();
            _cts = null;
            _monitorTask = null;
        }
    }

    private async Task MonitorLoopAsync(int checkIntervalMs, CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(checkIntervalMs, ct).ConfigureAwait(false);

                // Dispatch a lightweight heartbeat on the UI thread
                _ = Dispatcher.UIThread.InvokeAsync(() =>
                {
                    Interlocked.Exchange(ref _lastHeartbeatTicks, Stopwatch.GetTimestamp());
                });

                double ageMs = LastHeartbeatAgeMs;
                if (ageMs > HangThresholdMs)
                {
                    bool shouldPublish = false;
                    lock (_lock)
                    {
                        if (!_isHung)
                        {
                            _isHung = true;
                            _hungCount++;
                            shouldPublish = true;
                        }
                    }

                    if (shouldPublish)
                    {
                        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.AnomalyDetected, new
                        {
                            category = "Stability",
                            code = "UI_THREAD_HUNG",
                            severity = "CRITICAL",
                            hangDurationMs = Math.Round(ageMs, 1),
                            thresholdMs = HangThresholdMs,
                            detail = $"Avalonia UI Dispatcher has not responded for {Math.Round(ageMs, 1)}ms (> {HangThresholdMs}ms budget)"
                        });
                    }
                }
                else
                {
                    bool shouldRecover = false;
                    lock (_lock)
                    {
                        if (_isHung)
                        {
                            _isHung = false;
                            shouldRecover = true;
                        }
                    }

                    if (shouldRecover)
                    {
                        AiTelemetryHub.Instance.Publish(AiTelemetryEventType.AnomalyDetected, new
                        {
                            category = "Stability",
                            code = "UI_THREAD_RECOVERED",
                            severity = "INFO",
                            detail = "Avalonia UI Dispatcher has resumed processing messages"
                        });
                    }
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[RowlEyeWatchdog] Error in monitor loop: {ex.Message}");
            }
        }
    }

    public void Dispose()
    {
        Stop();
    }
}
