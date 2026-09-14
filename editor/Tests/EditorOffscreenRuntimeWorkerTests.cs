using System;
using System.Collections.Concurrent;
using System.Threading;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

public sealed class EditorOffscreenRuntimeWorkerTests
{
    [Fact]
    public void Worker_OwnsHandleAndSerializesCallsOnOneDedicatedThread()
    {
        int callerThread = Environment.CurrentManagedThreadId;
        int createThread = 0;
        int destroyThread = 0;
        var callThreads = new ConcurrentBag<int>();

        using (var worker = new OffscreenRuntimeWorker(
            createHandle: () =>
            {
                createThread = Environment.CurrentManagedThreadId;
                return new IntPtr(1234);
            },
            destroyHandle: _ => destroyThread = Environment.CurrentManagedThreadId))
        {
            if (worker.ManagedThreadId == callerThread || worker.ManagedThreadId != createThread)
                throw new Exception("Offscreen handle was not created on a dedicated worker thread.");

            int first = worker.Invoke(handle =>
            {
                callThreads.Add(Environment.CurrentManagedThreadId);
                return handle.ToInt32();
            });
            worker.Invoke(_ => callThreads.Add(Environment.CurrentManagedThreadId));

            if (first != 1234 || callThreads.Count != 2)
                throw new Exception("Worker did not execute both serialized commands.");
            foreach (int threadId in callThreads)
            {
                if (threadId != createThread)
                    throw new Exception("A native command escaped the owning worker thread.");
            }
        }

        if (destroyThread != createThread)
            throw new Exception("Offscreen handle was not destroyed on its owner thread.");
    }

    [Fact]
    public void Worker_DrainsPostedWorkBeforeOwnerThreadDestruction()
    {
        using var reached = new ManualResetEventSlim(false);
        int workerThread = 0;
        int destroyThread = 0;
        var worker = new OffscreenRuntimeWorker(
            createHandle: () =>
            {
                workerThread = Environment.CurrentManagedThreadId;
                return new IntPtr(7);
            },
            destroyHandle: _ => destroyThread = Environment.CurrentManagedThreadId);

        if (!worker.TryPost(_ => reached.Set()))
            throw new Exception("Worker rejected a command while available.");
        worker.Dispose();

        if (!reached.IsSet || destroyThread != workerThread || worker.TryPost(_ => { }))
            throw new Exception("Worker did not drain safely before owner-thread destruction.");
    }
}
