using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 5 Dilim 5 — dönüştürülmüş asset provenance rozet açıklaması
/// (saf, native çağrı YOKTUR). Sidecar JSON'dan
/// <c>converter_name/version</c> + kısa hash'leri Inspector satırına çevirir.
/// Bozuk/eksik girdi fail-closed kapanır (gizli rozet).
/// </summary>
public sealed record ProvenanceBadgeDescription(bool Visible, string Text)
{
    public static readonly ProvenanceBadgeDescription Hidden = new(false, string.Empty);
}

/// <summary>
/// Faz 5 Dilim 5 — asset provenance ince servisi. Canlı okuma çağrıcının
/// verdiği native delege üzerinden yapılır (null delege = sidecar dosyası,
/// fail-closed); kendi thread'i yoktur. <c>EngineHost</c>'a dokunmaz
/// (SIFIR-DIFF: adapter-forward, çağrıcı delegeyi + handle'ı verir).
/// <para>
/// Native imza (<c>engine/include/rowl/c_api.h</c> — KAYNAK):
/// <c>RowlEngine_GetAssetProvenanceJson(handle, assetPathUtf8, buffer,
/// bufferSize, outRequiredSize)</c>; capability <c>131072</c>; sidecar yoksa
/// FILE_NOT_FOUND(3), bozuksa PARSE_ERROR(5), dar tamponsa BUFFER_TOO_SMALL(12).
/// Native yoksa/hatalıysa diskteki <c>&lt;çıktı&gt;.rowlconv.json</c>
/// sidecar'a düşülür.
/// </para>
/// </summary>
internal static class AssetProvenanceService
{
    /// <summary>Native provenance capability biti (C API'de tanımlı değilse tanımsız sayılır).</summary>
    public const ulong CapabilityAssetProvenance = 131072UL;

    /// <summary>
    /// Native caller-buffer çağrısının C# karşılığı — 1:1 Cdecl
    /// (<c>RowlEngine_GetAssetProvenanceJson</c>): handle ilk parametredir.
    /// Null/0 boyut sorgusu, eksik tampon + gerekli boyut, ölü handle — hepsi
    /// <see cref="NativeBridge.ResultCode"/> ile döner.
    /// </summary>
    public delegate NativeBridge.ResultCode ProvenanceNativeCall(
        IntPtr engineHandle, string assetPathUtf8, IntPtr buffer, uint bufferSize, out uint requiredSize);

    /// <summary>
    /// Prod native delegesi (Faz 5 Dilim 5 fix: ölü C# native yolunu canlandırır).
    /// <c>NativeBridge.RowlEngine_GetAssetProvenanceJson</c> DllImport'un ince sarmalayıcısıdır
    /// (doğrudan çağrı → UTF-8 marshal garantili; delege kurulurken native yüklenmez,
    /// ilk çağrıda çözülür). Çağrıcı delegeyi + handle'ı verir; testlerdeki
    /// sahte-delege deseni aynen korunur (hangi delege verilirse o kullanılır).
    /// </summary>
    internal static readonly ProvenanceNativeCall ProductionNativeCall = ProductionNativeCallImpl;

    private static NativeBridge.ResultCode ProductionNativeCallImpl(
        IntPtr engineHandle, string assetPathUtf8, IntPtr buffer, uint bufferSize, out uint requiredSize)
        => NativeBridge.RowlEngine_GetAssetProvenanceJson(engineHandle, assetPathUtf8, buffer, bufferSize, out requiredSize);

