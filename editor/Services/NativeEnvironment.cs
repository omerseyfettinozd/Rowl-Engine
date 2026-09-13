using System;
using System.Runtime.InteropServices;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Display-free driver selection for the native engine. SDL reads its
/// driver variables with the native getenv, which the managed
/// Environment.SetEnvironmentVariable does not reach inside the test/run
/// host (verified: native getenv stays empty after a managed set while a
/// libc setenv is honored). The dummy drivers are therefore exported
/// through libc on Unix. Explicit environment overrides (CI, CTest) win
/// whenever they are already set. Must run before the first native init.
/// </summary>
internal static class NativeEnvironment
{
    [DllImport("libc", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl)]
    private static extern int setenv(string name, string value, int overwrite);

    public static void EnsureDisplayFreeDrivers()
    {
        EnsureDriver("SDL_VIDEODRIVER", "dummy");
        EnsureDriver("SDL_AUDIODRIVER", "dummy");
    }

    private static void EnsureDriver(string name, string value)
    {
        try
        {
            if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable(name)))
                return;
        }
        catch
        {
            return;
        }

        if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
        {
            try { setenv(name, value, 0); }
            catch { }
        }

        try { Environment.SetEnvironmentVariable(name, value); }
        catch { }
    }
}
