using System;
using System.Collections.Generic;
using System.IO;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 1 Dilim 1: MVP medya format sözleşmesinin editör tarafındaki tek kaynağı.
///
/// Kabul edilen: PNG/JPEG/BMP/TGA (görsel), WAV/OGG (ses), TTF/OTF (font).
/// MP3, FLAC ve WebP, Faz 5 dönüştürücü gelene kadar açık hatayla reddedilir.
/// Import, picker/preview, linter (ProjectValidationService) ve bu katalogun
/// Python aynası (tools/package_assets.py) aynı kümeleri kullanır; yeni bir
/// zincir eklenirken buraya danışmayan uzantı listesi yazılmaz.
/// </summary>
public static class MediaFormatCatalog
{
    // CONTRACT(accepted-image): mirrored by tools/package_assets.py ACCEPTED_IMAGE_EXTS;
    // tests/test_media_format_gate.py parses this block. One extension per line.
    public static IReadOnlySet<string> AcceptedImageExtensions { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        ".png",
        ".jpg",
        ".jpeg",
        ".bmp",
        ".tga",
    };

    // CONTRACT(accepted-audio): mirrored by tools/package_assets.py ACCEPTED_AUDIO_EXTS.
    public static IReadOnlySet<string> AcceptedAudioExtensions { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        ".wav",
        ".ogg",
    };

    // CONTRACT(accepted-font): mirrored by tools/package_assets.py ACCEPTED_FONT_EXTS.
    public static IReadOnlySet<string> AcceptedFontExtensions { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        ".ttf",
        ".otf",
    };

    // CONTRACT(converter-pending): mirrored by tools/package_assets.py CONVERTER_PENDING_EXTS.
    // Faz 5 dönüştürücü (MP3/FLAC -> OGG Vorbis, WebP -> PNG) gelene kadar reddedilir.
    public static IReadOnlySet<string> ConverterPendingExtensions { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        ".mp3",
        ".flac",
        ".webp",
    };

    // CONTRACT(known-unsupported): mirrored by tools/package_assets.py KNOWN_UNSUPPORTED_MEDIA_EXTS.
    // Bilinen ama MVP dışında kalan medya uzantıları (GIF dahil: native stb açsa bile sözleşme dışı).
    public static IReadOnlySet<string> KnownUnsupportedMediaExtensions { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        ".gif",
        ".psd",
        ".hdr",
        ".aiff",
        ".aif",
        ".m4a",
        ".wma",
        ".aac",
        ".opus",
        ".woff",
        ".woff2",
        ".eot",
    };

    public static string ImagePickerLabel => "Image Files (*.png, *.jpg, *.jpeg, *.bmp, *.tga)";

    public static string[] ImagePickerPatterns => new[] { "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga" };

    public static string AudioPickerLabel => "Audio Files (*.wav, *.ogg)";

    public static string[] AudioPickerPatterns => new[] { "*.wav", "*.ogg" };

    /// <summary>Dosya adı veya uzantıyı ".png" biçiminde normalize eder; uzantı yoksa "" döner.</summary>
    public static string NormalizeExtension(string? fileNameOrExt)
    {
        if (string.IsNullOrWhiteSpace(fileNameOrExt)) return string.Empty;
        string trimmed = fileNameOrExt.Trim();
        // Saf uzantı girdisi (".png") Path.GetExtension sürüm farklarına takılmasın.
        if (trimmed.StartsWith('.') && trimmed.IndexOf('.', 1) < 0
            && !trimmed.Contains('/') && !trimmed.Contains('\\'))
            return trimmed.ToLowerInvariant();
        return Path.GetExtension(trimmed).ToLowerInvariant();
    }

    public static bool IsSupportedImageExtension(string? fileNameOrExt)
        => AcceptedImageExtensions.Contains(NormalizeExtension(fileNameOrExt));

    public static bool IsSupportedAudioExtension(string? fileNameOrExt)
        => AcceptedAudioExtensions.Contains(NormalizeExtension(fileNameOrExt));

    public static bool IsSupportedFontExtension(string? fileNameOrExt)
        => AcceptedFontExtensions.Contains(NormalizeExtension(fileNameOrExt));

    public static bool IsAcceptedMediaExtension(string? fileNameOrExt)
    {
        string ext = NormalizeExtension(fileNameOrExt);
        return AcceptedImageExtensions.Contains(ext)
            || AcceptedAudioExtensions.Contains(ext)
            || AcceptedFontExtensions.Contains(ext);
    }

    public static bool IsConverterPendingExtension(string? fileNameOrExt)
        => ConverterPendingExtensions.Contains(NormalizeExtension(fileNameOrExt));

    public static bool IsKnownUnsupportedMediaExtension(string? fileNameOrExt)
        => KnownUnsupportedMediaExtensions.Contains(NormalizeExtension(fileNameOrExt));

    /// <summary>Açık red gerektiren uzantı: dönüştürücü bekleyen veya bilinen-desteklenmeyen medya.</summary>
    public static bool RequiresExplicitRejection(string? fileNameOrExt)
        => IsConverterPendingExtension(fileNameOrExt) || IsKnownUnsupportedMediaExtension(fileNameOrExt);

    /// <summary>Kabul edilen medya için standart alt dizini verir (images/audio/fonts).</summary>
    public static bool TryGetAssetSubdirectory(string fileNameOrExt, out string subdirectory)
    {
        string ext = NormalizeExtension(fileNameOrExt);
        if (AcceptedImageExtensions.Contains(ext)) { subdirectory = "images"; return true; }
        if (AcceptedAudioExtensions.Contains(ext)) { subdirectory = "audio"; return true; }
        if (AcceptedFontExtensions.Contains(ext)) { subdirectory = "fonts"; return true; }
        subdirectory = string.Empty;
        return false;
    }

    /// <summary>Reddedilen dosya için kullanıcıya gösterilecek açık mesajı üretir.</summary>
    public static string RejectionMessage(string? fileNameOrExt)
    {
        string name = string.IsNullOrWhiteSpace(fileNameOrExt) ? "(unnamed file)" : fileNameOrExt.Trim();
        string ext = NormalizeExtension(fileNameOrExt);
        const string accepted = "Accepted: PNG/JPEG/BMP/TGA, WAV/OGG, TTF/OTF.";
        if (ConverterPendingExtensions.Contains(ext))
            return $"'{name}' uses '{ext}', which needs the Faz 5 media converter (MP3/FLAC -> OGG, WebP -> PNG) and is rejected until then. {accepted}";
        return $"'{name}' uses unsupported format '{ext}'. {accepted}";
    }
}
