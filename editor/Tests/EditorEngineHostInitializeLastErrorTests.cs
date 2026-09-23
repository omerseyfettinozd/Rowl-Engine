using System;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

// EngineHost.Initialize init-hatasinda detayli LastError kilidi.
//
// Initialize uc noktada aciklamasiz false donuyordu (worker-start,
// handlesiz-worker, Init 0). Artik her basarisiz dal LastError'u
// doldurur, basarili init bosaltir; imza bool korunur, throw yok.
// EmbeddedRuntimeBootstrap satir 134-159 emsaldir; yeni native
// P/Invoke yoktur (TryGetLastEngineResult + ClearLastResult yeniden
// kullanilir, Init 0 dali pre-clear'lidir).
[Collection("StaticRootSequential")]
public sealed class EditorEngineHostInitializeLastErrorTests
{
    [Fact]
    public void Init_Success_ClearsLastError()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        if (!host.Initialize(64, 64, false))
            throw new Exception("W8f: failed to init offscreen host");
        if (host.LastError != string.Empty)
            throw new Exception($"W8f: successful init must clear LastError, got '{host.LastError}'");
    }

    [Fact]
    public void Init_WorkerStartFailure_ReportsDetail()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        host.RuntimeFactory = () => throw new InvalidOperationException("w8-test worker boom");
        if (host.Initialize(64, 64, false))
            throw new Exception("W8f: throwing worker factory must fail init");
        if (string.IsNullOrWhiteSpace(host.LastError))
            throw new Exception("W8f: failed init must fill LastError");
        if (!host.LastError.Contains("worker", StringComparison.OrdinalIgnoreCase))
            throw new Exception($"W8f: worker-start LastError must mention worker, got '{host.LastError}'");
    }

    [Fact]
    public void Init_HandlelessWorker_ReportsDetail()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        host.RuntimeFactory = () => new OffscreenRuntimeWorker(() => IntPtr.Zero, _ => { });
        if (host.Initialize(64, 64, false))
            throw new Exception("W8f: handleless worker must fail init");
        if (string.IsNullOrWhiteSpace(host.LastError))
            throw new Exception("W8f: failed init must fill LastError");
        if (!host.LastError.Contains("worker", StringComparison.OrdinalIgnoreCase))
            throw new Exception($"W8f: handleless LastError must mention worker, got '{host.LastError}'");
    }

    [Fact]
    public void Init_NativeInitReturnsZero_ReportsDetail()
    {
        // W8-f dar-duzeltme: Init-0 dali (EngineHost.cs Init sonucu 0)
        // hicbir fact tarafindan calistirilmiyordu — satirlar silinse
        // suit yesil kaliyordu. Bu fact dali surer: kullanilabilir
        // gorunen (sifir-degil) ama olu handle + Init sessiz-0
        // (c_api_lifecycle.cpp:326 olu-handle fail-closed 0).
        NativeEnvironment.EnsureDisplayFreeDrivers();
        IntPtr handle = NativeBridge.RowlEngine_Create();
        try
        {
            NativeBridge.RowlEngine_Destroy(handle);
            using var host = new EngineHost();
            IntPtr dead = handle;
            host.RuntimeFactory = () => new OffscreenRuntimeWorker(() => dead, _ => { });
            if (host.Initialize(64, 64, false))
                throw new Exception("W8f: init on dead handle must fail");
            if (string.IsNullOrWhiteSpace(host.LastError))
                throw new Exception("W8f: Init-0 must fill LastError");
            if (!host.LastError.Contains("init", StringComparison.OrdinalIgnoreCase))
                throw new Exception($"W8f: Init-0 LastError must mention init, got '{host.LastError}'");
        }
        finally
        {
            // Olasi cift-Destroy sessiz no-op'tur (olu-handle disiplini).
            try { NativeBridge.RowlEngine_Destroy(handle); } catch { }
        }
    }
}
