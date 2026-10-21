using System;
using System.Runtime.InteropServices;

namespace RowlEngine.Editor.Native
{
    /// <summary>Where the native game id came from (mirrors RowlEngine_GameIdentitySource).</summary>
    public enum GameIdentitySource
    {
        None = 0,
        Package = 1,
        Manifest = 2,
        /// <summary>Path-derived fallback: stable per location but not authored.</summary>
        Derived = 3
    }

    public readonly record struct GameIdentityInfo(string GameId, GameIdentitySource Source, string Diagnostic)
    {
        public static GameIdentityInfo None { get; } = new(string.Empty, GameIdentitySource.None, string.Empty);
    }

    /// <summary>
    /// D01a — reads the identity resolved by the native engine at project
    /// mount. Call on the engine thread, e.g.
    /// <c>host.InvokeOnEngineThread(GameIdentityBridge.Read, GameIdentityInfo.None)</c>.
    /// </summary>
    internal static class GameIdentityBridge
    {
        internal static GameIdentityInfo Read(IntPtr handle)
        {
            if (handle == IntPtr.Zero) return GameIdentityInfo.None;
            string id = ReadString(handle, NativeBridge.RowlEngine_GetGameIdUtf8);
            int rawSource = NativeBridge.RowlEngine_GetGameIdentitySource(handle);
            var source = Enum.IsDefined(typeof(GameIdentitySource), rawSource)
                ? (GameIdentitySource)rawSource
                : GameIdentitySource.None;
            string diagnostic = ReadString(handle, NativeBridge.RowlEngine_GetGameIdentityDiagnosticUtf8);
            return new GameIdentityInfo(id, source, diagnostic);
        }

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
