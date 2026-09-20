using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Faz 5 Dilim 4 — prefetch ilerleme rozet açıklaması.</summary>
public sealed record PrefetchChaptersDescription(bool Visible, string Text)
{
    public static readonly PrefetchChaptersDescription Hidden = new(false, string.Empty);
}

/// <summary>
/// Faz 5 Dilim 4 — prefetch ilerleme snapshot'ı. Native
/// <c>RowlEngine_GetPrefetchProgressJson</c> çıktısı
/// (<c>total_assets/ready_assets/missing_assets/queued_assets/ready_bytes/
/// budget_bytes/complete/missing_paths[]/last_diagnostic</c>, sayaçlar
/// non-negatif tam sayı).
/// </summary>
public sealed record PrefetchProgressSnapshot(
    long TotalAssets,
    long ReadyAssets,
    long MissingAssets,
    long QueuedAssets,
    long ReadyBytes,
    long BudgetBytes,
    bool Complete,
    IReadOnlyList<string> MissingPaths,
    string LastDiagnostic)
{
    public static readonly PrefetchProgressSnapshot Unknown = new(
        0, 0, 0, 0, 0, (long)PrefetchChaptersService.DefaultBudgetBytes,
        false, Array.Empty<string>(), string.Empty);
}

/// <summary>
/// Faz 5 Dilim 4 — bütçeli asset prefetch + chapter penceresi karar yüzeyi
/// (saf + polling-free, native çağrı YOKTUR). Motorun Dilim 4 C API
/// çıkışlarını (prefetch bütçesi/pump süresi, ilerleme JSON'u, aktif±1
/// komşuluk, chapter-id sözdizimi) tip güvenli kararlara çevirir.
///
/// <c>EngineHost</c>'a dokunmaz: canlı okuma, çağrıcının verdiği delege
/// üzerinden yapılır (null delege = ölü handle, fail-closed). Kendi
/// timer/thread'i yoktur; yoklama kadansını çağrıcı seçer (rozet mevcut
/// preview tick'inde en fazla 4 Hz yoklar). Bozuk/eksik girdi fail-closed
/// kapanır (throw yok). Kaynak:
/// <c>docs/PREFETCH_AND_CHAPTERS_CONTRACT.md</c>; çelişirse native kod
/// kazanır.
/// </summary>
public static class PrefetchChaptersService
{
    /// <summary>Varsayılan prefetch bütçesi (native <c>kPrefetchDefaultBudgetBytes</c>).</summary>
    public const ulong DefaultBudgetBytes = 32UL * 1024 * 1024;

    /// <summary>Prefetch bütçe tavanı (native <c>kPrefetchMaxBudgetBytes</c>).</summary>
    public const ulong MaxBudgetBytes = 128UL * 1024 * 1024;

    /// <summary>Varsayılan pump süresi, ms (native <c>kPrefetchDefaultPumpMilliseconds</c>).</summary>
    public const double DefaultPumpMilliseconds = 4.0;

    /// <summary>Pump süresi tavanı, ms (native <c>kPrefetchMaxPumpMilliseconds</c>).</summary>
    public const double MaxPumpMilliseconds = 50.0;

    /// <summary>Komşuluk yarıçapı (native <c>kChapterNeighborDistance</c>): resident = aktif±1.</summary>
    public const int NeighborDistance = 1;

    /// <summary>Chapter-id tavanı, UTF-8 baytı (native <c>kMaxGraphIdChars</c>).</summary>
    public const int MaxChapterIdChars = 128;

    /// <summary>Rozet yoklama aralığı tabanı (≤4 Hz polling).</summary>
    public static readonly TimeSpan PollInterval = TimeSpan.FromMilliseconds(250);

    /// <summary>Paketdeki chapter dosyaları öneki (Assets köküne göre).</summary>
    public const string ChaptersPackagePrefix = "json/chapters/";

    // Bütçe aynası

    /// <summary>
    /// Bütçeyi native formüle clamp'ler: 0 → varsayılan (32 MiB), aksi halde
    /// min(bütçe, 128 MiB). Asla reddedilmez.
    /// </summary>
    public static ulong ClampBudgetBytes(ulong budgetBytes) =>
        budgetBytes == 0 ? DefaultBudgetBytes : Math.Min(budgetBytes, MaxBudgetBytes);

