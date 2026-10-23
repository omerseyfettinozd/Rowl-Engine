using System;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace RowlEngine.Editor.Native
{
    /// <summary>
    /// D01c-2 — status of the native player profile
    /// (<c>RowlEngine_ProfileGetStatusJson</c>). <see cref="Status"/> is one of
    /// <c>unmounted | loaded | missing | corrupt | unsupported_version |
    /// wrong_game</c>; <see cref="ReadOnly"/> profiles are never written.
    /// </summary>
    public readonly record struct NativeProfileStatus(
        string Status,
        bool ReadOnly,
        string Diagnostic,
        string Path,
        int ReadCount,
        string GameId,
        bool Dirty)
    {
        /// <summary>Returned when the capability or the handle is missing.</summary>
        public static NativeProfileStatus Unavailable { get; } =
            new("unavailable", true, string.Empty, string.Empty, 0, string.Empty, false);

        /// <summary>True when a project is mounted and the profile store answers.</summary>
        public bool IsMounted => Status is not ("unavailable" or "unmounted");
    }

    /// <summary>
    /// D01c-2 — typed wrapper over the native player-profile C API
    /// (<c>ROWL_ENGINE_CAPABILITY_PLAYER_PROFILE</c>). Every method takes the
    /// engine handle and must run on the engine thread:
    /// <c>host.InvokeOnEngineThread(NativeProfileBridge.GetStatus, NativeProfileStatus.Unavailable)</c>.
    /// Calling from another thread makes the native side answer WRONG_THREAD.
    /// </summary>
    internal static class NativeProfileBridge
    {
        /// <summary>Mirrors ROWL_ENGINE_CAPABILITY_PLAYER_PROFILE.</summary>
        internal const ulong CapabilityPlayerProfile = 16777216UL;

        private static bool? _supported;

        /// <summary>
        /// True when the loaded native library advertises the player-profile
        /// capability (handle-free query, cached). Older libraries or a missing
        /// library answer false so callers fall back to the managed store.
        /// </summary>
        internal static bool IsSupported()
        {
            if (_supported is bool known)
                return known;
            bool supported;
            try
            {
                supported = NativeBridge.RowlEngine_GetCapabilities(out ulong caps) == NativeBridge.ResultCode.Ok
                    && (caps & CapabilityPlayerProfile) != 0;
            }
            catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
            {
                supported = false;
            }
            _supported = supported;
            return supported;
        }

        /// <summary>The profile directory of the mounted game (empty before a mount).</summary>
        internal static string GetProfileDirectory(IntPtr handle) =>
            handle == IntPtr.Zero ? string.Empty : ReadString(handle, NativeBridge.RowlEngine_GetProfileDirectoryUtf8);

        /// <summary>The full current profile document (what a flush would write); empty on failure.</summary>
        internal static string GetJson(IntPtr handle) =>
            handle == IntPtr.Zero ? string.Empty : ReadString(handle, NativeBridge.RowlEngine_ProfileGetJson);

        internal static NativeProfileStatus GetStatus(IntPtr handle)
        {
            if (handle == IntPtr.Zero)
                return NativeProfileStatus.Unavailable;
            string json = ReadString(handle, NativeBridge.RowlEngine_ProfileGetStatusJson);
            if (json.Length == 0)
                return NativeProfileStatus.Unavailable;
            try
            {
                using var document = JsonDocument.Parse(json);
                var root = document.RootElement;
                return new NativeProfileStatus(
                    Text(root, "status", "unavailable"),
                    root.TryGetProperty("read_only", out var ro) && ro.ValueKind == JsonValueKind.True,
                    Text(root, "diagnostic", string.Empty),
                    Text(root, "path", string.Empty),
                    root.TryGetProperty("read_count", out var rc) && rc.TryGetInt32(out int count) ? count : 0,
                    Text(root, "game_id", string.Empty),
                    root.TryGetProperty("dirty", out var dirty) && dirty.ValueKind == JsonValueKind.True);
            }
            catch (JsonException)
            {
                return NativeProfileStatus.Unavailable;
            }
        }

        /// <summary>Merges known preference keys (all-or-nothing on the native side).</summary>
        internal static NativeBridge.ResultCode SetPreferencesJson(IntPtr handle, string json) =>
            handle == IntPtr.Zero
                ? NativeBridge.ResultCode.InvalidHandle
                : NativeBridge.RowlEngine_ProfileSetPreferencesJson(handle, json);

        internal static NativeBridge.ResultCode MarkRead(IntPtr handle, string contentId) =>
            handle == IntPtr.Zero
                ? NativeBridge.ResultCode.InvalidHandle
                : NativeBridge.RowlEngine_ProfileMarkRead(handle, contentId);

        internal static bool IsRead(IntPtr handle, string contentId) =>
            handle != IntPtr.Zero
            && NativeBridge.RowlEngine_ProfileIsRead(handle, contentId, out int isRead) == NativeBridge.ResultCode.Ok
            && isRead != 0;

        internal static NativeBridge.ResultCode Flush(IntPtr handle) =>
            handle == IntPtr.Zero
                ? NativeBridge.ResultCode.InvalidHandle
                : NativeBridge.RowlEngine_ProfileFlush(handle);

        private static string Text(JsonElement root, string key, string fallback) =>
            root.TryGetProperty(key, out var element) && element.ValueKind == JsonValueKind.String
                ? element.GetString() ?? fallback
                : fallback;

        private delegate NativeBridge.ResultCode Getter(IntPtr handle, IntPtr buffer, uint size, out uint required);

        private static string ReadString(IntPtr handle, Getter getter)
        {
            if (getter(handle, IntPtr.Zero, 0, out uint required) != NativeBridge.ResultCode.Ok || required == 0)
                return string.Empty;
            IntPtr buffer = Marshal.AllocHGlobal((int)required);
            try
            {
                if (getter(handle, buffer, required, out _) != NativeBridge.ResultCode.Ok)
                    return string.Empty;
                return Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
    }
}
