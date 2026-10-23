using System;
using System.Runtime.InteropServices;

namespace RowlEngine.Editor.Native
{
    /// <summary>
    /// D01b — where the native engine keeps the mounted game's data
    /// (<c>games/&lt;game-id&gt;/{saves,profile}</c> under the user-data root, or
    /// the project folder for a game without an authored id) and what the last
    /// legacy-save migration did (JSON, empty when nothing was migrated).
    /// </summary>
    public readonly record struct GameDataInfo(string DataDirectory, string MigrationJson)
    {
        public static GameDataInfo None { get; } = new(string.Empty, string.Empty);
    }

    /// <summary>
    /// Reads the per-game data layout resolved at project mount. Call on the
    /// engine thread: <c>host.InvokeOnEngineThread(GameDataBridge.Read, GameDataInfo.None)</c>.
    /// </summary>
    internal static class GameDataBridge
    {
        internal static GameDataInfo Read(IntPtr handle)
        {
            if (handle == IntPtr.Zero) return GameDataInfo.None;
            return new GameDataInfo(
                ReadString(handle, NativeBridge.RowlEngine_GetGameDataDirectoryUtf8),
                ReadString(handle, NativeBridge.RowlEngine_GetLastDataMigrationJson));
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