    /// <summary>
    /// Pump süresini native formüle clamp'ler: non-finite / &lt;= 0 →
    /// ~4 ms varsayılan, aksi halde min(süre, 50 ms).
    /// </summary>
    public static double ClampPumpMilliseconds(double maxMilliseconds) =>
        !double.IsFinite(maxMilliseconds) || maxMilliseconds <= 0.0
            ? DefaultPumpMilliseconds
            : Math.Min(maxMilliseconds, MaxPumpMilliseconds);

    // Chapter-id doğrulama

    /// <summary>
    /// Chapter-id sözdizimi (native <c>checkChapterId</c> aynası): boş değil,
    /// NUL baytı yok, UTF-8 ile en fazla <see cref="MaxChapterIdChars"/> bayt.
    /// </summary>
    public static bool IsChapterIdValid(string? chapterId)
    {
        if (string.IsNullOrEmpty(chapterId))
            return false;
        if (chapterId.IndexOf('\0') >= 0)
            return false;
        return Encoding.UTF8.GetByteCount(chapterId) <= MaxChapterIdChars;
    }

    // Komşuluk kararları (saf)

    /// <summary>
    /// Resident pencere (native <c>applyWindow</c> aynası): sıralı chapter
    /// listesinde aktif chapter + ±<see cref="NeighborDistance"/> komşuları,
    /// uçlarda kırpılır. Bilinmeyen/boş aktif fail-closed boş döner.
    /// </summary>
    public static IReadOnlyList<string> ComputeResidentWindow(
        IReadOnlyList<string>? orderedChapters, string? activeChapterId)
    {
        var window = new List<string>();
        int index = IndexOf(orderedChapters, activeChapterId);
        if (index < 0 || orderedChapters is null)
            return window;
        for (int i = index - NeighborDistance; i <= index + NeighborDistance; i++)
        {
            if (i >= 0 && i < orderedChapters.Count)
                window.Add(orderedChapters[i]);
        }
        return window;
    }

    /// <summary>
    /// Prefetch tetik penceresi (native <c>PrefetchChapterAssets</c> aynası):
    /// istenen chapter + sonraki chapter (aktif + sonraki sahne). Son
    /// chapter'da yalnız kendisi; bilinmeyen/boş girdi fail-closed boş döner.
    /// </summary>
    public static IReadOnlyList<string> ComputePrefetchWindow(
        IReadOnlyList<string>? orderedChapters, string? requestedChapterId)
    {
        var window = new List<string>();
        int index = IndexOf(orderedChapters, requestedChapterId);
        if (index < 0 || orderedChapters is null)
            return window;
        window.Add(orderedChapters[index]);
        if (index + 1 < orderedChapters.Count)
            window.Add(orderedChapters[index + 1]);
        return window;
    }

    /// <summary>
    /// Chapter-değişiminde prefetch tetik kararı: geçerli ve öncekinden
    /// farklı bir chapter'a geçildiyse tetiklenecek id'yi döner, aksi halde
    /// null (sessiz fail-closed, tetik yok).
    /// </summary>
    public static string? DecidePrefetchTrigger(
        IReadOnlyList<string>? orderedChapters, string? previousActive, string? currentActive)
    {
        if (!IsChapterIdValid(currentActive) ||
            string.Equals(previousActive, currentActive, StringComparison.Ordinal))
            return null;
        if (IndexOf(orderedChapters, currentActive) < 0 && orderedChapters?.Count > 0)
            return null;
        return currentActive;
    }

    // İlerleme parse

    /// <summary>
    /// İlerleme JSON'unu snapshot'a çevirir. Null/boş/bozuk girdi, kökte
    /// obje-dışılık, eksik/negatif/kesirli/yanlış-tipli alan fail-closed
    /// <see cref="PrefetchProgressSnapshot.Unknown"/> döner (throw yok).
    /// </summary>
    public static PrefetchProgressSnapshot ParseProgress(string? progressJson) =>
        TryParseProgress(progressJson, out PrefetchProgressSnapshot snapshot)
            ? snapshot
            : PrefetchProgressSnapshot.Unknown;

