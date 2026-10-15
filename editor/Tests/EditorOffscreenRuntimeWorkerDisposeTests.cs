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
    public async Task Worker_NativeCallStall_TimesOutAndRejectsFurtherCommands()
    {
        using var entered = new ManualResetEventSlim(false);
        using var release = new ManualResetEventSlim(false);
        using var destroyed = new ManualResetEventSlim(false);
        using var worker = new OffscreenRuntimeWorker(
            createHandle: () => new IntPtr(7),
            destroyHandle: _ => destroyed.Set());
        try
        {
            // Takılan çağrı adanmış threadde bekler: Task.Run paylaşılan
            // havuza bağlıydı; paralel testler havuzu doldurunca komut 2 sn
            // içinde hiç kuyruğa girmiyordu (Windows nightly, 13-14 Eki).
            var pending = Task.Factory.StartNew(() => worker.Invoke(_ =>
            {
                entered.Set();
                release.Wait();
                return 1;
            }), CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
            Assert.True(entered.Wait(TimeSpan.FromSeconds(2)));
            await Assert.ThrowsAsync<TimeoutException>(async () => await pending);
            Assert.False(worker.IsAvailable);
            Assert.False(worker.TryPost(_ => { }));
        }
        finally
        {
            release.Set();
        }
        Assert.True(destroyed.Wait(TimeSpan.FromSeconds(2)));
    }

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
