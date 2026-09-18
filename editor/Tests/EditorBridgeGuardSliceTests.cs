using System;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

// B4: C# öndoğrulama/clamp (NativeGuard) + public RuntimeErrorCode 12/13.
// Native setter'lardaki NaN-deliği (Engine::setTextSpeedMultiplier /
// setAutoAdvanceDelayOffset — çıplak std::clamp NaN'i korur,
// engine.cpp:1777-1782) D3 yasağı yüzünden native'de kapatılamaz; guard
// köprü-öncesinde keser. Speed/offset için native getter YOKTUR — bu iki
// setter no-throw + guard-ünitiyle kapsanır (kırmızı-yalanı söylenmez);
// gözlenebilir setter'lar (master, scale, blip) round-trip ile kanıtlanır.
public sealed class EditorBridgeGuardSliceTests
{
    [Fact]
    public void B4_Guard_Unit_ClampSemantics()
    {
        if (NativeGuard.TryClamp01(float.NaN, out _) ||
            NativeGuard.TryClamp01(float.PositiveInfinity, out _) ||
            NativeGuard.TryClamp01(float.NegativeInfinity, out _))
            throw new Exception("B4: non-finite must not forward");
        if (!NativeGuard.TryClamp01(-1.0f, out float lo) || lo != 0.0f)
            throw new Exception("B4: below-range must clamp to 0");
        if (!NativeGuard.TryClamp01(5.0f, out float hi) || hi != 1.0f)
            throw new Exception("B4: above-range must clamp to 1");
        if (!NativeGuard.TryClamp01(0.5f, out float mid) || mid != 0.5f)
            throw new Exception("B4: in-range must pass through");
        if (NativeGuard.TryClamp(float.NaN, 0.25f, 4.0f, out _) ||
            !NativeGuard.TryClamp(10.0f, 0.25f, 4.0f, out float c) || c != 4.0f)
            throw new Exception("B4: ranged clamp semantics broken");
    }

    [Fact]
    public void B4_RuntimeErrorCode_MirrorsBridge12And13()
    {
        if ((int)RuntimeErrorCode.BufferTooSmall != 12 ||
            (int)RuntimeErrorCode.Unsupported != 13)
            throw new Exception("B4: public enum must mirror 12/13");
        if ((int)NativeBridge.ResultCode.BufferTooSmall != 12 ||
            (int)NativeBridge.ResultCode.Unsupported != 13)
            throw new Exception("B4: bridge/public enum parity broken");
    }

    private static EngineHost CreateLiveHost()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        var host = new EngineHost();
        if (!host.Initialize(64, 64, false))
            throw new Exception("B4: failed to init offscreen host");
        return host;
    }

    [Fact]
    public void B4_Guard_MasterVolume_NaNPreservesAndClamps()
    {
        using var host = CreateLiveHost();
            host.SetMasterVolume(0.5f);
            if (host.GetMasterVolume() != 0.5f)
                throw new Exception("B4: master round-trip failed");
            host.SetMasterVolume(float.NaN);
            host.SetMasterVolume(float.PositiveInfinity);
            if (host.GetMasterVolume() != 0.5f)
                throw new Exception("B4: NaN/Inf must preserve master volume");
            host.SetMasterVolume(5.0f);
            if (host.GetMasterVolume() != 1.0f)
                throw new Exception("B4: master must clamp to 1");
            host.SetMasterVolume(-2.0f);
            if (host.GetMasterVolume() != 0.0f)
                throw new Exception("B4: master must clamp to 0");
    }

    [Fact]
    public void B4_Guard_TextScale_NaNPreservesAndClamps()
    {
        using var host = CreateLiveHost();
            if (host.GetTextScale() != 1.0f)
                throw new Exception("B4: text scale default must be 1.0");
            host.SetTextScale(float.NaN);
            if (host.GetTextScale() != 1.0f)
                throw new Exception("B4: NaN must preserve text scale");
            host.SetTextScale(5.0f);
            if (host.GetTextScale() != 2.0f)
                throw new Exception("B4: text scale must clamp to 2");
    }

    [Fact]
    public void B4_Guard_BlipVolume_NaNPreserves()
    {
        using var host = CreateLiveHost();
            host.SetDialogueVoiceBlipVolume(0.5f);
            if (host.GetDialogueVoiceBlipVolume() != 0.5f)
                throw new Exception("B4: blip volume round-trip failed");
            host.SetDialogueVoiceBlipVolume(float.NaN);
            if (host.GetDialogueVoiceBlipVolume() != 0.5f)
                throw new Exception("B4: NaN must preserve blip volume");
    }

    [Fact]
    public void B4_Guard_SpeedAndOffset_NoThrowLive()
    {
        // Getter yok (C API'de de yok): canlı-handle'da throw-atmama +
        // clamp-forward yolunun çalışması kanıtlanır; değer-doğruluğu
        // B4_Guard_Unit_ClampSemantics'tedir.
        using var host = CreateLiveHost();
            var exception = Record.Exception(() =>
            {
                host.SetTextSpeedMultiplier(float.NaN);
                host.SetTextSpeedMultiplier(10.0f);
                host.SetAutoAdvanceDelayOffset(float.NaN);
                host.SetAutoAdvanceDelayOffset(-5.0f);
                host.SetBgmVolume(float.NaN);
                host.SetVoiceVolume(float.NaN);
                host.SetSfxVolume(float.NaN);
            });
            if (exception != null)
                throw new Exception($"B4: guarded setters threw: {exception.Message}");
    }
}
