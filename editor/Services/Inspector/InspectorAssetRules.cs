using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace RowlEngine.Editor.Services.Inspector
{
    /// <summary>
    /// Faz 4 Dilim 4 — pure single-reference asset check for inline field
    /// validation. Mirrors the severity policy of
    /// <c>ProjectValidationService.ValidateSingleAssetReference</c> (outside
    /// project / converter-pending / unsupported / case-mismatch / missing
    /// are all build-blocking errors there, hence errors here too) but works
    /// per keystroke against a lightweight file-name set instead of a full
    /// project scan. Empty paths are optional-field silence (null, not ok).
    /// </summary>
    public static class InspectorAssetRules
    {
        /// <summary>
        /// Checks one asset reference. Returns the issue message, or null
        /// when the reference resolves (or the field is empty = optional).
        /// </summary>
        public static string? CheckSingleReference(
            string? asset, ISet<string> exactPaths, ISet<string> insensitivePaths,
            string? assetsRoot = null)
        {
            if (string.IsNullOrWhiteSpace(asset))
                return null;
            string reference = asset.Trim();
            if (IsOutsideProjectPath(reference))
                return $"Varlık '{reference}' proje Assets dizini dışını gösteriyor; Assets-göreli yol kullanın.";
            string ext = Path.GetExtension(reference.Replace('\\', '/'));
            if (MediaFormatCatalog.IsConverterPendingExtension(ext))
                return $"Varlık '{reference}' {ext.ToLowerInvariant()} dönüştürücü bekliyor (Faz 5'e kadar reddedilir). Kabul: PNG/JPEG/BMP/TGA, WAV/OGG, TTF/OTF.";
            if (MediaFormatCatalog.IsKnownUnsupportedMediaExtension(ext))
                return $"Varlık '{reference}' desteklenmeyen '{ext.ToLowerInvariant()}' biçiminde. Kabul: PNG/JPEG/BMP/TGA, WAV/OGG, TTF/OTF.";
            string normalized = reference.Replace('\\', '/');
            string[] candidates =
            {
                normalized, "images/" + normalized, "audio/" + normalized,
                "fonts/" + normalized, "scripts/" + normalized,
            };
            if (candidates.Any(exactPaths.Contains))
                return null;
            string? insensitiveHit = candidates
                .FirstOrDefault(c => insensitivePaths.Contains(c));
            if (insensitiveHit is not null)
                return $"Varlık '{reference}' birebir eşleşmiyor ('{insensitiveHit}' farklı harf büyüklüğünde bulundu). Yollar büyük/küçük harfe duyarlıdır.";
            // Fallback for files added after the last index refresh (import
            // mid-session): a direct hit clears the badge without a rescan.
            if (!string.IsNullOrWhiteSpace(assetsRoot) && Directory.Exists(assetsRoot))
            {
                string full = Path.GetFullPath(assetsRoot);
                foreach (string candidate in candidates)
                {
                    try
                    {
                        if (File.Exists(Path.Combine(full, candidate)))
                            return null;
                    }
                    catch (Exception)
                    {
                        break;
                    }
                }
            }
            return $"Varlık '{reference}' bulunamadı.";
        }

        internal static bool IsOutsideProjectPath(string asset)
        {
            if (Path.IsPathFullyQualified(asset)) return true;
            if (asset.Length >= 3 && char.IsLetter(asset[0]) && asset[1] == ':' &&
                (asset[2] == '\\' || asset[2] == '/')) return true;
            string normalized = asset.Replace('\\', '/');
            if (normalized.StartsWith('/')) return true;
            foreach (string segment in normalized.Split('/'))
                if (segment == "..") return true;
            return false;
        }
    }
}