    /// <summary>
    /// <see cref="ParseProgress"/> ile aynı katı eşlemeyi yapar; bozuk
    /// JSON/null girdi için <c>false</c> +
    /// <see cref="PrefetchProgressSnapshot.Unknown"/> döner.
    /// </summary>
    public static bool TryParseProgress(string? progressJson, out PrefetchProgressSnapshot snapshot)
    {
        snapshot = PrefetchProgressSnapshot.Unknown;
        if (string.IsNullOrWhiteSpace(progressJson))
            return false;
        try
        {
            using var document = JsonDocument.Parse(progressJson);
            JsonElement root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return false;
            if (!root.TryGetProperty("complete", out JsonElement completeElement) ||
                (completeElement.ValueKind != JsonValueKind.True &&
                 completeElement.ValueKind != JsonValueKind.False))
                return false;
            snapshot = new PrefetchProgressSnapshot(
                ReadNonNegative(root, "total_assets"),
                ReadNonNegative(root, "ready_assets"),
                ReadNonNegative(root, "missing_assets"),
                ReadNonNegative(root, "queued_assets"),
                ReadNonNegative(root, "ready_bytes"),
                ReadNonNegative(root, "budget_bytes"),
                completeElement.GetBoolean(),
                ReadMissingPaths(root),
                ReadDiagnostic(root));
            return true;
        }
        catch (Exception)
        {
            snapshot = PrefetchProgressSnapshot.Unknown;
            return false;
        }
    }

    // Delege okumaları (null/throw fail-closed)

    /// <summary>
    /// Verilen okuyucudan ilerleme snapshot'ı alır (polling-free: çağrıcı ne
    /// zaman isterse çağırır). Null delege/throw/boş JSON fail-closed
    /// <see cref="PrefetchProgressSnapshot.Unknown"/> döner.
    /// </summary>
    public static PrefetchProgressSnapshot ReadProgressSnapshot(Func<string?>? readJson)
    {
        if (readJson is null)
            return PrefetchProgressSnapshot.Unknown;
        try
        {
            return ParseProgress(readJson());
        }
        catch (Exception)
        {
            return PrefetchProgressSnapshot.Unknown;
        }
    }

    /// <summary>
    /// Ham loaded-chapters JSON'unu okur. Null delege/throw fail-closed ""
    /// döner (şema yorumu çağrıcıya aittir).
    /// </summary>
    public static string ReadLoadedChaptersJson(Func<string?>? readJson)
    {
        if (readJson is null)
            return string.Empty;
        try
        {
            return readJson() ?? string.Empty;
        }
        catch (Exception)
        {
            return string.Empty;
        }
    }

