using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 5 Dilim 4 fix turu 1 — chapter feed (prod besleme hattı).
/// <see cref="ChapterStorageService"/> düzenindeki
/// (<c>&lt;assetsJson&gt;/chapters/chapter_index.json</c> + bölüm dosyaları;
/// dosya şeması için <c>ChapterStorageService.LoadChapterFile</c> referansı)
/// index + bölüm JSON'larını okuyup <c>NativeBridge</c>
/// <c>LoadChapterIndexJson</c>/<c>AppendChapterFileJson</c> delegelerine
/// fail-closed forward eder.
///
/// Kurallar: eksik dizin/dosya sessiz no-op + <c>log</c> (tanı rozeti
/// değil); bozuk JSON atlanır, kalan dosyalara devam edilir;
/// <c>host.Handle</c>/<c>IsInitialized</c> kontrolü (ölü handle no-op);
/// her native çağrı try/catch içindedir (throw yok). Kendi timer/thread'i
/// yoktur; çağrıcı (<c>EditorPlayModeCoordinator</c>) ne zaman isterse
/// çağırır. Sözleşme <c>docs/PREFETCH_AND_CHAPTERS_CONTRACT.md</c>'dedir;
/// çelişirse native kod kazanır.
/// </summary>
public static class PrefetchChapterFeedService
{
    /// <summary>
    /// <c>assetsJsonPath</c> altındaki chapter dizini adı
    /// (<c>MainWindowViewModel.SplitChaptersToFiles</c> ile aynı düzen).
    /// </summary>
    public const string ChaptersDirName = "chapters";

    /// <summary>
    /// Prod girişi: canlı <see cref="EngineHost"/> üzerinden besler.
    /// Ölü handle / eksik dizin sessiz no-op'tur (throw yok). Dönen değer
    /// forward edilen payload sayısıdır (index + bölüm dosyaları).
    /// </summary>
    public static int FeedFromAssetsDir(
        EngineHost? host, string? assetsJsonPath, Action<string>? log = null)
    {
        if (host is null)
            return 0;
        try
        {
            if (!host.IsInitialized || host.Handle == IntPtr.Zero)
                return 0;
            return FeedFromAssetsDir(
                assetsJsonPath,
                () => host.IsInitialized ? host.Handle : IntPtr.Zero,
                (handle, json) => (int)NativeBridge.RowlEngine_LoadChapterIndexJson(handle, json),
                (handle, json) => (int)NativeBridge.RowlEngine_AppendChapterFileJson(handle, json),
                log);
        }
        catch (Exception)
        {
            return 0;
        }
    }

    /// <summary>
    /// Seam girişi (headless test + delege mock): handle ve native
    /// delegeler çağrıcıdan alınır; native çağrı YOKTUR. Fail-closed:
    /// null delege/throw/bozuk girdi sessizce yoksayılır (throw yok).
    /// </summary>
    public static int FeedFromAssetsDir(
        string? assetsJsonPath,
        Func<IntPtr>? getHandle,
        Func<IntPtr, string, int>? loadIndexJson,
        Func<IntPtr, string, int>? appendChapterFileJson,
        Action<string>? log = null)
    {
        int forwarded = 0;
        try
        {
            if (string.IsNullOrWhiteSpace(assetsJsonPath) ||
                getHandle is null ||
                loadIndexJson is null ||
                appendChapterFileJson is null)
                return 0;
            IntPtr handle = getHandle();
            if (handle == IntPtr.Zero)
                return 0;

            string chaptersDir = Path.Combine(assetsJsonPath, ChaptersDirName);
            if (!Directory.Exists(chaptersDir))
            {
                log?.Invoke($"[Play] Chapter feed atlandı: dizin yok ({ChaptersDirName}).");
                return 0;
            }

            string indexPath = Path.Combine(chaptersDir, ChapterStorageService.IndexFileName);
            if (!File.Exists(indexPath))
            {
                log?.Invoke($"[Play] Chapter feed atlandı: {ChapterStorageService.IndexFileName} yok.");
                return 0;
            }

            string? indexJson = TryReadText(indexPath);
            if (string.IsNullOrWhiteSpace(indexJson))
            {
                log?.Invoke($"[Play] Chapter feed: index okunamadı, dosyalarla devam ediliyor.");
            }
            else if (!IsJsonObject(indexJson))
            {
                log?.Invoke($"[Play] Chapter feed: index bozuk, atlandı; dosyalarla devam ediliyor.");
                indexJson = null;
            }

            if (indexJson is not null)
            {
                try
                {
                    handle = getHandle();
                    if (handle == IntPtr.Zero)
                        return 0;
                    loadIndexJson(handle, indexJson);
                    forwarded++;
                }
                catch (Exception)
                {
                    log?.Invoke($"[Play] Chapter feed: index forward başarısız.");
                }
            }

            string[] chapterFiles;
            try
            {
                chapterFiles = Directory.EnumerateFiles(chaptersDir, "*.json")
                    .Where(f => !string.Equals(
                        Path.GetFileName(f),
                        ChapterStorageService.IndexFileName,
                        StringComparison.OrdinalIgnoreCase))
                    .OrderBy(f => f, StringComparer.Ordinal)
                    .ToArray();
            }
            catch (Exception)
            {
                return forwarded;
            }

            foreach (string file in chapterFiles)
            {
                string? chapterJson = TryReadText(file);
                if (string.IsNullOrWhiteSpace(chapterJson) ||
                    !IsChapterFileValid(chapterJson))
                {
                    log?.Invoke($"[Play] Chapter feed: bozuk bölüm atlandı ({Path.GetFileName(file)}).");
                    continue;
                }
                try
                {
                    handle = getHandle();
                    if (handle == IntPtr.Zero)
                        return forwarded;
                    appendChapterFileJson(handle, chapterJson);
                    forwarded++;
                }
                catch (Exception)
                {
                    log?.Invoke($"[Play] Chapter feed: forward başarısız ({Path.GetFileName(file)}).");
                }
            }

            return forwarded;
        }
        catch (Exception)
        {
            return forwarded;
        }
    }

    private static string? TryReadText(string path)
    {
        try
        {
            return File.ReadAllText(path);
        }
        catch (Exception)
        {
            return null;
        }
    }

    private static bool IsJsonObject(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            return document.RootElement.ValueKind == JsonValueKind.Object;
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>
    /// <c>ChapterStorageService.LoadChapterFile</c> aynası (hafif): kökte
    /// obje + boş-olmayan <c>chapter_id</c> gerekir (<c>nodes</c> yokluğu
    /// sorun değildir — loader boş dizi varsayar).
    /// </summary>
    private static bool IsChapterFileValid(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            JsonElement root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return false;
            if (!root.TryGetProperty("chapter_id", out JsonElement id) ||
                id.ValueKind != JsonValueKind.String)
                return false;
            return !string.IsNullOrEmpty(id.GetString());
        }
        catch (Exception)
        {
            return false;
        }
    }
}
