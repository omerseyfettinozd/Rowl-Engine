using System;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

// W8-c — fade-curve loud-tier Checked twin kilidi.
//
// Legacy void/int SetFadeCurve/GetFadeCurve formlari ABI icin durur;
// fail-closed semantik yalnizca Checked twin'lerdedir:
//  - IntPtr.Zero → InvalidHandle, native'e DOKUNULMAZ.
//  - curve 0/1 disi → InvalidArgument, native'e DOKUNULMAZ.
//  - null out (native) → InvalidArgument (C# out int null tutamaz;
//    null yalnizca ham P/Invoke ile ifade edilir, asagidaki
//    TestOnly ham ilan test-montajindadir, kopru census'unu bozmaz).
//  - canli handle'a yabanci-thread cagrisi → native WrongThread
//    damgasi aynen yukari tasinir (yonlendirme yok).
// c_api.h satir 830-842 ile birebir; native'e dokunulmaz.
[Collection("StaticRootSequential")]
public sealed class EditorFadeCurveCheckedTests
{
    // ── RED-1: kotu-curve fail-closed (native-free, bogus handle) ─────────

    [Fact]
    public void W8c_BadCurve_BogusHandle_ReturnsInvalidArgument()
    {
        foreach (int bad in new[] { 7, -1, 2 })
        {
            var rc = NativeBridgeChecked.SetFadeCurveChecked((IntPtr)0x1234, bad);
            if (rc != NativeBridge.ResultCode.InvalidArgument)
                throw new Exception($"W8c: SetFadeCurveChecked({bad}) must be InvalidArgument, got {rc}");
        }
    }

    // ── RED-1b: olu-handle fail-closed (native-free) ───────────────────────