    /// <summary>
    /// Caller-buffer sözleşmesini delege üzerinden okur (1:1 NativeBridge
    /// deseni; throw yok, fail-closed).
    /// </summary>
    public static bool TryGetViaNative(
        IntPtr engineHandle,
        string assetPath,
        ProvenanceNativeCall nativeCall,
        out string json,
        out string error)
    {
        json = string.Empty;
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(assetPath) || nativeCall is null)
        {
            error = "Geçersiz provenance sorgusu.";
            return false;
        }
        NativeBridge.ResultCode query;
        try
        {
            query = nativeCall(engineHandle, assetPath, IntPtr.Zero, 0, out uint required);
            if (query != NativeBridge.ResultCode.Ok || required == 0)
            {
                error = query switch
                {
                    NativeBridge.ResultCode.FileNotFound =>
                        $"Provenance bulunamadı: '{assetPath}' (NOT_FOUND).",
                    NativeBridge.ResultCode.BufferTooSmall =>
                        $"Provenance tamponu küçük ({required} bayt gerekli): '{assetPath}'.",
                    _ => $"Provenance sorgusu başarısız ({query}): '{assetPath}'.",
                };
                return false;
            }
            IntPtr buffer = Marshal.AllocHGlobal(checked((int)required));
            try
            {
                NativeBridge.ResultCode read = nativeCall(engineHandle, assetPath, buffer, required, out _);
                if (read != NativeBridge.ResultCode.Ok)
                {
                    error = read == NativeBridge.ResultCode.BufferTooSmall
                        ? $"Provenance tamponu küçük ({required} bayt gerekli): '{assetPath}'."
                        : $"Provenance okunamadı ({read}): '{assetPath}'.";
                    return false;
                }
                json = Marshal.PtrToStringUTF8(buffer) ?? string.Empty;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        catch (Exception ex) when (ex is EntryPointNotFoundException or DllNotFoundException)
        {
            error = $"Native provenance API yok ({ex.GetType().Name}); sidecar'a düşülür.";
            return false;
        }
        if (string.IsNullOrEmpty(json))
        {
            error = $"Provenance boş döndü: '{assetPath}'.";
            return false;
        }
        return true;
    }

    /// <summary>
    /// Asset referansını provenance kaydına çözer: önce native (delege
    /// verildiyse), sonra <c>Assets/</c> altındaki sidecar dosyası.
    /// </summary>
    public static MediaConversionProvenance? ResolveProvenance(
        string? assetRef, string assetsRoot, ProvenanceNativeCall? nativeCall = null, IntPtr engineHandle = default)
    {
        if (string.IsNullOrWhiteSpace(assetRef) || string.IsNullOrWhiteSpace(assetsRoot))
            return null;
        if (nativeCall is not null
            && TryGetViaNative(engineHandle, assetRef, nativeCall, out string nativeJson, out _)
            && MediaConversionProvenance.TryParse(nativeJson) is { } nativeProvenance)
            return nativeProvenance;
        string? outputFull = ResolveOutputFullPath(assetRef, assetsRoot);
        if (outputFull is null)
            return null;
        return MediaConversionProvenance.TryReadFile(outputFull + MediaConverterService.SidecarSuffix);
    }

    internal static string? ResolveOutputFullPath(string assetRef, string assetsRoot)
    {
        string normalized = assetRef.Replace('\\', '/').Trim();
        string[] candidates = {
            normalized,
            "images/" + normalized, "audio/" + normalized, "fonts/" + normalized,
        };
        string root;
        try
        {
            root = Path.GetFullPath(assetsRoot);
            if (!Directory.Exists(root))
                return null;
        }
        catch (Exception)
        {
            return null;
        }
        foreach (string candidate in candidates)
        {
            string full;
            try
            {
                full = Path.GetFullPath(Path.Combine(root, candidate));
                if (!ProjectFileSystem.IsSameOrDescendant(full, root))
                    continue;
            }
            catch (Exception)
            {
                continue;
            }
            if (File.Exists(full))
                return full;
        }
        return null;
    }

    public static string ShortHash(string? sha256, int length = 8)
    {
        if (string.IsNullOrEmpty(sha256))
            return "—";
        string trimmed = sha256.Trim();
        return trimmed.Length <= length ? trimmed : trimmed[..length];
    }

    /// <summary>Inspector rozeti: dönüştürücü adı/sürümü + kısa hash'ler.</summary>
    public static ProvenanceBadgeDescription Describe(MediaConversionProvenance? provenance)
    {
        if (provenance is null)
            return ProvenanceBadgeDescription.Hidden;
        return new(true,
            $"CONV • {provenance.ConverterName} {provenance.ConverterVersion} • " +
            $"src {ShortHash(provenance.SourceSha256)} • out {ShortHash(provenance.OutputSha256)}");
    }

    /// <summary>Salt-okunur Inspector detay satırları (3 satır).</summary>
    public static IReadOnlyList<string> FormatDetails(MediaConversionProvenance? provenance)
    {
        if (provenance is null)
            return Array.Empty<string>();
        return new[]
        {
            $"Dönüştürücü: {provenance.ConverterName} {provenance.ConverterVersion}",
            $"Kaynak SHA-256: {ShortHash(provenance.SourceSha256)}",
            $"Çıktı SHA-256: {ShortHash(provenance.OutputSha256)}",
        };
    }

    /// <summary>
    /// Seçili düğümün asset referanslarından dönüştürülmüş olanların
    /// provenance satırları (salt-okunur Inspector görünümü). Referans
    /// başına başlık + 3 detay satırı; çözülemeyen referans sessiz geçilir.
    /// </summary>
    public static IReadOnlyList<string> FormatSelectedNodeProvenance(
        NodeViewModel? node, string assetsRoot, ProvenanceNativeCall? nativeCall = null, IntPtr engineHandle = default)
    {
        if (node is null || string.IsNullOrWhiteSpace(assetsRoot))
            return Array.Empty<string>();
        var lines = new List<string>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var component in node.AllComponents)
        {
            if (!component.IsEnabled)
                continue;
            Dictionary<string, object> data;
            try
            {
                data = component.Serialize();
            }
            catch (Exception)
            {
                continue;
            }
            foreach (var pair in data)
            {
                if (!ProjectValidationService.AssetKeys.Contains(pair.Key) || pair.Value is not string asset)
                    continue;
                if (string.IsNullOrWhiteSpace(asset) || !seen.Add(asset))
                    continue;
                MediaConversionProvenance? provenance;
                try
                {
                    provenance = ResolveProvenance(asset, assetsRoot, nativeCall, engineHandle);
                }
                catch (Exception)
                {
                    continue;
                }
                if (provenance is null)
                    continue;
                lines.Add($"{asset}:");
                lines.AddRange(FormatDetails(provenance));
            }
        }
        return lines;
    }
}
