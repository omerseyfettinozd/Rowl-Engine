/**
 * NativeBridge.cs
 *
 * P/Invoke declarations for the native RowlEngineCore shared library.
 *
 * All string parameters and return values are explicitly marshaled as UTF-8
 * to support full international character sets (Turkish, Japanese, etc.).
 */

using System;
using System.Runtime.InteropServices;

namespace RowlEngine.Editor.Native
{
    internal static class NativeBridge
    {
        private const string Lib = "RowlEngineCore";

        // ── Lifecycle ────────────────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_Create();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_Destroy(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_Init(
            IntPtr handle,
            uint virtualWidth,
            uint virtualHeight,
            int vsync);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_Step(IntPtr handle, float deltaTime);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_Shutdown(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int RowlEngine_IsRunning(IntPtr handle);

        // ── Native window embedding ──────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetExternalWindowHandle(
            IntPtr handle,
            IntPtr nativeWindowHandle,
            uint width,
            uint height);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ResizeViewport(
            IntPtr handle,
            uint newWidth,
            uint newHeight);

        // ── Offscreen Framebuffer & Playback Control ──────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetPixelBuffer(
            IntPtr handle,
            out uint outW,
            out uint outH);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_SetPlayState(
            IntPtr handle,
            int isPlaying);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_ResetToStartNode(
            IntPtr handle);

        // ── Scene / story control ────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_UpdateScene(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string speaker,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string dialogue,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string background,
            float bgX,   float bgY,   float bgW,   float bgH,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string character,
            float charX, float charY, float charW, float charH,
            float dlgX,  float dlgY,  float dlgW,  float dlgH);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_LoadStoryGraph(
            IntPtr handle,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonPath);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void RowlEngine_AdvanceNode(
            IntPtr handle,
            uint choiceIndex);

        // ── State queries ────────────────────────────────────────────────────

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetSpeaker(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr RowlEngine_GetDialogue(IntPtr handle);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern ulong RowlEngine_GetCurrentNodeId(IntPtr handle);

        // ── Helpers ──────────────────────────────────────────────────────────

        /// <summary>Converts a native C UTF-8 string pointer to a managed string safely.</summary>
        internal static string PtrToString(IntPtr ptr)
            => ptr == IntPtr.Zero ? string.Empty
                                  : Marshal.PtrToStringUTF8(ptr) ?? string.Empty;
    }
}
