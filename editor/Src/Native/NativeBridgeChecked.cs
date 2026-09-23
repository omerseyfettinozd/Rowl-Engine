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
    }
}
