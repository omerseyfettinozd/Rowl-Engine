using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Native
{
    /// <summary>
    /// Owns one editor-only offscreen native handle and serializes every call
    /// onto the thread that created it. Standalone/visible SDL runtimes do not
    /// use this service and remain owned by their host UI/event thread.
    /// </summary>
    internal sealed class OffscreenRuntimeWorker : IDisposable
    {
        private readonly BlockingCollection<Action<IntPtr>> _commands = new();
        private readonly Func<IntPtr> _createHandle;
        private readonly Action<IntPtr> _destroyHandle;
        private readonly ManualResetEventSlim _started = new(false);
        private readonly Thread _thread;
        private Exception? _startupError;
        private IntPtr _handle;
        private int _disposeState;

        internal OffscreenRuntimeWorker(
            Func<IntPtr>? createHandle = null,
            Action<IntPtr>? destroyHandle = null)
        {
            _createHandle = createHandle ?? NativeBridge.RowlEngine_Create;
            _destroyHandle = destroyHandle ?? DestroyNativeHandle;
            _thread = new Thread(Run)
            {
                IsBackground = true,
                Name = "Rowl Editor Offscreen Runtime",
            };
            _thread.Start();
            _started.Wait();
            if (_startupError != null)
            {
                Dispose();
                throw new InvalidOperationException(
                    "The editor offscreen runtime worker could not start.", _startupError);
            }
        }

        internal IntPtr Handle => _handle;
        internal int ManagedThreadId { get; private set; }
        internal bool IsAvailable => Volatile.Read(ref _disposeState) == 0 && _handle != IntPtr.Zero;

        internal T Invoke<T>(Func<IntPtr, T> command)
        {
            ArgumentNullException.ThrowIfNull(command);
            ThrowIfUnavailable();
            if (Environment.CurrentManagedThreadId == ManagedThreadId)
                return command(_handle);

            var completion = new TaskCompletionSource<T>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            try
            {
                _commands.Add(handle =>
                {
                    try { completion.SetResult(command(handle)); }
                    catch (Exception error) { completion.SetException(error); }
                });
            }
            catch (InvalidOperationException)
            {
                throw new ObjectDisposedException(nameof(OffscreenRuntimeWorker));
            }
            return completion.Task.GetAwaiter().GetResult();
        }

        internal void Invoke(Action<IntPtr> command)
        {
            ArgumentNullException.ThrowIfNull(command);
            Invoke(handle =>
            {
                command(handle);
                return true;
            });
        }

        internal bool TryPost(Action<IntPtr> command)
        {
            ArgumentNullException.ThrowIfNull(command);
            if (!IsAvailable) return false;
            return _commands.TryAdd(handle =>
            {
                try { command(handle); }
                catch (Exception error)
                {
                    // Async callers own diagnostics and the worker must stay
                    // alive, but record the failure so posted commands cannot
                    // fail silently.
                    Debug.WriteLine($"[OffscreenRuntimeWorker] TryPost command failed: {error}");
                }
            });
        }

        private void Run()
        {
            ManagedThreadId = Environment.CurrentManagedThreadId;
            try
            {
                _handle = _createHandle();
            }
            catch (Exception error)
            {
                _startupError = error;
            }
            finally
            {
                _started.Set();
            }

            if (_handle == IntPtr.Zero) return;
            try
            {
                foreach (Action<IntPtr> command in _commands.GetConsumingEnumerable())
                    command(_handle);
            }
            finally
            {
                _destroyHandle(_handle);
                _handle = IntPtr.Zero;
            }
        }

        private static void DestroyNativeHandle(IntPtr handle)
        {
            NativeBridge.RowlEngine_Shutdown(handle);
            NativeBridge.RowlEngine_Destroy(handle);
        }

        private void ThrowIfUnavailable()
        {
            if (!IsAvailable)
                throw new ObjectDisposedException(nameof(OffscreenRuntimeWorker));
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposeState, 1) != 0) return;
            _commands.CompleteAdding();
            if (Environment.CurrentManagedThreadId != ManagedThreadId)
                _thread.Join();
            _commands.Dispose();
            _started.Dispose();
        }
    }
}
