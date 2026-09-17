using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 5 Dilim 5 — SourceAssets→Assets dönüştürücü hattı (saf kararlar +
/// ince proses sarmalayıcı). <c>SourceAssets/</c> altındaki
/// <c>.mp3/.flac/.webp</c> kaynaklarını harici araçlarla
/// (<c>rowl_oggenc</c> → OGG, <c>rowl_webp2png</c> → PNG) <c>Assets/</c>
/// altına dönüştürür; her çıktının yanına araç tarafından yazılan
/// <c>&lt;çıktı&gt;.rowlconv.json</c> sidecar konur. Kaynak dosya asla
/// üzerine yazılmaz; aynı kaynak+araç sürümü bayt-identical çıktı verir,
/// hash eşleşirse dönüştürme atlanır (incremental-skip).
/// <para>
/// CLI/sözleşme kaynağı: <c>docs/MEDIA_CONVERTERS_CONTRACT.md</c> + araç
/// kaynakları (<c>tools/rowl_oggenc.c</c>, <c>tools/rowl_webp2png.c</c>) +
/// <c>engine/include/rowl/c_api.h</c>. Çelişirse KAYNAK kazanır:
/// <list type="bullet">
/// <item><c>rowl_oggenc [--rate HZ] [--channels N] -o OUT.ogg --sidecar FILE [IN.pcm|-]</c>
/// (girdi ham s16le PCM; MP3/FLAC önce harici decode edilir).</item>
/// <item><c>rowl_webp2png -o OUT.png --sidecar FILE IN.webp</c>.</item>
/// <item>Decode yalnızca harici prosesledir (ffmpeg GPL'e link YOK):
/// <c>ffmpeg -hide_banner -loglevel error -i IN -ar 44100 -ac 2 -sample_fmt s16 -f s16le OUT.pcm</c>.</item>
/// <item>OGG sidecar'daki <c>source_sha256</c>, decode edilmiş PCM
/// baytlarının hash'idir (ham MP3/FLAC değil).</item>
/// </list>
/// </para>
/// </summary>
public static class MediaConverterService
{
    public const string OggToolName = "rowl_oggenc";
    public const string WebpToolName = "rowl_webp2png";
    public const string FfmpegToolName = "ffmpeg";
    public const string SidecarSuffix = ".rowlconv.json";

    /// <summary>Sözleşme varsayılanları: 44100 Hz, 2 kanal (araç + decode aynı).</summary>
    public const int PcmSampleRateHz = 44100;
    public const int PcmChannels = 2;

    /// <summary>Harici proses zaman aşımı üst sınırı.</summary>
    public static readonly TimeSpan DefaultToolTimeout = TimeSpan.FromSeconds(120);

    /// <summary>Dönüştürme kararı.</summary>
    public enum ConversionOutcome
    {
        Converted,
        SkippedUpToDate,
        Failed,
    }

    public sealed record ConversionResult(
        ConversionOutcome Outcome,
        string OutputRelativePath,
        string Message);

    public sealed record ConverterOptions(
        string? OggToolPath = null,
        string? WebpToolPath = null,
        string? FfmpegPath = null,
        TimeSpan? ToolTimeout = null,
        Func<ProcessStartInfo, CancellationToken, Task<ExternalToolResult>>? Runner = null);

    /// <summary>Kaynak uzantı → (çıktı uzantısı, Assets alt dizini, araç adı).</summary>
    public static bool TryGetConversionTarget(string? fileNameOrExt,
        out string outputExtension, out string outputSubdirectory, out string toolName)
    {
        string ext = MediaFormatCatalog.NormalizeExtension(fileNameOrExt);
        switch (ext)
        {
            case ".mp3":
            case ".flac":
                outputExtension = ".ogg";
                outputSubdirectory = "audio";
                toolName = OggToolName;
                return true;
            case ".webp":
                outputExtension = ".png";
                outputSubdirectory = "images";
                toolName = WebpToolName;
                return true;
            default:
                outputExtension = string.Empty;
                outputSubdirectory = string.Empty;
                toolName = string.Empty;
                return false;
        }
    }

    /// <summary>
    /// Kaynak referansı → dönüştürülmüş çıktı adayları
    /// (örn. <c>theme.mp3</c> → <c>audio/theme.ogg</c>). Doğrulayıcılar
    /// (build + inline) aynı adayları kullanır; yeni zincir buraya danışır.
    /// </summary>
    public static IEnumerable<string> ConvertedOutputCandidates(string? assetRef)
    {
        if (string.IsNullOrWhiteSpace(assetRef))
            yield break;
        string normalized = assetRef.Replace('\\', '/').Trim();
        if (!TryGetConversionTarget(normalized, out string outExt, out string outDir, out _))
            yield break;
        string stem = Path.ChangeExtension(normalized, outExt).Replace('\\', '/');
        yield return stem;
        yield return outDir + "/" + Path.GetFileName(stem);
        string bare = Path.GetFileName(stem);
        if (!string.Equals(bare, stem, StringComparison.Ordinal))
            yield return bare;
    }

    public static string SidecarPathForOutput(string outputFullPath)
        => outputFullPath + SidecarSuffix;

    /// <summary>
    /// Dönüştürülmüş çıktı yolu kuralı (tek nokta — Faz 5 Dilim 5 fix:
    /// bulk/single-import tutarlılığı). Kural: <c>Assets/{outDir}/{göreli}</c>.
    /// Bulk tarafı SourceAssets-göreli yolu verir (ağaç korunur, çakışma olmaz);
    /// tek-dosya import tarafı yalın dosya adı verir (dışarıdan bırakılan dosyanın
    /// ağacı yoktur → doğası gereği flat). Formül aynıdır, girdi bilgisi farklıdır.
    /// </summary>
    public static string BuildConvertedOutputFullPath(
        string assetsRoot, string outputSubdirectory, string sourceRelativePath)
    {
        // Path.Combine does NOT split embedded separators: "sfx/theme.ogg"
        // would glue into "...\audio\sfx/theme.ogg" on Windows. Normalize
        // to the OS separator first (no-op on Linux) so bulk and
        // single-import outputs are identical full paths (tur-6).
        string file = sourceRelativePath
            .Replace('\\', Path.DirectorySeparatorChar)
            .Replace('/', Path.DirectorySeparatorChar);
        return Path.Combine(assetsRoot, outputSubdirectory, file);
    }

    /// <summary>
    /// Tool sidecar'ı yazdıktan sonra C# <c>source_path</c>'i damgalar (Faz 5
    /// Dilim 5 fix: tool'lar pratikte <c>source_path</c> yazmaz). Damga
    /// hash-dışı metadata'dır: <c>output_sha256</c> yeniden hesaplanMAZ, çıktı
    /// baytları değişmez → determinizm etkilenmez. Linter/paket-manifestosu bu
    /// damgayı kullanır; damga yoksa gövde-adı aramasına düşülür (fail-open).
    /// Best-effort'tur (IO hatası yutulur — dönüşüm zaten doğrulanmıştır).
    /// </summary>
    internal static void StampSourcePath(string outputFullPath, string? sourcePathStamp)
    {
        if (string.IsNullOrWhiteSpace(sourcePathStamp))
            return;
        string sidecarPath = SidecarPathForOutput(outputFullPath);
        try
        {
            string json = File.ReadAllText(sidecarPath);
            if (JsonNode.Parse(json) is not JsonObject node)
                return;
            node["source_path"] = sourcePathStamp.Replace('\\', '/');
            File.WriteAllText(sidecarPath, node.ToJsonString() + "\n");
        }
        catch (Exception)
        {
            // Damga düşerse gövde-adı araması devralır; dönüşüm sonucu korunur.
        }
    }

    public static string ComputeFileSha256(string fullPath)
    {
        using var stream = File.OpenRead(fullPath);
        using var sha = SHA256.Create();
        return Convert.ToHexString(sha.ComputeHash(stream)).ToLowerInvariant();
    }

    /// <summary>
    /// Artımlı-atlama kararı (saf): sidecar geçerli + verilen kaynak hash'i
    /// (WebP'de dosya baytları, seste decode PCM baytları) ve çıktı hash'i
    /// tutuyor + beklenen araç adı eşleşiyor + sürüm boş değilse atla.
    /// </summary>
    public static bool IsSidecarFreshForHashes(
        string outputFullPath, string expectedToolName, string sourceHash)
    {
        if (!File.Exists(outputFullPath) || string.IsNullOrWhiteSpace(sourceHash))
            return false;
        MediaConversionProvenance? provenance =
            MediaConversionProvenance.TryReadFile(SidecarPathForOutput(outputFullPath));
        if (provenance is null)
            return false;
        if (!string.Equals(provenance.ConverterName, expectedToolName, StringComparison.Ordinal))
            return false;
        if (string.IsNullOrWhiteSpace(provenance.ConverterVersion))
            return false;
        string outputHash;
        try
        {
            outputHash = ComputeFileSha256(outputFullPath);
        }
        catch (Exception)
        {
            return false;
        }
        return string.Equals(provenance.SourceSha256, sourceHash, StringComparison.OrdinalIgnoreCase)
            && string.Equals(provenance.OutputSha256, outputHash, StringComparison.OrdinalIgnoreCase);
    }

    // ── CLI inşası (tek nokta; sözleşme sırası) ────────────────

    /// <summary>Sözleşme: <c>rowl_webp2png -o OUT.png --sidecar FILE IN.webp</c>.</summary>
    public static ProcessStartInfo BuildWebpStartInfo(
        string toolPath, string sourceFullPath, string outputFullPath, string sidecarFullPath)
    {
        var startInfo = new ProcessStartInfo { FileName = toolPath };
        startInfo.ArgumentList.Add("-o");
        startInfo.ArgumentList.Add(outputFullPath);
        startInfo.ArgumentList.Add("--sidecar");
        startInfo.ArgumentList.Add(sidecarFullPath);
        startInfo.ArgumentList.Add(sourceFullPath);
        return startInfo;
    }

    /// <summary>Sözleşme: <c>rowl_oggenc [--rate HZ] [--channels N] -o OUT.ogg --sidecar FILE [IN.pcm|-]</c>.</summary>
    public static ProcessStartInfo BuildOggStartInfo(
        string toolPath, string pcmFullPath, string outputFullPath, string sidecarFullPath,
        int rateHz = PcmSampleRateHz, int channels = PcmChannels)
    {
        var startInfo = new ProcessStartInfo { FileName = toolPath };
        startInfo.ArgumentList.Add("--rate");
        startInfo.ArgumentList.Add(rateHz.ToString(System.Globalization.CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("--channels");
        startInfo.ArgumentList.Add(channels.ToString(System.Globalization.CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("-o");
        startInfo.ArgumentList.Add(outputFullPath);
        startInfo.ArgumentList.Add("--sidecar");
        startInfo.ArgumentList.Add(sidecarFullPath);
        startInfo.ArgumentList.Add(pcmFullPath);
        return startInfo;
    }

    /// <summary>Sözleşme decode: <c>ffmpeg -hide_banner -loglevel error -i IN -ar 44100 -ac 2 -sample_fmt s16 -f s16le OUT.pcm</c>.</summary>
    public static ProcessStartInfo BuildFfmpegDecodeStartInfo(
        string ffmpegPath, string sourceFullPath, string pcmFullPath,
        int rateHz = PcmSampleRateHz, int channels = PcmChannels)
    {
        var startInfo = new ProcessStartInfo { FileName = ffmpegPath };
        startInfo.ArgumentList.Add("-hide_banner");
        startInfo.ArgumentList.Add("-loglevel");
        startInfo.ArgumentList.Add("error");
        startInfo.ArgumentList.Add("-i");
        startInfo.ArgumentList.Add(sourceFullPath);
        startInfo.ArgumentList.Add("-ar");
        startInfo.ArgumentList.Add(rateHz.ToString(System.Globalization.CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("-ac");
        startInfo.ArgumentList.Add(channels.ToString(System.Globalization.CultureInfo.InvariantCulture));
        startInfo.ArgumentList.Add("-sample_fmt");
        startInfo.ArgumentList.Add("s16");
        startInfo.ArgumentList.Add("-f");
        startInfo.ArgumentList.Add("s16le");
        startInfo.ArgumentList.Add(pcmFullPath);
        return startInfo;
    }

    internal static string ResolveToolPath(string toolName, string? explicitPath, Func<string, string?>? getEnv = null)
    {
        if (!string.IsNullOrWhiteSpace(explicitPath))
            return explicitPath;
        getEnv ??= Environment.GetEnvironmentVariable;
        string envKey = toolName switch
        {
            OggToolName => "ROWL_OGGENC_PATH",
            WebpToolName => "ROWL_WEBP2PNG_PATH",
            FfmpegToolName => "FFMPEG_PATH",
            _ => string.Empty,
        };
        string? fromEnv = null;
        if (!string.IsNullOrEmpty(envKey))
        {
            try { fromEnv = getEnv(envKey); } catch (Exception) { }
        }
        if (!string.IsNullOrWhiteSpace(fromEnv))
            return fromEnv;
        return toolName;
    }

    internal static string ResolveToolPathFor(string toolName, ConverterOptions options)
        => ResolveToolPath(toolName, toolName switch
        {
            OggToolName => options.OggToolPath,
            WebpToolName => options.WebpToolPath,
            FfmpegToolName => options.FfmpegPath,
            _ => null,
        });

    // ── Dönüştürme ─────────────────────────────────────────────

    /// <summary>
    /// Tek dosyayı dönüştürür (fail-closed: timeout/exit-code/sidecar
    /// uyuşmazlığı → <see cref="ConversionOutcome.Failed"/>, throw yok).
    /// Ses kaynakları önce harici ffmpeg ile PCM'e çözülür (ara PCM temp
    /// dosyadadır, işlem sonunda silinir). Çıktı yolu
    /// <paramref name="outputFullPath"/>; üst dizin açılır.
    /// </summary>
    public static async Task<ConversionResult> ConvertFileAsync(
        string sourceFullPath,
        string outputFullPath,
        string toolName,
        ConverterOptions? options = null,
        Action<string>? log = null,
        CancellationToken cancellationToken = default,
        string? sourcePathStamp = null)
    {
        options ??= new ConverterOptions();
        string outputRelative = Path.GetFileName(outputFullPath);
        if (!File.Exists(sourceFullPath))
            return new(ConversionOutcome.Failed, outputRelative, $"Kaynak bulunamadı: '{sourceFullPath}'.");

        string? directory = Path.GetDirectoryName(outputFullPath);
        if (!string.IsNullOrEmpty(directory))
            Directory.CreateDirectory(directory);
        string sidecarFullPath = SidecarPathForOutput(outputFullPath);

        if (string.Equals(toolName, WebpToolName, StringComparison.Ordinal))
        {
            string sourceHash;
            try
            {
                sourceHash = ComputeFileSha256(sourceFullPath);
            }
            catch (Exception error)
            {
                return new(ConversionOutcome.Failed, outputRelative, $"Kaynak hash'i okunamadı ({error.GetType().Name}).");
            }
            try
            {
                if (IsSidecarFreshForHashes(outputFullPath, toolName, sourceHash))
                    return new(ConversionOutcome.SkippedUpToDate, outputRelative, $"Atlandı (güncel): '{outputRelative}'.");
            }
            catch (Exception)
            {
                // Kararsızlık = dönüştür (fail-safe yeniden üretim).
            }
            string toolPath = ResolveToolPathFor(toolName, options);
            ConversionResult result = await RunToolAsync(
                BuildWebpStartInfo(toolPath, sourceFullPath, outputFullPath, sidecarFullPath),
                toolPath, options, log, cancellationToken).ConfigureAwait(false);
            if (result.Outcome == ConversionOutcome.Failed)
                return result;
            string? verifyError = VerifyFreshOutput(sourceFullPath, sourceHash, outputFullPath, toolName);
            if (verifyError is not null)
                return new(ConversionOutcome.Failed, outputRelative, verifyError);
            StampSourcePath(outputFullPath, sourcePathStamp ?? Path.GetFileName(sourceFullPath));
            log?.Invoke($"🔄 Dönüştürüldü: {Path.GetFileName(sourceFullPath)} -> {outputRelative}");
            return new(ConversionOutcome.Converted, outputRelative, $"Dönüştürüldü: '{outputRelative}'.");
        }

        if (string.Equals(toolName, OggToolName, StringComparison.Ordinal))
            return await ConvertAudioAsync(
                sourceFullPath, outputFullPath, sidecarFullPath, options, log, cancellationToken, sourcePathStamp).ConfigureAwait(false);

        return new(ConversionOutcome.Failed, outputRelative, $"Bilinmeyen dönüştürücü: '{toolName}'.");
    }

    private static async Task<ConversionResult> ConvertAudioAsync(
        string sourceFullPath,
        string outputFullPath,
        string sidecarFullPath,
        ConverterOptions options,
        Action<string>? log,
        CancellationToken cancellationToken,
        string? sourcePathStamp = null)
    {
        string outputRelative = Path.GetFileName(outputFullPath);
        string pcmTemp = Path.Combine(
            Path.GetTempPath(), $"RowlPcm_{Guid.NewGuid():N}.pcm");
        try
        {
            string ffmpegPath = ResolveToolPathFor(FfmpegToolName, options);
            ConversionResult decode = await RunToolAsync(
                BuildFfmpegDecodeStartInfo(ffmpegPath, sourceFullPath, pcmTemp),
                ffmpegPath, options, log, cancellationToken).ConfigureAwait(false);
            if (decode.Outcome == ConversionOutcome.Failed)
                return new(ConversionOutcome.Failed, outputRelative,
                    $"Ses decode edilemedi ('{ffmpegPath}'): {decode.Message}");
            if (!File.Exists(pcmTemp))
                return new(ConversionOutcome.Failed, outputRelative,
                    $"Ses decode çıktısı üretilmedi ('{ffmpegPath}').");

            string pcmHash;
            try
            {
                pcmHash = ComputeFileSha256(pcmTemp);
            }
            catch (Exception error)
            {
                return new(ConversionOutcome.Failed, outputRelative, $"PCM hash'i okunamadı ({error.GetType().Name}).");
            }
            try
            {
                if (IsSidecarFreshForHashes(outputFullPath, OggToolName, pcmHash))
                    return new(ConversionOutcome.SkippedUpToDate, outputRelative, $"Atlandı (güncel): '{outputRelative}'.");
            }
            catch (Exception)
            {
                // Kararsızlık = dönüştür.
            }
            string oggPath = ResolveToolPathFor(OggToolName, options);
            ConversionResult encode = await RunToolAsync(
                BuildOggStartInfo(oggPath, pcmTemp, outputFullPath, sidecarFullPath),
                oggPath, options, log, cancellationToken).ConfigureAwait(false);
            if (encode.Outcome == ConversionOutcome.Failed)
                return encode;
            // Sözleşme: OGG sidecar source_sha256 = decode PCM baytları.
            string? verifyError = VerifyFreshOutput(pcmTemp, pcmHash, outputFullPath, OggToolName);
            if (verifyError is not null)
                return new(ConversionOutcome.Failed, outputRelative, verifyError);
            StampSourcePath(outputFullPath, sourcePathStamp ?? Path.GetFileName(sourceFullPath));
            log?.Invoke($"🔄 Dönüştürüldü: {Path.GetFileName(sourceFullPath)} -> {outputRelative}");
            return new(ConversionOutcome.Converted, outputRelative, $"Dönüştürüldü: '{outputRelative}'.");
        }
        finally
        {
            try { if (File.Exists(pcmTemp)) File.Delete(pcmTemp); } catch (Exception) { }
        }
    }

    /// <summary>
    /// Ses kaynağını harici decode ile PCM hash'ine çözer (linter kaynak
    /// kolu; decode edilemezse null). Temp PCM silinir.
    /// </summary>
    internal static async Task<string?> TryDecodeSourceHashAsync(
        string sourceFullPath,
        ConverterOptions options,
        CancellationToken cancellationToken = default)
    {
        string pcmTemp = Path.Combine(Path.GetTempPath(), $"RowlPcm_{Guid.NewGuid():N}.pcm");
        try
        {
            string ffmpegPath = ResolveToolPathFor(FfmpegToolName, options);
            ConversionResult decode = await RunToolAsync(
                BuildFfmpegDecodeStartInfo(ffmpegPath, sourceFullPath, pcmTemp),
                ffmpegPath, options, null, cancellationToken).ConfigureAwait(false);
            if (decode.Outcome == ConversionOutcome.Failed || !File.Exists(pcmTemp))
                return null;
            return ComputeFileSha256(pcmTemp);
        }
        catch (Exception)
        {
            return null;
        }
        finally
        {
            try { if (File.Exists(pcmTemp)) File.Delete(pcmTemp); } catch (Exception) { }
        }
    }

    private static async Task<ConversionResult> RunToolAsync(
        ProcessStartInfo startInfo,
        string toolPath,
        ConverterOptions options,
        Action<string>? log,
        CancellationToken cancellationToken)
    {
        Func<ProcessStartInfo, CancellationToken, Task<ExternalToolResult>> runner =
            options.Runner ?? DefaultRunnerAsync;
        TimeSpan timeout = options.ToolTimeout ?? DefaultToolTimeout;
        ExternalToolResult toolResult;
        try
        {
            using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutSource.CancelAfter(timeout);
            toolResult = await runner(startInfo, timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return new(ConversionOutcome.Failed, Path.GetFileName(toolPath),
                $"Dönüştürücü zaman aşımı ({timeout.TotalSeconds:F0}s): '{toolPath}'.");
        }
        catch (OperationCanceledException)
        {
            return new(ConversionOutcome.Failed, Path.GetFileName(toolPath), "Dönüştürme iptal edildi.");
        }
        catch (Exception error)
        {
            return new(ConversionOutcome.Failed, Path.GetFileName(toolPath),
                $"Dönüştürücü başlatılamadı ('{toolPath}'): {error.GetType().Name}: {error.Message}. " +
                $"ROWL_OGGENC_PATH / ROWL_WEBP2PNG_PATH / FFMPEG_PATH ile araç yolunu verin.");
        }
        if (toolResult.ExitCode != 0)
            return new(ConversionOutcome.Failed, Path.GetFileName(toolPath),
                $"Dönüştürücü çıkış kodu {toolResult.ExitCode}: '{toolPath}'. stderr: {FirstLine(toolResult.StandardError)}");
        return new(ConversionOutcome.Converted, Path.GetFileName(toolPath), "Araç adımı tamam.");
    }

    private static string? VerifyFreshOutput(
        string hashSourceFullPath, string hashSourceHash, string outputFullPath, string toolName)
    {
        if (!File.Exists(outputFullPath))
            return $"Dönüştürücü çıktı üretmedi: '{outputFullPath}'.";
        string sidecarPath = SidecarPathForOutput(outputFullPath);
        MediaConversionProvenance? provenance = MediaConversionProvenance.TryReadFile(sidecarPath);
        if (provenance is null)
            return $"Dönüştürücü sidecar yazmadı: '{sidecarPath}'.";
        string outputHash;
        try
        {
            outputHash = ComputeFileSha256(outputFullPath);
        }
        catch (Exception error)
        {
            return $"Çıktı hash'i okunamadı ({error.GetType().Name}); çıktı güvensiz sayıldı.";
        }
        if (!string.Equals(provenance.SourceSha256, hashSourceHash, StringComparison.OrdinalIgnoreCase))
            return $"Sidecar kaynak hash'i uyuşmuyor: '{sidecarPath}'.";
        if (!string.Equals(provenance.OutputSha256, outputHash, StringComparison.OrdinalIgnoreCase))
            return $"Sidecar çıktı hash'i uyuşmuyor: '{sidecarPath}'.";
        if (!string.Equals(provenance.ConverterName, toolName, StringComparison.Ordinal))
            return $"Sidecar araç adı uyuşmuyor ('{provenance.ConverterName}' != '{toolName}').";
        return null;
    }

    private static async Task<ExternalToolResult> DefaultRunnerAsync(
        ProcessStartInfo startInfo, CancellationToken cancellationToken)
        => await ExternalToolRunner.RunAsync(startInfo, null, cancellationToken).ConfigureAwait(false);

    private static string FirstLine(string text)
    {
        if (string.IsNullOrEmpty(text))
            return "(boş)";
        int newline = text.IndexOf('\n');
        string first = (newline < 0 ? text : text[..newline]).Trim();
        return first.Length > 200 ? first[..200] + "…" : first;
    }

    /// <summary>
    /// <c>SourceAssets/</c> ağacındaki dönüştürülebilir kaynakları tarar,
    /// göreli alt yolu koruyarak <c>Assets/{audio,images}/…</c> altına yazar
    /// (örn. <c>SourceAssets/sfx/theme.mp3</c> → <c>Assets/audio/sfx/theme.ogg</c>).
    /// Güncel çıktılar atlanır; kaynak asla yazılmaz.
    /// </summary>
    public static async Task<IReadOnlyList<ConversionResult>> ImportConvertedSourceAssetsAsync(
        string sourceAssetsRoot,
        string assetsRoot,
        ConverterOptions? options = null,
        Action<string>? log = null,
        CancellationToken cancellationToken = default)
    {
        var results = new List<ConversionResult>();
        if (string.IsNullOrWhiteSpace(sourceAssetsRoot) || !Directory.Exists(sourceAssetsRoot))
            return results;
        if (string.IsNullOrWhiteSpace(assetsRoot))
            return results;

        string[] sources;
        try
        {
            sources = Directory.GetFiles(sourceAssetsRoot, "*", SearchOption.AllDirectories);
        }
        catch (Exception error)
        {
            log?.Invoke($"⚠️ SourceAssets taranamadı ({error.GetType().Name}); dönüştürme atlandı.");
            return results;
        }
        Array.Sort(sources, StringComparer.Ordinal);

        foreach (string sourceFull in sources)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!TryGetConversionTarget(sourceFull, out string outExt, out string outDir, out string toolName))
                continue;
            string relative = Path.GetRelativePath(sourceAssetsRoot, sourceFull).Replace('\\', '/');
            string outputRelative = Path.ChangeExtension(relative, outExt).Replace('\\', '/');
            string outputFull = BuildConvertedOutputFullPath(assetsRoot, outDir, outputRelative);
            ConversionResult result = await ConvertFileAsync(
                sourceFull, outputFull, toolName, options, log, cancellationToken, relative).ConfigureAwait(false);
            string loggedRelative = (outDir + "/" + outputRelative).Replace('\\', '/');
            results.Add(result with { OutputRelativePath = loggedRelative });
        }
        return results;
    }
}

/// <summary>
/// Faz 5 Dilim 5 — <c>&lt;çıktı&gt;.rowlconv.json</c> sidecar kaydı.
/// Şema (native araçların yazdığı; kaynak:
/// <c>docs/MEDIA_CONVERTERS_CONTRACT.md</c>): <c>source_sha256,
/// converter_name, converter_version, settings, output_sha256,
/// created_by</c> (settings değerleri sayı/null da olabilir).
/// <c>settings</c> ZORUNLUDUR (nesne olmalı; araçlar her zaman yazar —
/// yoksa sidecar geçersiz sayılır, fail-closed). <c>source_path</c>
/// opsiyoneldir: tool'lar pratikte yazmaz, C# convert hattı
/// (<see cref="MediaConverterService.StampSourcePath"/>) damgalar; native
/// okuyucu bilinmeyen anahtarları yoksayar.
/// Bozuk/eksik dosya → null (fail-closed, throw yok).
/// </summary>
public sealed record MediaConversionProvenance(
    string SourceSha256,
    string ConverterName,
    string ConverterVersion,
    Dictionary<string, string>? Settings,
    string OutputSha256,
    string CreatedBy,
    string? SourcePath = null)
{
    public static MediaConversionProvenance? TryReadFile(string sidecarFullPath)
    {
        string json;
        try
        {
            if (!File.Exists(sidecarFullPath))
                return null;
            json = File.ReadAllText(sidecarFullPath);
        }
        catch (Exception)
        {
            return null;
        }
        return TryParse(json);
    }

    public static MediaConversionProvenance? TryParse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
            return null;
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return null;
            string? sourceSha = GetString(root, "source_sha256");
            string? converterName = GetString(root, "converter_name");
            string? converterVersion = GetString(root, "converter_version");
            string? outputSha = GetString(root, "output_sha256");
            string? createdBy = GetString(root, "created_by");
            if (string.IsNullOrWhiteSpace(sourceSha) || string.IsNullOrWhiteSpace(converterName)
                || string.IsNullOrWhiteSpace(converterVersion) || string.IsNullOrWhiteSpace(outputSha)
                || string.IsNullOrWhiteSpace(createdBy))
                return null;
            Dictionary<string, string> settings;
            if (!root.TryGetProperty("settings", out JsonElement settingsElement)
                || settingsElement.ValueKind != JsonValueKind.Object)
            {
                // Sıkılaştırma (fix): araçlar her zaman settings yazar;
                // settings'siz sidecar geçersiz sayılır (fail-closed).
                return null;
            }
            settings = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (JsonProperty property in settingsElement.EnumerateObject())
            {
                if (property.Value.ValueKind == JsonValueKind.String)
                    settings[property.Name] = property.Value.GetString() ?? string.Empty;
                else
                    settings[property.Name] = property.Value.ToString();
            }
            string? sourcePath = GetString(root, "source_path")
                ?? (settings is not null && settings.TryGetValue("source_path", out string? fromSettings) ? fromSettings : null)
                ?? (settings is not null && settings.TryGetValue("source", out string? legacy) ? legacy : null);
            return new MediaConversionProvenance(
                sourceSha, converterName, converterVersion, settings, outputSha, createdBy, sourcePath);
        }
        catch (Exception)
        {
            return null;
        }
    }

    private static string? GetString(JsonElement root, string property)
        => root.TryGetProperty(property, out JsonElement element)
            && element.ValueKind == JsonValueKind.String
            ? element.GetString() : null;

    public string ToJson()
    {
        var document = new Dictionary<string, object?>(StringComparer.Ordinal)
        {
            ["source_sha256"] = SourceSha256,
            ["converter_name"] = ConverterName,
            ["converter_version"] = ConverterVersion,
            ["settings"] = Settings ?? new Dictionary<string, string>(),
            ["output_sha256"] = OutputSha256,
            ["created_by"] = CreatedBy,
        };
        if (!string.IsNullOrEmpty(SourcePath))
            document["source_path"] = SourcePath;
        return JsonSerializer.Serialize(document, new JsonSerializerOptions { WriteIndented = false }) + "\n";
    }
}
