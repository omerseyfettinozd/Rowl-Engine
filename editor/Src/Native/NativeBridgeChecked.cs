using System;

namespace RowlEngine.Editor.Native
{
    // D16 — playback-transport + master-gain Checked twin'leri.
    //
    // Legacy P/Invoke formlar (int/void/float) ABI için NativeBridge'de
    // durur; fail-closed semantik BURADADIR (managed, native'siz):
    //  - IntPtr.Zero → InvalidHandle, native'e DOKUNULMAZ.
    //  - SetMasterVolume non-finite → InvalidArgument, native'e
    //    DOKUNULMAZ (B4 NativeGuard deliği burada da kapalıdır).
    //  - finite hacim [0,1]'e clamp'lenir (native "clamp+ignore"
    //    semantiğiyle aynı), canlı handle'da legacy'e forward → Ok.
    // c_api.h'ye dokunulmaz; yeni native export YOKTUR.
    internal static class NativeBridgeChecked
    {
        internal static NativeBridge.ResultCode IsPausedChecked(IntPtr handle, out bool paused)
        {
            paused = false;
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            paused = NativeBridge.RowlEngine_IsPaused(handle) != 0;
            return NativeBridge.ResultCode.Ok;
        }

        internal static NativeBridge.ResultCode SetPausedChecked(IntPtr handle, bool paused)
        {
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            NativeBridge.RowlEngine_SetPaused(handle, paused ? 1 : 0);
            return NativeBridge.ResultCode.Ok;
        }

        internal static NativeBridge.ResultCode SetPlayStateChecked(IntPtr handle, bool isPlaying)
        {
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            NativeBridge.RowlEngine_SetPlayState(handle, isPlaying ? 1 : 0);
            return NativeBridge.ResultCode.Ok;
        }

        internal static NativeBridge.ResultCode IsRunningChecked(IntPtr handle, out bool running)
        {
            running = false;
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            running = NativeBridge.RowlEngine_IsRunning(handle) != 0;
            return NativeBridge.ResultCode.Ok;
        }

        internal static NativeBridge.ResultCode SetMasterVolumeChecked(IntPtr handle, float volume)
        {
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            if (!NativeGuard.TryClamp01(volume, out float v))
                return NativeBridge.ResultCode.InvalidArgument;
            NativeBridge.RowlEngine_SetMasterVolume(handle, v);
            return NativeBridge.ResultCode.Ok;
        }

        internal static NativeBridge.ResultCode GetMasterVolumeChecked(IntPtr handle, out float volume)
        {
            volume = 0.0f;
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            volume = NativeBridge.RowlEngine_GetMasterVolume(handle);
            return NativeBridge.ResultCode.Ok;
        }

        // W8-c — fade-curve loud-tier Checked twin'leri (c_api.h 830-842).
        //
        // Legacy void/int formlar sessiz-tier iken bu formlar loud-tier'dir:
        //  - IntPtr.Zero → InvalidHandle, native'e DOKUNULMAZ.
        //  - Set'te curve 0/1 dışı → InvalidArgument, native'e DOKUNULMAZ
        //    (native "0 Linear / 1 EqualPower" semantiğiyle birebir).
        //  - null out C#'ta out int ile ifade edilemez; null yalıtımı
        //    native'de fail-closed'dur (InvalidArgument) — bk. test.
        //  - canlı handle'da yabancı-thread çağrısı native WrongThread
        //    damgasını aynen yukarı taşır (eşleme/yutma yok).
        internal static NativeBridge.ResultCode SetFadeCurveChecked(IntPtr handle, int curve)
        {
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            if (curve != 0 && curve != 1)
                return NativeBridge.ResultCode.InvalidArgument;
            return NativeBridge.RowlEngine_SetFadeCurveChecked(handle, curve);
        }

        internal static NativeBridge.ResultCode GetFadeCurveChecked(IntPtr handle, out int curve)
        {
            curve = 0;
            if (handle == IntPtr.Zero)
                return NativeBridge.ResultCode.InvalidHandle;
            return NativeBridge.RowlEngine_GetFadeCurveChecked(handle, out curve);
        }
    }
}
