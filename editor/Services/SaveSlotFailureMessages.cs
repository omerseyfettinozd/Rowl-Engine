using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Services;

/// <summary>
/// C04 — motorun son sonuç kodunu (EngineHost.LastResultCode) slot kaydet/yükle
/// hatası için yerelleştirilmiş nedene çevirir. Bilinmeyen kod: null (genel ileti).
/// </summary>
internal static class SaveSlotFailureMessages
{
    public static string? Describe(RuntimeErrorCode code) => code switch
    {
        RuntimeErrorCode.IoError => EditorStrings.T("SaveSlots.Error.Io"),
        RuntimeErrorCode.ValidationError or RuntimeErrorCode.Unsupported => EditorStrings.T("SaveSlots.Error.Invalid"),
        RuntimeErrorCode.ParseError => EditorStrings.T("SaveSlots.Error.Corrupt"),
        RuntimeErrorCode.FileNotFound => EditorStrings.T("SaveSlots.Error.Missing"),
        RuntimeErrorCode.InvalidArgument => EditorStrings.T("SaveSlots.Error.BadSlot"),
        RuntimeErrorCode.StateError => EditorStrings.T("SaveSlots.Error.NotStarted"),
        _ => null,
    };

    /// <summary>"... kaydedilemedi" + varsa " (neden)" eki.</summary>
    public static string WithReason(string baseMessage, RuntimeErrorCode code)
    {
        string? reason = Describe(code);
        return reason is null ? baseMessage : $"{baseMessage} ({reason})";
    }
}
