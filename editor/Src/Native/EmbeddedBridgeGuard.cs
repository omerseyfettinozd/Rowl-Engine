using System;

namespace RowlEngine.Editor.Native
{
    /// <summary>
    /// D01 (#135) — gömülü-yol yönlendirme kararı. Saf statik, native'siz
    /// ünit-test edilir; worker-dispatch/affinity sözleşmesine dokunmaz.
    /// </summary>
    internal enum EmbeddedRoute
    {
        /// <summary>Null handle: offscreen framebuffer yoluna düş.</summary>
        OffscreenFallback,
        /// <summary>Nonzero handle + gömülebilir platform: worker üzerinden
        /// önce Checked-SetExternal, sonra Init dene.</summary>
        EmbeddedAttempt,
        /// <summary>Hiçbir native çağrı yapmadan false + LastError ile kapat.</summary>
        FailClosed,
    }

    /// <summary>
    /// D01 (#135) — InitializeEmbedded yönlendirme bekçisi. EngineHost'un
    /// eski imzası dondurulmuştur (offscreen-fallback olarak kalır); yeni
    /// gömülü-yol EmbeddedRuntimeBootstrap + bu guard üzerinden akar.
    /// Fail-closed: throw yok, native çağrı yok.
    /// </summary>
    internal static class EmbeddedBridgeGuard
    {
        internal static EmbeddedRoute Classify(IntPtr nativeWindowHandle, uint width, uint height)
        {
            if (nativeWindowHandle == IntPtr.Zero)
                return EmbeddedRoute.OffscreenFallback;
            if (width == 0 || height == 0)
                return EmbeddedRoute.FailClosed;
            if (IsWaylandSession())
                return EmbeddedRoute.FailClosed;
            return EmbeddedRoute.EmbeddedAttempt;
        }

        /// <summary>
        /// Wayland'da gerçek HWND/X11 handle üretilemez; gömme denemesi
        /// window.cpp:427 fail-closed'a takılmak yerine hiç denenmez.
        /// (SDL_VIDEODRIVER=dummy tek başına kapatmaz: dummy altında Zero
        /// handle zaten offscreen'e düşer; nonzero'nun reddi native Init'e
        /// aittir.)
        /// </summary>
        internal static bool IsWaylandSession()
        {
            try
            {
                if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("WAYLAND_DISPLAY")))
                    return true;
                return string.Equals(
                    Environment.GetEnvironmentVariable("XDG_SESSION_TYPE"),
                    "wayland",
                    StringComparison.OrdinalIgnoreCase);
            }
            catch
            {
                return false;
            }
        }

        internal static string DescribeRejection(IntPtr nativeWindowHandle, uint width, uint height)
        {
            if (nativeWindowHandle != IntPtr.Zero && (width == 0 || height == 0))
                return $"Embedded init rejected: nonzero window handle with zero surface size ({width}x{height}); pass IntPtr.Zero for offscreen fallback.";
            if (nativeWindowHandle != IntPtr.Zero && IsWaylandSession())
                return "Embedded init rejected: Wayland sessions cannot host a native window handle; pass IntPtr.Zero for offscreen fallback (window.cpp Wayland fail-closed preserved).";
            return "Embedded init rejected: unsupported handle/platform combination; pass IntPtr.Zero for offscreen fallback.";
        }
    }
}
