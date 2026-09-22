using System;
using System.Collections.Generic;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

// D01 (#135) gömülü-yol fail-closed kilidi.
//
// RED (pre-fix) kanıtı: EngineHost.InitializeEmbedded((IntPtr)0x1234, ...)
// handle'ı düşürüp offscreen Initialize'a giriyordu (fail-open) —
// Assert.False(ok) FAILED/exit 1 veriyordu.
//
// GREEN sözleşmesi (EngineHost.InitializeEmbedded İMZASI + eski yolu
// DONDURULMUŞTUR — offscreen-fallback olarak kalır; yeni gömülü-yol
// EmbeddedRuntimeBootstrap + EmbeddedBridgeGuard üzerinden akar):
//  1. Legacy pin: Zero ve nonzero handle da offscreen fallback'a düşer
//     (davranış değişikliği yok — sessiz migrasyon engeli).
//  2. Guard yönlendirme tablosu (native'siz, deterministik).
//  3. Bootstrap + kayıtlı sahte-köprü: bogus-nonzero reddinde Init
//     ÇAĞRILMAZ (false + LastError); kabulde SetExternal→Init sıralanır;
//     Zero'da SetExternal hiç çağrılmaz; Wayland'da native'e dokunulmaz.
[Collection("StaticRootSequential")]
public sealed class EmbeddedHandleRedProbeTests
{
    private sealed class RecordingBridge : IEmbeddedNativeBridge
    {
        internal readonly List<string> Calls = new();
        internal Func<IntPtr, IntPtr, uint, uint, NativeBridge.ResultCode>? SetExternalImpl;
        internal Func<IntPtr, uint, uint, int, int>? InitImpl;

        public NativeBridge.ResultCode SetExternalWindowHandleChecked(
            IntPtr handle, IntPtr nativeWindowHandle, uint width, uint height)
        {
            Calls.Add("SetExternal");
            return SetExternalImpl?.Invoke(handle, nativeWindowHandle, width, height)
                ?? NativeBridge.ResultCode.Ok;
        }

        public int Init(IntPtr handle, uint width, uint height, int vsync)
        {
            Calls.Add("Init");
            return InitImpl?.Invoke(handle, width, height, vsync) ?? 1;
        }

        public string GetLastError(IntPtr handle) => "[0] none: ok";
    }

    private static OffscreenRuntimeWorker FakeWorker()
        => new(createHandle: () => (IntPtr)0xBEEF, destroyHandle: _ => { });

    /// <summary>
    /// Gömülebilir-platform simülasyonu: Wayland sinyallerini söküp
    /// EmbeddedAttempt dalını deterministik açar (shim köprü native'e
    /// değmediği için gerçek platform fark etmez). Finally ile restore.
    /// </summary>
    private sealed class X11Simulation : IDisposable
    {
        private readonly string? _savedSession = Environment.GetEnvironmentVariable("XDG_SESSION_TYPE");
        private readonly string? _savedDisplay = Environment.GetEnvironmentVariable("WAYLAND_DISPLAY");

        internal X11Simulation()
        {
            Environment.SetEnvironmentVariable("XDG_SESSION_TYPE", "x11");
            Environment.SetEnvironmentVariable("WAYLAND_DISPLAY", null);
        }

        public void Dispose()
        {
            Environment.SetEnvironmentVariable("XDG_SESSION_TYPE", _savedSession);
            Environment.SetEnvironmentVariable("WAYLAND_DISPLAY", _savedDisplay);
        }
    }

    [Fact]
    public void InitializeEmbedded_MustRouteNativeHandleThroughSetExternal()
    {
        // Legacy pin (dondurulmuş davranış): EngineHost.InitializeEmbedded
        // her handle'ı offscreen fallback'a düşürür. Yeni gömülü-yol
        // EmbeddedRuntimeBootstrap'tadır (aşağıdaki testler); bu test eski
        // yolun sessizce değişmediğini kilitler.
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        if (!host.InitializeEmbedded(IntPtr.Zero, 64, 64, false))
            throw new Exception("D01: legacy Zero offscreen fallback must stay alive");
        if (!host.IsInitialized)
            throw new Exception("D01: legacy Zero must produce a live offscreen engine");
    }

    [Fact]
    public void Guard_ZeroFallsBackToOffscreen()
    {
        if (EmbeddedBridgeGuard.Classify(IntPtr.Zero, 1280, 720) != EmbeddedRoute.OffscreenFallback)
            throw new Exception("D01: Zero handle must route to offscreen fallback");
    }

    [Fact]
    public void Guard_NonzeroNeverThrowsAndNeverIgnores()
    {
        // Bogus-nonzero ya gömme-denemesi ya fail-closed olur; offscreen'e
        // sessiz düşme (fail-open) yasaktır.
        var route = EmbeddedBridgeGuard.Classify((IntPtr)0x1234, 1280, 720);
        if (route == EmbeddedRoute.OffscreenFallback)
            throw new Exception("D01: nonzero handle must never silently fall back to offscreen");
    }

    [Fact]
    public void Guard_ZeroDimsFailClosed()
    {
        if (EmbeddedBridgeGuard.Classify((IntPtr)0x1234, 0, 720) != EmbeddedRoute.FailClosed)
            throw new Exception("D01: nonzero handle with zero dims must fail closed");
        if (string.IsNullOrWhiteSpace(EmbeddedBridgeGuard.DescribeRejection((IntPtr)0x1234, 0, 720)))
            throw new Exception("D01: rejection must carry a LastError text");
    }

