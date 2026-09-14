using System;
using System.Runtime.InteropServices;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Thin caller-buffer bridge for the native shaping inspection API. Runtime
/// drawing already consumes the same native pipeline; this helper lets editor
/// diagnostics and tests inspect its deterministic JSON without owning a
/// native engine handle.
/// </summary>
public static class TextShapingService
{
    public const ulong NativeCapabilityTextShaping = 512UL;

    public static bool TryShapeNativeJson(
        string? markup,
        byte[]? fontData,
        float fontSize,
        float maxWidth,
        string? language,
        out string? json)
    {
        json = null;
        if (fontData is null || fontData.Length == 0 ||
            fontData.LongLength > uint.MaxValue)
            return false;

        GCHandle pin = default;
        try
        {
            pin = GCHandle.Alloc(fontData, GCHandleType.Pinned);
            IntPtr bytes = pin.AddrOfPinnedObject();
            NativeBridge.ResultCode Query(IntPtr buffer, uint size, out uint required) =>
                NativeBridge.RowlEngine_ShapeMarkup(
                    markup ?? string.Empty, bytes, (uint)fontData.Length,
                    fontSize, maxWidth, language, buffer, size, out required);

            if (Query(IntPtr.Zero, 0, out uint requiredSize) !=
                    NativeBridge.ResultCode.Ok || requiredSize == 0)
                return false;
            IntPtr buffer = Marshal.AllocHGlobal(checked((int)requiredSize));
            try
            {
                if (Query(buffer, requiredSize, out uint repeated) !=
                        NativeBridge.ResultCode.Ok || repeated != requiredSize)
                    return false;
                json = Marshal.PtrToStringUTF8(buffer);
                return json is not null;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        catch (Exception)
        {
            return false;
        }
        finally
        {
            if (pin.IsAllocated) pin.Free();
        }
    }
}
