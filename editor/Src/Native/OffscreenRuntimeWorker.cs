using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.InteropServices;
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
        private const int NativeCommandTimeoutMilliseconds = 7000;
        private readonly BlockingCollection<Action<IntPtr>> _commands = new();
        private readonly Func<IntPtr> _createHandle;
        private readonly Action<IntPtr> _destroyHandle;
        private readonly ManualResetEventSlim _started = new(false);
        private readonly Thread _thread;
        private Exception? _startupError;
        private IntPtr _handle;
        private int _disposeState;
        private int _commandsDisposed;

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
            bool completed;
            try { completed = completion.Task.Wait(NativeCommandTimeoutMilliseconds); }
            catch (AggregateException) { return completion.Task.GetAwaiter().GetResult(); }
            if (!completed)
            {
                // A native call cannot be interrupted safely. Stop accepting
                // commands; the background owner cleans up if it returns.
                Interlocked.Exchange(ref _disposeState, 1);
                try { _commands.CompleteAdding(); }
                catch (ObjectDisposedException) { }
                _started.Dispose();
                throw new TimeoutException("The offscreen native runtime did not respond within 7 seconds.");
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

        // ── B5 K-trio worker wrapper'ları ─────────────────────────────────
        //
        // TAŞINMA KURALI: marshal API burada yaşar. Adapter UI-thread'inden
        // direkt NativeBridge çağırıyordu — yabancı-thread → toEngineChecked
        // fail-closed → canlıda sessizce hiçbir şey yapmıyordu (B5-kırmızısı
        // kanıtlı). Bu wrapper'lar owner-thread'den (Invoke) çağırır; guard/
        // pre-check semantiği adapter'dakiyle birebir aynıdır (NativeGuard +
        // servis-ayna kuralları taşınır, yeniden-icat edilmez).

        // Trio-1 — mixer: ambience/UI volume, fade-curve, sfx-pool.
        internal void SetAmbienceVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v)) return;
            Invoke(handle => NativeBridge.RowlEngine_SetAmbienceVolume(handle, v));
        }

        internal void SetUiVolume(float volume)
        {
            if (!NativeGuard.TryClamp01(volume, out float v)) return;
            Invoke(handle => NativeBridge.RowlEngine_SetUiVolume(handle, v));
        }

        internal void SetFadeCurve(int curve)
        {
            if (curve != 0 && curve != 1) return;
            Invoke(handle => NativeBridge.RowlEngine_SetFadeCurve(handle, curve));
        }

        // W8-c — fade-curve loud formu: sessiz-düşüren void form korunur,
        // yanına owner-thread Invoke üzerinden Checked P/Invoke çağrısı
        // (ResultCode dönüşlü). Servis-ayna pre-check adapter'da kalır,
        // worker yalnızca marshal yapar (B5 taşınma kuralı).
        internal NativeBridge.ResultCode SetFadeCurveChecked(int curve) =>
            Invoke(handle => NativeBridgeChecked.SetFadeCurveChecked(handle, curve));

        internal NativeBridge.ResultCode GetFadeCurveChecked(out int curve)
        {
            int captured = 0;
            NativeBridge.ResultCode code = Invoke(handle =>
                NativeBridgeChecked.GetFadeCurveChecked(handle, out captured));
            curve = captured;
            return code;
        }

        internal void SetSfxPoolDepth(int depth) =>
            Invoke(handle => NativeBridge.RowlEngine_SetSfxPoolDepth(
                handle, Math.Clamp(depth, 1, 16)));

        // Trio-2 — character layers: slot yazmaları + preset + last-error.
        // Servis-ayna pre-check'ler adapter'da kalır (domain-önkoşul);
        // burada yalnızca owner-thread marshal + NativeGuard uygulanır.
        internal void SetCharacterSlotAsset(string slot, string asset) =>
            Invoke(handle => NativeBridge.RowlEngine_SetCharacterSlotAsset(handle, slot, asset));

        internal void SetCharacterSlotOpacity(string slot, float opacity)
        {
            if (!NativeGuard.TryClamp01(opacity, out float o)) return;
            Invoke(handle => NativeBridge.RowlEngine_SetCharacterSlotOpacity(handle, slot, o));
        }

        internal void SetCharacterSlotVisible(string slot, bool visible) =>
            Invoke(handle => NativeBridge.RowlEngine_SetCharacterSlotVisible(handle, slot, visible ? 1 : 0));

        internal void RegisterCharacterPreset(string name, string expressionJson) =>
            Invoke(handle => NativeBridge.RowlEngine_RegisterCharacterPreset(handle, name, expressionJson));

        internal void ApplyCharacterExpression(string name) =>
            Invoke(handle => NativeBridge.RowlEngine_ApplyCharacterExpression(handle, name));

        internal string GetLastCharacterError() =>
            Invoke(handle =>
            {
                if (NativeBridge.RowlEngine_GetLastCharacterErrorUtf8(
                        handle, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
                    required == 0)
                    return string.Empty;
                IntPtr buffer = Marshal.AllocHGlobal((int)required);
                try
                {
                    if (NativeBridge.RowlEngine_GetLastCharacterErrorUtf8(
                            handle, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                        return string.Empty;
                    return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
                }
                finally
                {
                    Marshal.FreeHGlobal(buffer);
                }
            });

        // Trio-3 — chapter/prefetch: index/load/unload/prefetch/pump +
        // caller-buffer JSON okumalar. Servis-ayna pre-check'ler (boş-JSON,
        // chapter-id geçerliliği, bütçe/pump clamp) adapter'da kalır;
        // burada yalnızca owner-thread marshal uygulanır. Sözleşme
        // docs/PREFETCH_AND_CHAPTERS_CONTRACT.md'dedir.
        internal void LoadChapterIndexJson(string indexJson) =>
            Invoke(handle => NativeBridge.RowlEngine_LoadChapterIndexJson(handle, indexJson));

        internal void AppendChapterFileJson(string chapterJson) =>
            Invoke(handle => NativeBridge.RowlEngine_AppendChapterFileJson(handle, chapterJson));

        internal void LoadChapter(string chapterId) =>
            Invoke(handle => NativeBridge.RowlEngine_LoadChapter(handle, chapterId));

        internal void UnloadChapter(string chapterId) =>
            Invoke(handle => NativeBridge.RowlEngine_UnloadChapter(handle, chapterId));

        internal void PrefetchChapterAssets(string? chapterId, ulong budgetBytes) =>
            Invoke(handle => NativeBridge.RowlEngine_PrefetchChapterAssets(handle, chapterId, budgetBytes));

        internal int PumpPrefetch(float maxMilliseconds) =>
            Math.Max(0, Invoke(handle => NativeBridge.RowlEngine_PumpPrefetch(handle, maxMilliseconds)));

        internal bool IsChapterBoundaryNode(ulong nodeId) =>
            Invoke(handle => NativeBridge.RowlEngine_IsChapterBoundaryNode(handle, nodeId) != 0);

        private delegate NativeBridge.ResultCode CallerJsonReader(
            IntPtr handle, IntPtr buffer, uint bufferSize, out uint required);

        private string ReadCallerJson(CallerJsonReader read) =>
            Invoke(handle =>
            {
                if (read(handle, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok ||
                    required == 0)
                    return string.Empty;
                IntPtr buffer = Marshal.AllocHGlobal((int)required);
                try
                {
                    if (read(handle, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                        return string.Empty;
                    return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
                }
                finally
                {
                    Marshal.FreeHGlobal(buffer);
                }
            });

        internal string GetLoadedChaptersJson() =>
            ReadCallerJson(NativeBridge.RowlEngine_GetLoadedChaptersJson);

        internal string GetPrefetchProgressJson() =>
            ReadCallerJson(NativeBridge.RowlEngine_GetPrefetchProgressJson);

        internal string GetCurrentChapterId() =>
            ReadCallerJson(NativeBridge.RowlEngine_GetCurrentChapterIdUtf8);

        internal bool TryPost(Action<IntPtr> command)
        {
            ArgumentNullException.ThrowIfNull(command);
            if (!IsAvailable) return false;
            try
            {
                return _commands.TryAdd(handle =>
                {
                    try { command(handle); }
                    catch (Exception error)
                    {
                        Debug.WriteLine($"[OffscreenRuntimeWorker] TryPost command failed: {error}");
                    }
                });
            }
            catch (InvalidOperationException)
            {
                return false;
            }
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

            if (_handle != IntPtr.Zero)
            {
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
            // Worker-thread Dispose yalnızca CompleteAdding yapar; kuyunun
            // dispose'u numaralandırıcı bittikten sonra buraya aittir.
            DisposeCommandsOnce();
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
            try
            {
                _commands.CompleteAdding();
            }
            catch (ObjectDisposedException)
            {
                // Sifir-handle'li worker Run() kuyrugu zaten kapatmistir;
                // Dispose idempotent kalir (W8-f dal-2: handlesiz worker
                // fırlatmadan duser, EngineHost.Initialize false doner).
            }
            if (Environment.CurrentManagedThreadId == ManagedThreadId)
            {
                // Worker threadi: kuyuyu numaralandırıyoruz; dispose'u
                // Run bitimine bırak, yalnızca _started'ı kapat.
                _started.Dispose();
                return;
            }
            if (!_thread.Join(NativeCommandTimeoutMilliseconds))
            {
                _started.Dispose();
                return; // Run owns queue disposal when the native call returns.
            }
            DisposeCommandsOnce();
            _started.Dispose();
        }

        private void DisposeCommandsOnce()
        {
            if (Interlocked.Exchange(ref _commandsDisposed, 1) == 0)
                _commands.Dispose();
        }
    }
}