    [Fact]
    public void Bootstrap_BogusNonzeroRejectedByBridgeMeansNoInit()
    {
        // Fail-closed: köprü reddinde Init ÇAĞRILMAZ, false + LastError.
        using var _ = new X11Simulation();
        var bridge = new RecordingBridge
        {
            SetExternalImpl = (_, hwnd, _, _) =>
                hwnd == (IntPtr)0x1234
                    ? NativeBridge.ResultCode.InvalidArgument
                    : NativeBridge.ResultCode.Ok,
        };
        using var bootstrap = new EmbeddedRuntimeBootstrap();
        bool ok = bootstrap.InitializeEmbedded((IntPtr)0x1234, 1280, 720, false, bridge, FakeWorker);

        if (ok)
            throw new Exception("D01: rejected bogus handle must return false");
        if (bridge.Calls.Count != 1 || bridge.Calls[0] != "SetExternal")
            throw new Exception($"D01: only SetExternal may run before rejection (calls: {string.Join(",", bridge.Calls)})");
        if (string.IsNullOrWhiteSpace(bootstrap.LastError))
            throw new Exception("D01: rejection must set LastError");
        if (bootstrap.Worker != null)
            throw new Exception("D01: rejected bootstrap must not hold a live worker");
    }

    [Fact]
    public void Bootstrap_NonzeroAcceptedOrdersSetExternalBeforeInit()
    {
        using var _ = new X11Simulation();
        var bridge = new RecordingBridge();
        using var bootstrap = new EmbeddedRuntimeBootstrap();
        bool ok = bootstrap.InitializeEmbedded((IntPtr)0x5678, 1280, 720, false, bridge, FakeWorker);

        if (!ok)
            throw new Exception($"D01: accepted handle must init (LastError: {bootstrap.LastError})");
        if (bridge.Calls.Count != 2 || bridge.Calls[0] != "SetExternal" || bridge.Calls[1] != "Init")
            throw new Exception($"D01: order must be SetExternal→Init (calls: {string.Join(",", bridge.Calls)})");
        if (bootstrap.Worker == null)
            throw new Exception("D01: accepted bootstrap must hold a live worker");
    }

    [Fact]
    public void Bootstrap_ZeroSkipsSetExternalAndInitsOffscreen()
    {
        var bridge = new RecordingBridge();
        using var bootstrap = new EmbeddedRuntimeBootstrap();
        bool ok = bootstrap.InitializeEmbedded(IntPtr.Zero, 64, 64, false, bridge, FakeWorker);

        if (!ok)
            throw new Exception($"D01: Zero offscreen fallback must init (LastError: {bootstrap.LastError})");
        if (bridge.Calls.Count != 1 || bridge.Calls[0] != "Init")
            throw new Exception($"D01: Zero must call only Init (calls: {string.Join(",", bridge.Calls)})");
    }

    [Fact]
    public void Bootstrap_ZeroDimsTouchesNoNative()
    {
        var bridge = new RecordingBridge();
        using var bootstrap = new EmbeddedRuntimeBootstrap();
        bool ok = bootstrap.InitializeEmbedded((IntPtr)0x1234, 0, 720, false, bridge, FakeWorker);

        if (ok)
            throw new Exception("D01: zero-dims must return false");
        if (bridge.Calls.Count != 0)
            throw new Exception("D01: FailClosed route must make zero native calls");
        if (string.IsNullOrWhiteSpace(bootstrap.LastError))
            throw new Exception("D01: FailClosed route must set LastError");
    }

    [Fact]
    public void Bootstrap_WaylandNeverAttemptsEmbed()
    {
        // Wayland'da gerçek HWND/X11 handle üretilemez: EmbeddedAttempt'e
        // girilmez, native'e dokunulmaz (window.cpp:427 fail-closed korunur).
        string? savedSession = Environment.GetEnvironmentVariable("XDG_SESSION_TYPE");
        string? savedDisplay = Environment.GetEnvironmentVariable("WAYLAND_DISPLAY");
        try
        {
            Environment.SetEnvironmentVariable("XDG_SESSION_TYPE", "wayland");
            Environment.SetEnvironmentVariable("WAYLAND_DISPLAY", null);
            if (EmbeddedBridgeGuard.Classify((IntPtr)0x1234, 1280, 720) != EmbeddedRoute.FailClosed)
                throw new Exception("D01: Wayland nonzero must classify FailClosed");

            var bridge = new RecordingBridge();
            using var bootstrap = new EmbeddedRuntimeBootstrap();
            bool ok = bootstrap.InitializeEmbedded((IntPtr)0x1234, 1280, 720, false, bridge, FakeWorker);
            if (ok)
                throw new Exception("D01: Wayland nonzero must return false");
            if (bridge.Calls.Count != 0)
                throw new Exception("D01: Wayland route must make zero native calls");
        }
        finally
        {
            Environment.SetEnvironmentVariable("XDG_SESSION_TYPE", savedSession);
            Environment.SetEnvironmentVariable("WAYLAND_DISPLAY", savedDisplay);
        }
    }

    [Fact]
    public void Bootstrap_InitFailureReleasesWorker()
    {
        var bridge = new RecordingBridge { InitImpl = (_, _, _, _) => 0 };
        using var bootstrap = new EmbeddedRuntimeBootstrap();
        bool ok = bootstrap.InitializeEmbedded(IntPtr.Zero, 64, 64, false, bridge, FakeWorker);

        if (ok)
            throw new Exception("D01: failed Init must return false");
        if (bootstrap.Worker != null)
            throw new Exception("D01: failed Init must not leak a worker");
        if (string.IsNullOrWhiteSpace(bootstrap.LastError))
            throw new Exception("D01: failed Init must set LastError");
    }
}
