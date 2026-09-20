using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 4 Dilim 5 — background worker for heavy project scans (disk index,
/// batch lint, bitmap prefetch). Mirrors the
/// <see cref="Native.OffscreenRuntimeWorker"/> pattern: one dedicated owner
/// thread serializes every queued delegate, <see cref="Invoke{T}"/> blocks
/// the caller for validation results while <see cref="TryPost"/>
/// fire-and-forgets thumbnail work without ever killing the worker.
/// Standalone SDL hosts stay on the UI thread; only scan/decode work moves
/// here. Heavy builds keep their own
/// <c>ProjectBuildService.Task.Run + CancellationToken + staging-dir</c>
/// pattern and never enter this queue.
/// </summary>
internal sealed class AssetScanWorker : IDisposable
{
    private readonly BlockingCollection<Action> _commands = new();
    private readonly Thread _thread;
    private readonly ManualResetEventSlim _started = new(false);
    private int _disposeState;
    private int _commandsDisposed;

    public AssetScanWorker(string threadName = "Rowl Editor Asset Scan")
    {
        _thread = new Thread(Run)
        {
            IsBackground = true,
            Name = threadName,
        };
        _thread.Start();
        _started.Wait();
    }

    internal int ManagedThreadId { get; private set; }

    internal bool IsAvailable => Volatile.Read(ref _disposeState) == 0;

    /// <summary>
    /// Runs a scan on the owner thread and blocks for its result (used for
    /// batch validation / lint, whose result the caller must have).
    /// </summary>
    internal T Invoke<T>(Func<T> command)
    {
        ArgumentNullException.ThrowIfNull(command);
        ThrowIfUnavailable();
        if (Environment.CurrentManagedThreadId == ManagedThreadId)
            return command();

        var completion = new TaskCompletionSource<T>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            _commands.Add(() =>
            {
                try { completion.SetResult(command()); }
                catch (Exception error) { completion.SetException(error); }
            });
        }
        catch (InvalidOperationException)
        {
            throw new ObjectDisposedException(nameof(AssetScanWorker));
        }
        return completion.Task.GetAwaiter().GetResult();
    }

    internal void Invoke(Action command)
    {
        ArgumentNullException.ThrowIfNull(command);
        Invoke(() =>
        {
            command();
            return true;
        });
    }

    /// <summary>
    /// Queues fire-and-forget work (thumbnail prefetch). A failing delegate
    /// is recorded to debug output; the worker always stays alive.
    /// </summary>
    internal bool TryPost(Action command)
    {
        ArgumentNullException.ThrowIfNull(command);
        if (!IsAvailable)
            return false;
        return _commands.TryAdd(() =>
        {
            try
            {
                command();
            }
            catch (Exception error)
            {
                Debug.WriteLine($"[AssetScanWorker] TryPost command failed: {error}");
            }
        });
    }

    /// <summary>
    /// Prefetches one asset bitmap off the UI thread. Decode failures are
    /// contained by <see cref="TryPost"/> and never surface.
    /// </summary>
    internal bool PrefetchBitmap(string? assetPath)
    {
        if (string.IsNullOrWhiteSpace(assetPath))
            return false;
        return TryPost(() => AssetBitmapCache.GetOrLoad(assetPath));
    }

    private void Run()
    {
        ManagedThreadId = Environment.CurrentManagedThreadId;
        _started.Set();
        foreach (Action command in _commands.GetConsumingEnumerable())
            command();
        // Worker-thread Dispose yalnızca CompleteAdding yapar; kuyunun
        // dispose'u numaralandırıcı bittikten sonra buraya aittir.
        DisposeCommandsOnce();
    }

    private void ThrowIfUnavailable()
    {
        if (!IsAvailable)
            throw new ObjectDisposedException(nameof(AssetScanWorker));
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposeState, 1) != 0)
            return;
        _commands.CompleteAdding();
        if (Environment.CurrentManagedThreadId == ManagedThreadId)
        {
            // Worker threadi: kuyuyu numaralandırıyoruz; dispose'u
            // Run bitimine bırak, yalnızca _started'ı kapat.
            _started.Dispose();
            return;
        }
        _thread.Join();
        DisposeCommandsOnce();
        _started.Dispose();
    }

    private void DisposeCommandsOnce()
    {
        if (Interlocked.Exchange(ref _commandsDisposed, 1) == 0)
            _commands.Dispose();
    }
}
