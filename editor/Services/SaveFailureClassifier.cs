using System;
using System.IO;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Services;

/// <summary>Kayıt yazımının sonucu: yazıldı / daha yeni kayıt devraldı / engellendi / başarısız.</summary>
internal enum SaveWriteStatus { Written, Superseded, Blocked, Failed }

/// <summary>Neden ve (varsa) istisna ile kayıt sonucu; Superseded başarısızlık DEĞİLDİR.</summary>
internal readonly record struct SaveWriteResult(SaveWriteStatus Status, Exception? Error = null, string? Message = null)
{
    public bool Written => Status == SaveWriteStatus.Written;
}

/// <summary>
/// C04 — kayıt istisnasını kullanıcıya doğru nedenle gösterir: disk dolu,
/// izin yok, salt okunur dosya sistemi; bilinmeyen için null (çağıran genel iletiye düşer).
/// </summary>
public static class SaveFailureClassifier
{
    // Win32: ERROR_DISK_FULL 0x70, ERROR_HANDLE_DISK_FULL 0x27, ERROR_WRITE_PROTECT 0x13.
    // POSIX errno (.NET Unix IOException.HResult ham errno): ENOSPC 28, EROFS 30, EDQUOT 122.
    public enum Cause { Unknown, DiskFull, PermissionDenied, ReadOnly }

    public static Cause Classify(Exception? ex)
    {
        for (int depth = 0; ex != null && depth < 8; depth++, ex = ex.InnerException)
        {
            if (ex is UnauthorizedAccessException) return Cause.PermissionDenied;
            if (ex is IOException)
            {
                int hr = ex.HResult;
                if ((hr & unchecked((int)0xFFFF0000)) == unchecked((int)0x80070000))
                {
                    int code = hr & 0xFFFF;
                    if (code == 0x70 || code == 0x27) return Cause.DiskFull;
                    if (code == 0x13) return Cause.ReadOnly;
                }
                else if (hr == 28 || hr == 122) return Cause.DiskFull;
                else if (hr == 30) return Cause.ReadOnly;
            }
        }
        return Cause.Unknown;
    }

    /// <summary>Yerelleştirilmiş neden; sınıflanamazsa null.</summary>
    internal static string? Describe(Exception? ex) => Classify(ex) switch
    {
        Cause.DiskFull => EditorStrings.T("Operation.Save.DiskFull"),
        Cause.PermissionDenied => EditorStrings.T("Operation.Save.PermissionDenied"),
        Cause.ReadOnly => EditorStrings.T("Operation.Save.ReadOnly"),
        _ => null,
    };

    /// <summary>Hata kartı metni: engel iletisi > sınıflı neden > boş (genel WriteFailed).</summary>
    internal static string ReasonFor(SaveWriteResult result)
        => result.Message ?? Describe(result.Error) ?? string.Empty;
}