    /// <summary>
    /// Ham <c>RowlEngine_IsChapterBoundaryNode</c> çıktısını bool karara
    /// çevirir. Null delege (ölü handle) ve throw fail-closed <c>false</c>
    /// döner.
    /// </summary>
    public static bool ReadIsChapterBoundaryNode(Func<int>? readRaw)
    {
        if (readRaw is null)
            return false;
        try
        {
            return readRaw() != 0;
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>
    /// Ham <c>RowlEngine_PumpPrefetch</c> çıktısını (yeni-hazır sayısı)
    /// karara çevirir. Null delege/throw fail-closed 0 döner; negatif ham
    /// değer 0'a sabitlenir.
    /// </summary>
    public static int ReadPumpNewlyReady(Func<int>? readRaw)
    {
        if (readRaw is null)
            return 0;
        try
        {
            return Math.Max(0, readRaw());
        }
        catch (Exception)
        {
            return 0;
        }
    }

    // Rozet

    /// <summary>
    /// İlerleme JSON'unu hazır/eksik/byte rozetine çevirir. Geçerli girdi
    /// yoksa/bozuksa ya da toplam sıfırsa gizli döner (throw yok).
    /// </summary>
    public static PrefetchChaptersDescription Describe(string? progressJson)
    {
        if (!TryParseProgress(progressJson, out PrefetchProgressSnapshot snapshot) ||
            snapshot.TotalAssets <= 0)
            return PrefetchChaptersDescription.Hidden;
        string text = snapshot.Complete
            ? $"PREFETCH • hazır {snapshot.ReadyAssets}/{snapshot.TotalAssets}" +
              (snapshot.MissingAssets > 0 ? $" • {snapshot.MissingAssets} eksik" : string.Empty) +
              $" • {snapshot.ReadyBytes} B"
            : $"PREFETCH • {snapshot.ReadyAssets}/{snapshot.TotalAssets}" +
              $" • {snapshot.MissingAssets} eksik • {snapshot.ReadyBytes} B";
        return new PrefetchChaptersDescription(true, text);
    }

    /// <summary>Rozet metni; tek kaynak <see cref="Describe"/>.</summary>
    public static string ReadBadgeText(Func<string?>? readJson)
    {
        if (readJson is null)
            return string.Empty;
        try
        {
            return Describe(readJson()).Text;
        }
        catch (Exception)
        {
            return string.Empty;
        }
    }

    // Paket referans kümesi

    /// <summary>
    /// Paket referans dosyası mı? <see cref="ChaptersPackagePrefix"/> altındaki
    /// JSON chapter dosyaları (chapter_index.json dahil) paketin parçasıdır;
    /// kullanılmayan sayılmazlar, yeni format yoktur (mevcut
    /// chapter_index.json + LoadChapterFile şeması).
    /// </summary>
    public static bool IsChapterPackageFile(string? diskRelativePath)
    {
        if (string.IsNullOrEmpty(diskRelativePath))
            return false;
        string normalized = diskRelativePath.Replace('\\', '/');
        return normalized.StartsWith(ChaptersPackagePrefix, StringComparison.Ordinal) &&
               normalized.EndsWith(".json", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// Disk göreli yol listesinden chapter paket referanslarını süzer
    /// (linter referans kümesine eklemek için; throw yok).
    /// </summary>
    public static IReadOnlyList<string> CollectChapterPackageRefs(IEnumerable<string>? diskRelativePaths)
    {
        var refs = new List<string>();
        if (diskRelativePaths is null)
            return refs;
        try
        {
            foreach (string? path in diskRelativePaths)
            {
                if (IsChapterPackageFile(path))
                    refs.Add(path!);
            }
        }
        catch (Exception)
        {
            // Bozuk liste paket taramasını düşürmez.
        }
        return refs;
    }

    // Özel yardımcılar

    private static int IndexOf(IReadOnlyList<string>? ordered, string? chapterId)
    {
        if (ordered is null || string.IsNullOrEmpty(chapterId))
            return -1;
        for (int i = 0; i < ordered.Count; i++)
        {
            if (string.Equals(ordered[i], chapterId, StringComparison.Ordinal))
                return i;
        }
        return -1;
    }

    private static long ReadNonNegative(JsonElement root, string property)
    {
        if (!root.TryGetProperty(property, out JsonElement element))
            throw new JsonException($"Missing prefetch field '{property}'.");
        if (element.ValueKind != JsonValueKind.Number || !element.TryGetInt64(out long value))
            throw new JsonException($"Prefetch field '{property}' is not an integer.");
        if (value < 0)
            throw new JsonException($"Prefetch field '{property}' is negative.");
        return value;
    }

    private static IReadOnlyList<string> ReadMissingPaths(JsonElement root)
    {
        if (!root.TryGetProperty("missing_paths", out JsonElement element) ||
            element.ValueKind != JsonValueKind.Array)
            throw new JsonException("Prefetch field 'missing_paths' is not an array.");
        var paths = new List<string>();
        foreach (JsonElement item in element.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String)
                throw new JsonException("Prefetch field 'missing_paths' holds a non-string.");
            paths.Add(item.GetString() ?? string.Empty);
        }
        return paths;
    }

    private static string ReadDiagnostic(JsonElement root)
    {
        if (!root.TryGetProperty("last_diagnostic", out JsonElement element) ||
            element.ValueKind != JsonValueKind.String)
            throw new JsonException("Prefetch field 'last_diagnostic' is not a string.");
        return element.GetString() ?? string.Empty;
    }
}