    [Fact]
    public void W8c_Zero_SetFadeCurve_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.SetFadeCurveChecked(IntPtr.Zero, 0);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"W8c: SetFadeCurveChecked(Zero) must be InvalidHandle, got {rc}");
    }

    [Fact]
    public void W8c_Zero_SetFadeCurve_BadCurve_StillInvalidHandle()
    {
        // Zero-handle onceciligi: kotu curve bile InvalidHandle dondurur
        // (InvalidArgument degil), native'e dokunulmaz.
        var rc = NativeBridgeChecked.SetFadeCurveChecked(IntPtr.Zero, 7);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"W8c: SetFadeCurveChecked(Zero,7) must be InvalidHandle, got {rc}");
    }

    [Fact]
    public void W8c_Zero_GetFadeCurve_ReturnsInvalidHandle()
    {
        var rc = NativeBridgeChecked.GetFadeCurveChecked(IntPtr.Zero, out int curve);
        if (rc != NativeBridge.ResultCode.InvalidHandle)
            throw new Exception($"W8c: GetFadeCurveChecked(Zero) must be InvalidHandle, got {rc}");
        if (curve != 0)
            throw new Exception("W8c: GetFadeCurveChecked(Zero) must default curve=0");
    }

    // ── RED-2: sayim-kilidi (211 = 57 ResultCode + 154 legacy) ─────────────

    [Fact]
    public void W8c_LegacyCount_Locked_154_Of_211()
    {
        var bridgeMethods = typeof(NativeBridge)
            .GetMethods(BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
            .Where(m => m.GetCustomAttributes(typeof(DllImportAttribute), false).Length > 0)
            .ToList();
        int total = bridgeMethods.Count;
        int resultCode = bridgeMethods.Count(m => m.ReturnType == typeof(NativeBridge.ResultCode));
        int legacy = total - resultCode;
        if (total != 211 || resultCode != 57 || legacy != 154)
            throw new Exception($"W8c: bridge census must be 211=57+154, got {total}={resultCode}+{legacy}");

        var covered = typeof(NativeBridgeChecked)
            .GetMethods(BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
            .Where(m => m.ReturnType == typeof(NativeBridge.ResultCode))
            .ToList();
        if (covered.Count != 8)
            throw new Exception($"W8c: Checked coverage must be 8, got {covered.Count}");

        var legacyNames = bridgeMethods.Select(m => m.Name).ToHashSet(StringComparer.Ordinal);
        foreach (var m in covered)
        {
            string stem = m.Name.EndsWith("Checked", StringComparison.Ordinal)
                ? m.Name.Substring(0, m.Name.Length - "Checked".Length)
                : m.Name;
            if (!legacyNames.Any(n => n.EndsWith("_" + stem, StringComparison.Ordinal)))
                throw new Exception($"W8c: {m.Name} has no legacy RowlEngine_* twin");
        }
    }

    // ── GREEN: canli-handle pozitif yol (owner-thread dispatch) ────────────

    private static EngineHost CreateLiveHost()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        var host = new EngineHost();
        if (!host.Initialize(64, 64, false))
            throw new Exception("W8c: failed to init offscreen host");
        if (host.Runtime == null)
            throw new Exception("W8c: live host must expose a runtime worker");
        return host;
    }

    [Fact]
    public void W8c_Live_CheckedTwins_RoundTrip()
    {
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;

        if (runtime.Invoke(h => NativeBridgeChecked.SetFadeCurveChecked(h, 1))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("W8c: live set 1 must be Ok");
        int got = runtime.Invoke(h =>
        {
            if (NativeBridgeChecked.GetFadeCurveChecked(h, out int v)
                != NativeBridge.ResultCode.Ok)
                throw new Exception("W8c: live get must be Ok");
            return v;
        });
        if (got != 1)
            throw new Exception($"W8c: fade round-trip failed, got {got}");

        if (runtime.Invoke(h => NativeBridgeChecked.SetFadeCurveChecked(h, 0))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("W8c: live set 0 must be Ok");
    }

    [Fact]
    public void W8c_Live_BadCurve_PreservesValue()
    {
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;

        if (runtime.Invoke(h => NativeBridgeChecked.SetFadeCurveChecked(h, 1))
            != NativeBridge.ResultCode.Ok)
            throw new Exception("W8c: live set 1 must be Ok");
        if (runtime.Invoke(h => NativeBridgeChecked.SetFadeCurveChecked(h, 7))
            != NativeBridge.ResultCode.InvalidArgument)
            throw new Exception("W8c: live bad curve must be InvalidArgument");
        int after = runtime.Invoke(h =>
        {
            NativeBridgeChecked.GetFadeCurveChecked(h, out int v);
            return v;
        });
        if (after != 1)
            throw new Exception($"W8c: bad curve must preserve fade value, got {after}");
    }

    [Fact]
    public void W8c_Live_ForeignThread_ReturnsWrongThread()
    {
        using var host = CreateLiveHost();
        IntPtr live = host.Runtime!.Handle;

        var setRc = Task.Run(() => NativeBridgeChecked.SetFadeCurveChecked(live, 0)).GetAwaiter().GetResult();
        if (setRc != NativeBridge.ResultCode.WrongThread)
            throw new Exception($"W8c: foreign-thread set must be WrongThread, got {setRc}");
        var getRc = Task.Run(() => NativeBridgeChecked.GetFadeCurveChecked(live, out _)).GetAwaiter().GetResult();
        if (getRc != NativeBridge.ResultCode.WrongThread)
            throw new Exception($"W8c: foreign-thread get must be WrongThread, got {getRc}");
    }

    // Ham P/Invoke yalnizca test-montajinda: C# out int null tutamaz, null
    // out native sozlesmesi (InvalidArgument) boyle kilitlenir. Kopru
    // census'unu etkilemez (typeof(NativeBridge) disi).
    [DllImport("RowlEngineCore", CallingConvention = CallingConvention.Cdecl,
        EntryPoint = "RowlEngine_GetFadeCurveChecked")]
    private static extern NativeBridge.ResultCode TestOnly_GetFadeCurveCheckedRaw(
        IntPtr handle, IntPtr outValue);

    [Fact]
    public void W8c_Live_NullOut_ReturnsInvalidArgument()
    {
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;
        var rc = runtime.Invoke(h => TestOnly_GetFadeCurveCheckedRaw(h, IntPtr.Zero));
        if (rc != NativeBridge.ResultCode.InvalidArgument)
            throw new Exception($"W8c: null out must be InvalidArgument, got {rc}");
    }

    [Fact]
    public void W8c_Worker_LoudForm_RoundTrip()
    {
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;

        if (runtime.SetFadeCurveChecked(1) != NativeBridge.ResultCode.Ok)
            throw new Exception("W8c: worker loud set 1 must be Ok");
        if (runtime.GetFadeCurveChecked(out int got) != NativeBridge.ResultCode.Ok || got != 1)
            throw new Exception($"W8c: worker loud get must be Ok/1, got {got}");
        if (runtime.SetFadeCurveChecked(7) != NativeBridge.ResultCode.InvalidArgument)
            throw new Exception("W8c: worker loud bad curve must be InvalidArgument");
    }

    [Fact]
    public void W8c_LegacySilentForm_Unchanged()
    {
        // Legacy sessiz form davranisi korunur: kotu curve sessizce
        // dusurulur (son gecerli deger 1 kalir).
        using var host = CreateLiveHost();
        var runtime = host.Runtime!;

        runtime.SetFadeCurve(1);
        runtime.SetFadeCurve(7);
        if (runtime.GetFadeCurveChecked(out int got) != NativeBridge.ResultCode.Ok || got != 1)
            throw new Exception($"W8c: legacy silent drop must keep last valid, got {got}");
    }
}
