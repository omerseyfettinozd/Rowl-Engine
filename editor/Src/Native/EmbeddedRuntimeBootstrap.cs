using System;

namespace RowlEngine.Editor.Native
{
    /// <summary>
    /// D01 (#135) — gömülü-yol native köprü seam'i. Üretim karşılığı
    /// <see cref="WorkerEmbeddedNativeBridge"/> (owner-thread dispatch);
    /// testler kayıtlı sahte-köprü (recording shim) enjekte eder.
    /// </summary>
    internal interface IEmbeddedNativeBridge
    {
        NativeBridge.ResultCode SetExternalWindowHandleChecked(
            IntPtr handle, IntPtr nativeWindowHandle, uint width, uint height);
        int Init(IntPtr handle, uint width, uint height, int vsync);
        string GetLastError(IntPtr handle);
    }

    /// <summary>
    /// Üretim köprüsü: her çağrıyı worker (owner thread) üzerinden
    /// NativeBridge'e taşır. UI thread'inden direkt P/Invoke YOK
    /// (yabancı-thread → fail-closed olurdu).
    /// </summary>
    internal sealed class WorkerEmbeddedNativeBridge : IEmbeddedNativeBridge
    {
        private readonly OffscreenRuntimeWorker _worker;

        internal WorkerEmbeddedNativeBridge(OffscreenRuntimeWorker worker)
        {
            ArgumentNullException.ThrowIfNull(worker);
            _worker = worker;
        }

        public NativeBridge.ResultCode SetExternalWindowHandleChecked(
            IntPtr handle, IntPtr nativeWindowHandle, uint width, uint height)
            => _worker.Invoke(_ => NativeBridge.RowlEngine_SetExternalWindowHandleChecked(
                handle, nativeWindowHandle, width, height));

        public int Init(IntPtr handle, uint width, uint height, int vsync)
            => _worker.Invoke(_ => NativeBridge.RowlEngine_Init(handle, width, height, vsync));

        public string GetLastError(IntPtr handle)
            => _worker.Invoke(h =>
            {
                int code = NativeBridge.RowlEngine_GetLastResultCode(h);
                string op = NativeBridge.PtrToString(
                    NativeBridge.RowlEngine_GetLastResultOperationWithLength(h, out uint opLen), opLen);
                string message = NativeBridge.PtrToString(
                    NativeBridge.RowlEngine_GetLastResultMessageWithLength(h, out uint msgLen), msgLen);
                return $"[{code}] {op}: {message}";
            });
    }

    /// <summary>
    /// D01 (#135) — gömülü-yol fail-closed facade. EngineHost'un eski
    /// <c>InitializeEmbedded</c> imzası dondurulmuştur (offscreen-fallback);
    /// XAML/code-behind gömme-istekleri buraya taşınır (geçiş checklist'i
    /// rapordadır).
    ///
    /// Sıralama (nonzero handle): worker yarat → worker üzerinden
    /// Checked-SetExternal (pre-Init) → Ok ise Init. Checked != Ok ise Init
    /// ÇAĞRILMAZ (false + LastError). FailClosed rotasında hiçbir native
    /// çağrı yapılmaz. Throw yok: worker-start hatası dahil her ret
    /// false + LastError'dır.
    /// </summary>
    internal sealed class EmbeddedRuntimeBootstrap : IDisposable
    {
        internal OffscreenRuntimeWorker? Worker { get; private set; }
        internal string LastError { get; private set; } = string.Empty;

        private bool _disposed;

        internal bool InitializeEmbedded(
            IntPtr nativeWindowHandle,
            uint width,
            uint height,
            bool vsync = true,
            IEmbeddedNativeBridge? bridge = null,
            Func<OffscreenRuntimeWorker>? workerFactory = null)
        {
            if (_disposed)
            {
                LastError = "Embedded init rejected: bootstrap is disposed.";
                return false;
            }
            if (Worker != null)
            {
                LastError = "Embedded init rejected: bootstrap already holds a live worker (double-init refused).";
                return false;
            }

            EmbeddedRoute route = EmbeddedBridgeGuard.Classify(nativeWindowHandle, width, height);
            if (route == EmbeddedRoute.FailClosed)
            {
                LastError = EmbeddedBridgeGuard.DescribeRejection(nativeWindowHandle, width, height);
                return false;
            }

            OffscreenRuntimeWorker worker;
            try
            {
                worker = workerFactory != null ? workerFactory() : new OffscreenRuntimeWorker();
            }
            catch (Exception ex)
            {
                LastError = $"Embedded init rejected: offscreen runtime worker could not start ({ex.Message}).";
                return false;
            }
            Worker = worker;

            IEmbeddedNativeBridge nativeBridge = bridge ?? new WorkerEmbeddedNativeBridge(worker);

            if (route == EmbeddedRoute.EmbeddedAttempt)
            {
                NativeBridge.ResultCode checkedCode;
                try
                {
                    checkedCode = nativeBridge.SetExternalWindowHandleChecked(
                        worker.Handle, nativeWindowHandle, width, height);
                }
                catch (Exception ex)
                {
                    LastError = $"Embedded init rejected: SetExternal bridge threw ({ex.Message}); Init not attempted.";
                    DisposeWorker();
                    return false;
                }
                if (checkedCode != NativeBridge.ResultCode.Ok)
                {
                    LastError = $"Embedded init rejected: SetExternalWindowHandleChecked returned {checkedCode}; Init not attempted.";
                    DisposeWorker();
                    return false;
                }
            }

            int initResult;
            try
            {
                initResult = nativeBridge.Init(worker.Handle, width, height, vsync ? 1 : 0);
            }
            catch (Exception ex)
            {
                LastError = $"Embedded init failed: Init bridge threw ({ex.Message}).";
                DisposeWorker();
                return false;
            }
            if (initResult == 0)
            {
                string detail;
                try
                {
                    detail = nativeBridge.GetLastError(worker.Handle);
                }
                catch (Exception ex)
                {
                    detail = $"last-error unreadable ({ex.Message})";
                }
                LastError = $"Embedded init failed: RowlEngine_Init returned 0 ({detail}).";
                DisposeWorker();
                return false;
            }

            LastError = string.Empty;
            return true;
        }

        private void DisposeWorker()
        {
            OffscreenRuntimeWorker? worker = Worker;
            Worker = null;
            try
            {
                worker?.Dispose();
            }
            catch
            {
                // Fail-closed: dispose yolu asla fırlatmaz.
            }
        }

        public void Dispose()
        {
            if (_disposed)
                return;
            _disposed = true;
            DisposeWorker();
        }
    }
}
