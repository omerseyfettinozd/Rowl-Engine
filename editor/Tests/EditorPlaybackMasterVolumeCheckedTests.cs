using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

// D16 — playback-transport + master-gain Checked twin kilidi.
//
// Kapsam (kohezif alt-küme, 8 twin): IsPaused / SetPaused / SetPlayState /
// IsRunning / SetMasterVolume / GetMasterVolume + W8-c SetFadeCurve /
// GetFadeCurve. Legacy P/Invoke formlar ABI için durur; fail-closed
// c_api.h'ye dokunulmaz (DN düşer, X D kalır).
//
// RED-1 (fail-closed): legacy int/void imzaların ölü handle'da fail-closed
// karşılığı YOKTUR; Checked twin IntPtr.Zero'da native'e dokunmadan
// InvalidHandle döner (native-free, deterministik).
// RED-2 (sayım-kilidi): köprüde 211 DllImport = 57 ResultCode + 154 legacy;
// Checked kapsama 8'de sabitlenir (kalan 148 kilitlidir).
[Collection("StaticRootSequential")]
public sealed class EditorPlaybackMasterVolumeCheckedTests
{
    // ── RED-1: ölü-handle fail-closed (native-free) ──────────────────────

    [Fact]
    public void D16_Zero_IsPaused_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.IsPausedChecked(IntPtr.Zero, out bool paused);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"D16: IsPausedChecked(Zero) must be InvalidHandle, got {rc}");
        if (paused)
            throw new Exception("D16: IsPausedChecked(Zero) must default paused=false");
    }

    [Fact]
    public void D16_Zero_SetPaused_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.SetPausedChecked(IntPtr.Zero, true);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"D16: SetPausedChecked(Zero) must be InvalidHandle, got {rc}");
    }

    [Fact]
    public void D16_Zero_SetPlayState_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.SetPlayStateChecked(IntPtr.Zero, true);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"D16: SetPlayStateChecked(Zero) must be InvalidHandle, got {rc}");
    }

    [Fact]
    public void D16_Zero_IsRunning_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.IsRunningChecked(IntPtr.Zero, out bool running);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"D16: IsRunningChecked(Zero) must be InvalidHandle, got {rc}");
        if (running)
            throw new Exception("D16: IsRunningChecked(Zero) must default running=false");
    }

    [Fact]
    public void D16_Zero_SetMasterVolume_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.SetMasterVolumeChecked(IntPtr.Zero, 0.5f);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"D16: SetMasterVolumeChecked(Zero) must be InvalidHandle, got {rc}");
    }

    [Fact]
    public void D16_Zero_GetMasterVolume_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.GetMasterVolumeChecked(IntPtr.Zero, out float volume);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"D16: GetMasterVolumeChecked(Zero) must be InvalidHandle, got {rc}");
        if (volume != 0.0f)
            throw new Exception("D16: GetMasterVolumeChecked(Zero) must default volume=0");
    }

    [Fact]
    public void D16_SetMasterVolume_NonFinite_ReturnsInvalidArgument()
    {
        // Bogus-nonzero handle: guard native ÖNCESİ keser (native'e
        // dokunulmaz — D01 bogus-handle deseni, managed-önkoşul sürümü).
        foreach (float bad in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity })
        {
            var rc = NativeBridgeChecked.SetMasterVolumeChecked((IntPtr)0x1234, bad);
            if (rc != NativeBridge.ResultCode.InvalidArgument)
                throw new Exception($"D16: SetMasterVolumeChecked({bad}) must be InvalidArgument, got {rc}");
        }
    }

    // ── RED-2: sayım-kilidi ──────────────────────────────────────────────

    [Fact]
    public void D16_LegacyCount_Locked_154_Of_209()
    {
        var bridgeMethods = typeof(NativeBridge)
            .GetMethods(BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
            .Where(m => m.GetCustomAttributes(typeof(DllImportAttribute), false).Length > 0)
            .ToList();
        int total = bridgeMethods.Count;
        int resultCode = bridgeMethods.Count(m => m.ReturnType == typeof(NativeBridge.ResultCode));
        int legacy = total - resultCode;
        if (total != 211 || resultCode != 57 || legacy != 154)
            throw new Exception($"D16: bridge census must be 211=57+154, got {total}={resultCode}+{legacy}");

        var covered = typeof(NativeBridgeChecked)
            .GetMethods(BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
            .Where(m => m.ReturnType == typeof(NativeBridge.ResultCode))
            .ToList();
        if (covered.Count != 8)
            throw new Exception($"D16: Checked coverage must be 8, got {covered.Count}");

        var legacyNames = bridgeMethods.Select(m => m.Name).ToHashSet(StringComparer.Ordinal);
        foreach (var m in covered)
        {
            string stem = m.Name.EndsWith("Checked", StringComparison.Ordinal)
                ? m.Name.Substring(0, m.Name.Length - "Checked".Length)
                : m.Name;
            if (!legacyNames.Any(n => n.EndsWith("_" + stem, StringComparison.Ordinal)))
                throw new Exception($"D16: {m.Name} has no legacy RowlEngine_* twin");
        }
    }

    // ── GREEN: canlı-handle pozitif yol (worker-dispatch, owner-thread) ──

    private static EngineHost CreateLiveHost()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        var host = new EngineHost();
        if (!host.Initialize(64, 64, false))
            throw new Exception("D16: failed to init offscreen host");
        if (host.Runtime == null)
            throw new Exception("D16: live host must expose a runtime worker");
        return host;
    }

    [Fact]
    public void D16_Live_CheckedTwins_ReturnOk()
    {
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;

        if (runtime.Invoke(h => NativeBridgeChecked.SetPlayStateChecked(h, false))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live SetPlayStateChecked must be Ok");
        if (runtime.Invoke(h => NativeBridgeChecked.SetPausedChecked(h, false))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live SetPausedChecked must be Ok");
        if (runtime.Invoke(h => NativeBridgeChecked.IsPausedChecked(h, out _))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live IsPausedChecked must be Ok");
        if (runtime.Invoke(h => NativeBridgeChecked.IsRunningChecked(h, out _))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live IsRunningChecked must be Ok");
        if (runtime.Invoke(h => NativeBridgeChecked.SetMasterVolumeChecked(h, 0.5f))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live SetMasterVolumeChecked must be Ok");
        var rc = runtime.Invoke(h => NativeBridgeChecked.GetMasterVolumeChecked(h, out float got));
        if (rc != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live GetMasterVolumeChecked must be Ok");
    }

    [Fact]
    public void D16_Live_MasterVolume_RoundTrip_And_NaNPreserves()
    {
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;

        if (runtime.Invoke(h => NativeBridgeChecked.SetMasterVolumeChecked(h, 0.5f))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live set 0.5 must be Ok");
        float v = runtime.Invoke(h =>
        {
            if (NativeBridgeChecked.GetMasterVolumeChecked(h, out float got)
                != NativeBridge.ResultCode.Ok)
                throw new Exception("D16: live get must be Ok");
            return got;
        });
        if (Math.Abs(v - 0.5f) > 1e-6f)
            throw new Exception($"D16: master round-trip failed, got {v}");

        if (runtime.Invoke(h => NativeBridgeChecked.SetMasterVolumeChecked(h, float.NaN))
            != NativeBridge.ResultCode.InvalidArgument)
            throw new Exception("D16: live NaN must be InvalidArgument");
        float after = runtime.Invoke(h =>
        {
            NativeBridgeChecked.GetMasterVolumeChecked(h, out float got);
            return got;
        });
        if (Math.Abs(after - 0.5f) > 1e-6f)
            throw new Exception($"D16: NaN must preserve master volume, got {after}");

        if (runtime.Invoke(h => NativeBridgeChecked.SetMasterVolumeChecked(h, 5.0f))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("D16: live finite clamp set must be Ok");
        float clamped = runtime.Invoke(h =>
        {
            NativeBridgeChecked.GetMasterVolumeChecked(h, out float got);
            return got;
        });
        if (Math.Abs(clamped - 1.0f) > 1e-6f)
            throw new Exception($"D16: finite set must clamp to 1, got {clamped}");
    }
}
