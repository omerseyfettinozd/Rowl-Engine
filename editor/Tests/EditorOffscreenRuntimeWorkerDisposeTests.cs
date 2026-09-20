using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Kod-inceleme bulgusu #8 — worker threadi üzerinden çağrılan Dispose,
/// komut kuyusunu numaralandırıcı hâlâ canlıyken dispose ediyordu;
/// sonraki MoveNext ObjectDisposedException fırlatıp worker threadini
/// deviriyordu. Artık kuyunun dispose'u Run bitimine ertelenir.
/// </summary>
public sealed class EditorOffscreenRuntimeWorkerDisposeTests
{
    [Fact]
    public void Worker_SelfDisposeFromQueuedCommand_DrainsGracefully()
    {
        int destroyCount = 0;
        using var destroyed = new ManualResetEventSlim(false);
        using var worker = new OffscreenRuntimeWorker(
            createHandle: () => new IntPtr(7),
            destroyHandle: _ =>
            {
                Interlocked.Increment(ref destroyCount);
                destroyed.Set();
            });

        Assert.True(worker.TryPost(handle => worker.Dispose()));

        Assert.True(destroyed.Wait(TimeSpan.FromSeconds(5)));
        Assert.Equal(1, Volatile.Read(ref destroyCount));
    }
}
