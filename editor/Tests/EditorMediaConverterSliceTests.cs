using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 5 — C# import/converter hattı: SourceAssets→Assets dönüşümü
/// (çıktı adı + sidecar), hash-eşleşmede skip, kaynak değişince yeniden
/// dönüşüm; linter tazelik 3 kolu; kabul-dönüştür kapısı; provenance
/// P/Invoke + sidecar rozeti. Headless, native çağrı YOKTUR (sahte araç
/// koşucusu + sahte native delege).
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorMediaConverterSliceTests
{
    private sealed class TempProject : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), "RowlConv_" + Guid.NewGuid().ToString("N"));
        public string Assets => Path.Combine(Root, "Assets");
        public string SourceAssets => Path.Combine(Root, "SourceAssets");

        public TempProject()
        {
            Directory.CreateDirectory(Assets);
            Directory.CreateDirectory(SourceAssets);
        }

        public void Dispose()
        {
            try { Directory.Delete(Root, recursive: true); } catch (Exception) { }
        }
    }

    /// <summary>
    /// Sahte araç zinciri (sözleşme CLI sırasını doğrular):
    /// fake-ffmpeg girdiyi deterministik PCM'e çözer (son argüman = .pcm
    /// çıktısı), fake-oggenc PCM'i okuyup OGG + sidecar yazar
    /// (source_sha256 = PCM hash'i), fake-webp2png .webp'i okuyup PNG +
    /// sidecar yazar (source_sha256 = dosya hash'i). Yanlış argüman sırası
    /// exit-code 1 ile fail-closed döner.
    /// </summary>
    private static Task<ExternalToolResult> FakeRunner(ProcessStartInfo startInfo, CancellationToken _)
    {
        string fileName = Path.GetFileName(startInfo.FileName);
        var argv = startInfo.ArgumentList.ToArray();
        if (fileName.Contains("ffmpeg", StringComparison.OrdinalIgnoreCase))
        {
            // Sözleşme decode: -hide_banner -loglevel error -i IN -ar 44100
            // -ac 2 -sample_fmt s16 -f s16le OUT.pcm
            if (argv.Length != 14 || argv[0] != "-hide_banner" || argv[3] != "-i"
                || argv[11] != "-f" || argv[12] != "s16le"
                || argv[4].EndsWith(".pcm", StringComparison.OrdinalIgnoreCase))
                return Task.FromResult(new ExternalToolResult(1, string.Empty, "bad-ffmpeg-args"));
            byte[] pcm = System.Text.Encoding.UTF8.GetBytes("PCM:")
                .Concat(File.ReadAllBytes(argv[4])).ToArray();
            File.WriteAllBytes(argv[^1], pcm);
            return Task.FromResult(new ExternalToolResult(0, "ok", string.Empty));
        }
        string tool = fileName.Contains("webp", StringComparison.OrdinalIgnoreCase)
            ? MediaConverterService.WebpToolName
            : MediaConverterService.OggToolName;
        int oIndex = Array.IndexOf(argv, "-o");
        int sidecarIndex = Array.IndexOf(argv, "--sidecar");
        if (oIndex < 0 || sidecarIndex != oIndex + 2 || argv.Length < oIndex + 4)
            return Task.FromResult(new ExternalToolResult(1, string.Empty, "bad-args"));
        string output = argv[oIndex + 1];
        string sidecarPath = argv[sidecarIndex + 1];
        string source = argv[^1];
        if (!sidecarPath.EndsWith(MediaConverterService.SidecarSuffix, StringComparison.Ordinal))
            return Task.FromResult(new ExternalToolResult(1, string.Empty, "bad-sidecar"));
        byte[] input = File.ReadAllBytes(source);
        byte[] payload = System.Text.Encoding.UTF8.GetBytes("CONVERTED[" + tool + "]:")
            .Concat(input).ToArray();
        Directory.CreateDirectory(Path.GetDirectoryName(output)!);
        File.WriteAllBytes(output, payload);
        string sourceHash = MediaConverterService.ComputeFileSha256(source);
        string outputHash = MediaConverterService.ComputeFileSha256(output);
        // Gerçek araçlar source_path YAZMAZ (sözleşme); C# convert hattı
        // sonradan damgalar (StampSourcePath). Sahte araç da yazmaz.
        var sidecar = new Dictionary<string, object?>
        {
            ["source_sha256"] = sourceHash,
            ["converter_name"] = tool,
            ["converter_version"] = "9.9-fake",
            ["settings"] = new Dictionary<string, string>(),
            ["output_sha256"] = outputHash,
            ["created_by"] = tool + " --sidecar",
        };
        File.WriteAllText(sidecarPath, JsonSerializer.Serialize(sidecar) + "\n");
        return Task.FromResult(new ExternalToolResult(0, "ok", string.Empty));
    }

    private static MediaConverterService.ConverterOptions FakeOptions(string toolName) =>
        new(toolName == MediaConverterService.OggToolName ? "fake-oggenc" : "fake-webp2png",
            toolName == MediaConverterService.OggToolName ? null : "fake-webp2png",
            "fake-ffmpeg", null, FakeRunner);

    // ── Import hattı ──────────────────────────────────────────────

    [Fact]
    public async Task Convert_Mp3_ProducesOggPlusSidecar_SourceUntouched()
    {
        using var project = new TempProject();
        string source = Path.Combine(project.SourceAssets, "theme.mp3");
        byte[] sourceBytes = new byte[] { 0x49, 0x44, 0x33, 1, 2, 3 };
        File.WriteAllBytes(source, sourceBytes);
        string output = Path.Combine(project.Assets, "audio", "theme.ogg");

        var result = await MediaConverterService.ConvertFileAsync(
            source, output, MediaConverterService.OggToolName, FakeOptions(MediaConverterService.OggToolName));

        Assert.Equal(MediaConverterService.ConversionOutcome.Converted, result.Outcome);
        Assert.True(File.Exists(output));
        Assert.True(File.Exists(output + MediaConverterService.SidecarSuffix));
        Assert.Equal(sourceBytes, File.ReadAllBytes(source));
        var provenance = MediaConversionProvenance.TryReadFile(output + MediaConverterService.SidecarSuffix);
        Assert.NotNull(provenance);
        Assert.Equal(MediaConverterService.OggToolName, provenance.ConverterName);
        Assert.Equal(MediaConverterService.ComputeFileSha256(output), provenance.OutputSha256);
    }

    [Fact]
    public async Task Convert_SkipsWhenHashesMatch_ReconvertsWhenSourceChanges()
    {
        using var project = new TempProject();
        string source = Path.Combine(project.SourceAssets, "voice.flac");
        File.WriteAllBytes(source, new byte[] { 1, 2, 3 });
        string output = Path.Combine(project.Assets, "audio", "voice.ogg");
        var options = FakeOptions(MediaConverterService.OggToolName);

        var first = await MediaConverterService.ConvertFileAsync(source, output, MediaConverterService.OggToolName, options);
        Assert.Equal(MediaConverterService.ConversionOutcome.Converted, first.Outcome);

        var second = await MediaConverterService.ConvertFileAsync(source, output, MediaConverterService.OggToolName, options);
        Assert.Equal(MediaConverterService.ConversionOutcome.SkippedUpToDate, second.Outcome);

        File.AppendAllBytes(source, new byte[] { 4 });
        var third = await MediaConverterService.ConvertFileAsync(source, output, MediaConverterService.OggToolName, options);
        Assert.Equal(MediaConverterService.ConversionOutcome.Converted, third.Outcome);
        var provenance = MediaConversionProvenance.TryReadFile(output + MediaConverterService.SidecarSuffix);
        Assert.NotNull(provenance);
        // Sözleşme: OGG sidecar source_sha256 = decode PCM baytları
        // (sahte decode: "PCM:" + ham baytlar), ham kaynak hash'i DEĞİL.
        Assert.Equal(FakePcmHash(new byte[] { 1, 2, 3, 4 }), provenance.SourceSha256);
        Assert.NotEqual(MediaConverterService.ComputeFileSha256(source), provenance.SourceSha256);
    }

    private static string FakePcmHash(byte[] sourceBytes)
    {
        byte[] prefix = System.Text.Encoding.UTF8.GetBytes("PCM:");
        byte[] pcm = new byte[prefix.Length + sourceBytes.Length];
        Buffer.BlockCopy(prefix, 0, pcm, 0, prefix.Length);
        Buffer.BlockCopy(sourceBytes, 0, pcm, prefix.Length, sourceBytes.Length);
        using var sha = System.Security.Cryptography.SHA256.Create();
        return Convert.ToHexString(sha.ComputeHash(pcm)).ToLowerInvariant();
    }

    [Fact]
    public async Task Convert_WebP_ProducesPng()
    {
        using var project = new TempProject();
        string source = Path.Combine(project.SourceAssets, "hero.webp");
        File.WriteAllBytes(source, new byte[] { 0x52, 0x49, 0x46, 0x46 });
        string output = Path.Combine(project.Assets, "images", "hero.png");

        var result = await MediaConverterService.ConvertFileAsync(
            source, output, MediaConverterService.WebpToolName, FakeOptions(MediaConverterService.WebpToolName));

        Assert.Equal(MediaConverterService.ConversionOutcome.Converted, result.Outcome);
        Assert.True(File.Exists(output + MediaConverterService.SidecarSuffix));
    }

    [Fact]
    public async Task Import_RoutesConverterSources_AndStillRejectsGif()
    {
        using var project = new TempProject();
        string drop = Path.Combine(project.Root, "drop");
        Directory.CreateDirectory(drop);
        File.WriteAllBytes(Path.Combine(drop, "theme.mp3"), new byte[] { 1 });
        File.WriteAllBytes(Path.Combine(drop, "hero.webp"), new byte[] { 2 });
        File.WriteAllBytes(Path.Combine(drop, "fun.gif"), new byte[] { 3 });
        File.WriteAllBytes(Path.Combine(drop, "ok.png"), new byte[] { 4 });
        var options = new MediaConverterService.ConverterOptions("fake-oggenc", "fake-webp2png", "fake-ffmpeg", null, FakeRunner);
        var logLines = new List<string>();

        var imported = await EditorAssetImportService.ImportAssetFilesAsync(
            Directory.GetFiles(drop), project.Assets, options, logLines.Add);

        Assert.Contains("audio/theme.ogg", imported);
        Assert.Contains("images/hero.png", imported);
        Assert.Contains(imported, p => p.EndsWith("ok.png"));
        Assert.DoesNotContain(imported, p => p.EndsWith(".gif"));
        Assert.DoesNotContain(
            Directory.EnumerateFiles(project.Assets, "*", SearchOption.AllDirectories),
            f => f.EndsWith(".gif") || f.EndsWith(".mp3") || f.EndsWith(".webp"));
        Assert.Contains(logLines, l => l.Contains("fun.gif") && l.Contains("Import rejected"));
    }

    [Fact]
    public async Task Import_SourceAssetsTree_PreservesRelativePath()
    {
        using var project = new TempProject();
        string nested = Path.Combine(project.SourceAssets, "sfx");
        Directory.CreateDirectory(nested);
        File.WriteAllBytes(Path.Combine(nested, "theme.mp3"), new byte[] { 7 });
        var options = new MediaConverterService.ConverterOptions("fake-oggenc", "fake-webp2png", "fake-ffmpeg", null, FakeRunner);

        var results = await MediaConverterService.ImportConvertedSourceAssetsAsync(
            project.SourceAssets, project.Assets, options);

        Assert.Single(results);
        Assert.Equal("audio/sfx/theme.ogg", results[0].OutputRelativePath);
        Assert.True(File.Exists(Path.Combine(project.Assets, "audio", "sfx", "theme.ogg")));
    }

    // ── Linter tazelik 3 kolu ─────────────────────────────────────

    private static ProjectLintOptions FreshnessOnly() => new(
        CheckLuaScripts: false, CheckTranslations: false, CheckGlyphCoverage: false,
        CheckUnusedAssets: false, CheckLongAudio: false, CheckCharacterLayers: false,
        CheckPrefetch: false, CheckConvertedFreshness: true,
        ConverterOptions: new MediaConverterService.ConverterOptions(
            "fake-oggenc", "fake-webp2png", "fake-ffmpeg", null, FakeRunner));

    private static async Task SeedConvertedAsync(TempProject project)
    {
        string nested = Path.Combine(project.SourceAssets, "sfx");
        Directory.CreateDirectory(nested);
        File.WriteAllBytes(Path.Combine(nested, "theme.mp3"), new byte[] { 1, 2, 3 });
        var options = new MediaConverterService.ConverterOptions("fake-oggenc", "fake-webp2png", "fake-ffmpeg", null, FakeRunner);
        await MediaConverterService.ImportConvertedSourceAssetsAsync(
            project.SourceAssets, project.Assets, options);
    }

    private static List<ProjectValidationIssue> FreshnessIssues(TempProject project)
    {
        var nodes = new List<NodeViewModel> { new(1, "N", 0, 0, bare: true) };
        return ProjectLintService.Lint(nodes, Array.Empty<ConnectionViewModel>(),
            project.Assets, 1, null, FreshnessOnly())
            .Where(i => !i.IsError && i.AssetPath == "audio/sfx/theme.ogg").ToList();
    }

    [Fact]
    public async Task Lint_FreshConvertedAsset_IsSilent()
    {
        using var project = new TempProject();
        await SeedConvertedAsync(project);
        Assert.Empty(FreshnessIssues(project));
    }

    [Fact]
    public async Task Lint_OutputChanged_Warns()
    {
        using var project = new TempProject();
        await SeedConvertedAsync(project);
        File.AppendAllBytes(Path.Combine(project.Assets, "audio", "sfx", "theme.ogg"), new byte[] { 9 });
        var issues = FreshnessIssues(project);
        Assert.Single(issues);
        Assert.Contains("changed on disk", issues[0].Message);
    }

    [Fact]
    public async Task Lint_SourceChanged_Warns()
    {
        using var project = new TempProject();
        await SeedConvertedAsync(project);
        File.AppendAllBytes(Path.Combine(project.SourceAssets, "sfx", "theme.mp3"), new byte[] { 9 });
        var issues = FreshnessIssues(project);
        Assert.Single(issues);
        Assert.Contains("source changed", issues[0].Message);
    }

    // ── Kapı: dönüştürülen kabul, gerçek-desteklenmeyen red ────────

    [Fact]
    public void Gate_ConvertedSources_AcceptViaConversion_TrueUnsupported_Rejected()
    {
        foreach (string source in new[] { "theme.mp3", "voice.flac", "hero.webp", "THEME.MP3", "PHOTO.WEBP" })
        {
            Assert.True(MediaFormatCatalog.IsConverterPendingExtension(source));
            Assert.False(MediaFormatCatalog.RequiresExplicitRejection(source));
            Assert.False(MediaFormatCatalog.IsAcceptedMediaExtension(source));
        }
        foreach (string bad in new[] { "fun.gif", "clip.psd", "take.aiff", "song.m4a", "font.woff2" })
        {
            Assert.True(MediaFormatCatalog.RequiresExplicitRejection(bad));
            Assert.Contains("unsupported format", MediaFormatCatalog.RejectionMessage(bad));
        }
        Assert.DoesNotContain("Faz 5", MediaFormatCatalog.RejectionMessage("fun.gif"));
        Assert.Contains("converted", MediaFormatCatalog.RejectionMessage("theme.mp3"));
        Assert.Contains(MediaConverterService.ConvertedOutputCandidates("theme.mp3"), c => c == "audio/theme.ogg");
        Assert.Contains(MediaConverterService.ConvertedOutputCandidates("hero.webp"), c => c == "images/hero.png");
    }

    [Fact]
    public void Validate_ConverterPendingRef_WarnsNeverErrors()
    {
        using var project = new TempProject();
        var node = new NodeViewModel(2, "N", 0, 0, bare: true);
        node.AddComponent<BackgroundComponentViewModel>().Texture = "theme.mp3";

        var issues = ProjectValidationService.Validate(
            new[] { node }, Array.Empty<ConnectionViewModel>(), project.Assets, node.Id);

        Assert.DoesNotContain(issues, i => i.IsError && i.Message.Contains("theme.mp3"));
        Assert.Contains(issues, i => !i.IsError && i.Message.Contains("theme.mp3"));
    }

    [Fact]
    public void Validate_ConvertedOutputPresent_IsSilent()
    {
        using var project = new TempProject();
        Directory.CreateDirectory(Path.Combine(project.Assets, "audio"));
        File.WriteAllBytes(Path.Combine(project.Assets, "audio", "theme.ogg"), new byte[] { 1 });
        var node = new NodeViewModel(3, "N", 0, 0, bare: true);
        node.AddComponent<BackgroundComponentViewModel>().Texture = "theme.mp3";

        var issues = ProjectValidationService.Validate(
            new[] { node }, Array.Empty<ConnectionViewModel>(), project.Assets, node.Id);

        Assert.DoesNotContain(issues, i => i.Message.Contains("theme.mp3"));
    }

    // ── Provenance P/Invoke + sidecar ─────────────────────────────

    [Fact]
    public void NativeBridge_DeclaresProvenanceEntryPoint()
    {
        var method = typeof(NativeBridge).GetMethod(
            "RowlEngine_GetAssetProvenanceJson",
            System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.NonPublic);
        Assert.NotNull(method);
    }

    [Fact]
    public void Provenance_SidecarPresent_FormatsBadge()
    {
        using var project = new TempProject();
        Directory.CreateDirectory(Path.Combine(project.Assets, "audio"));
        string output = Path.Combine(project.Assets, "audio", "music.ogg");
        File.WriteAllBytes(output, new byte[] { 5 });
        var provenance = new MediaConversionProvenance(
            new string('a', 64), "rowl_oggenc", "1.2-test",
            new Dictionary<string, string>(), new string('b', 64), "rowl_oggenc --sidecar", "sfx/theme.mp3");
        File.WriteAllText(output + MediaConverterService.SidecarSuffix, provenance.ToJson());

        var resolved = AssetProvenanceService.ResolveProvenance("music.ogg", project.Assets, null);

        Assert.NotNull(resolved);
        var badge = AssetProvenanceService.Describe(resolved);
        Assert.True(badge.Visible);
        Assert.Contains("rowl_oggenc", badge.Text);
        Assert.Contains(new string('a', 8), badge.Text);
        var details = AssetProvenanceService.FormatDetails(resolved);
        Assert.Equal(3, details.Count);
    }

    [Fact]
    public void Provenance_MissingOrCorruptSidecar_Hidden()
    {
        using var project = new TempProject();
        Directory.CreateDirectory(Path.Combine(project.Assets, "audio"));
        string output = Path.Combine(project.Assets, "audio", "plain.ogg");
        File.WriteAllBytes(output, new byte[] { 5 });

        Assert.Null(AssetProvenanceService.ResolveProvenance("plain.ogg", project.Assets, null));
        Assert.False(AssetProvenanceService.Describe(null).Visible);
        Assert.Empty(AssetProvenanceService.FormatDetails(null));

        File.WriteAllText(output + MediaConverterService.SidecarSuffix, "{bozuk");
        Assert.Null(AssetProvenanceService.ResolveProvenance("plain.ogg", project.Assets, null));
    }

    [Fact]
    public void Provenance_NativeUndersizedBuffer_ReportsRequiredSize()
    {
        AssetProvenanceService.ProvenanceNativeCall undersized =
            (IntPtr _, string _, IntPtr buffer, uint size, out uint required) =>
            {
                required = 128;
                return NativeBridge.ResultCode.BufferTooSmall;
            };

        bool ok = AssetProvenanceService.TryGetViaNative(IntPtr.Zero, "audio/music.ogg", undersized, out _, out string error);

        Assert.False(ok);
        Assert.Contains("128", error);
    }

    [Fact]
    public void Provenance_NativeOkRoundTrip_ParsesJson()
    {
        string payload = "{\"source_sha256\":\"" + new string('c', 64) + "\",\"converter_name\":\"rowl_webp2png\"," +
            "\"converter_version\":\"2.0\",\"settings\":{},\"output_sha256\":\"" + new string('d', 64) + "\"," +
            "\"created_by\":\"rowl_webp2png --sidecar\"}";
        AssetProvenanceService.ProvenanceNativeCall fake =
            (IntPtr _, string _, IntPtr buffer, uint size, out uint required) =>
            {
                byte[] bytes = System.Text.Encoding.UTF8.GetBytes(payload + "\0");
                required = (uint)bytes.Length;
                if (size == 0)
                    return NativeBridge.ResultCode.Ok;
                if (size < required)
                    return NativeBridge.ResultCode.BufferTooSmall;
                Marshal.Copy(bytes, 0, buffer, bytes.Length);
                return NativeBridge.ResultCode.Ok;
            };

        bool ok = AssetProvenanceService.TryGetViaNative(IntPtr.Zero, "images/hero.png", fake, out string json, out _);

        Assert.True(ok);
        Assert.NotNull(MediaConversionProvenance.TryParse(json));
    }

    [Fact]
    public void Inspector_SelectedNodeProvenance_ListsConvertedRefs()
    {
        using var project = new TempProject();
        Directory.CreateDirectory(Path.Combine(project.Assets, "audio"));
        string output = Path.Combine(project.Assets, "audio", "music.ogg");
        File.WriteAllBytes(output, new byte[] { 5 });
        var provenance = new MediaConversionProvenance(
            new string('e', 64), "rowl_oggenc", "3.0",
            new Dictionary<string, string>(), new string('f', 64), "rowl_oggenc --sidecar");
        File.WriteAllText(output + MediaConverterService.SidecarSuffix, provenance.ToJson());
        var node = new NodeViewModel(4, "N", 0, 0, bare: true);
        node.AddComponent<AudioComponentViewModel>().BgmTrack = "music.ogg";

        var lines = AssetProvenanceService.FormatSelectedNodeProvenance(node, project.Assets);

        Assert.Contains(lines, l => l.Contains("rowl_oggenc"));
        Assert.Contains(lines, l => l.Contains(new string('e', 8)));
    }

    [Fact]
    public void ToolArguments_FollowSidecarCliContract()
    {
        // Sözleşme sırası: rowl_webp2png -o OUT.png --sidecar FILE IN.webp
        var webp = MediaConverterService.BuildWebpStartInfo("webp", "in.webp", "out.png", "out.png.rowlconv.json");
        Assert.Equal(new[] { "-o", "out.png", "--sidecar", "out.png.rowlconv.json", "in.webp" },
            webp.ArgumentList.ToArray());
        // rowl_oggenc [--rate HZ] [--channels N] -o OUT.ogg --sidecar FILE IN.pcm
        var ogg = MediaConverterService.BuildOggStartInfo("ogg", "in.pcm", "out.ogg", "out.ogg.rowlconv.json");
        Assert.Equal(new[] { "--rate", "44100", "--channels", "2", "-o", "out.ogg", "--sidecar", "out.ogg.rowlconv.json", "in.pcm" },
            ogg.ArgumentList.ToArray());
        // ffmpeg -hide_banner -loglevel error -i IN -ar 44100 -ac 2 -sample_fmt s16 -f s16le OUT.pcm
        var decode = MediaConverterService.BuildFfmpegDecodeStartInfo("ffmpeg", "in.mp3", "in.pcm");
        Assert.Equal(new[] { "-hide_banner", "-loglevel", "error", "-i", "in.mp3", "-ar", "44100", "-ac", "2", "-sample_fmt", "s16", "-f", "s16le", "in.pcm" },
            decode.ArgumentList.ToArray());
    }

    // ── Fix turu 1: BLOCKER kapanış kanıtları ─────────────────────────

    private static string FindRepoRoot()
    {
        string? dir = AppDomain.CurrentDomain.BaseDirectory;
        for (int i = 0; i < 12 && dir is not null; i++)
        {
            if (File.Exists(Path.Combine(dir, "editor", "Views", "Panels", "NodeInspectorView.axaml")))
                return dir;
            dir = Path.GetDirectoryName(dir);
        }
        dir = Directory.GetCurrentDirectory();
        for (int i = 0; i < 12 && dir is not null; i++)
        {
            if (File.Exists(Path.Combine(dir, "editor", "Views", "Panels", "NodeInspectorView.axaml")))
                return dir;
            dir = Path.GetDirectoryName(dir);
        }
        throw new InvalidOperationException("Repo kökü bulunamadı (NodeInspectorView.axaml yok).");
    }

    [Fact]
    public void Inspector_Axaml_BindsProvenanceBadge()
    {
        // BLOCKER 1 prob hedefi: rozet binding'i kaldırılırsa bu test kızarır.
        string axaml = File.ReadAllText(Path.Combine(
            FindRepoRoot(), "editor", "Views", "Panels", "NodeInspectorView.axaml"));
        Assert.Contains("InspectorViewModel.SelectedAssetProvenance", axaml);
        Assert.Contains("InspectorViewModel.HasSelectedAssetProvenance", axaml);
    }

    private static string NativePayload(string converterName)
        => "{\"source_sha256\":\"" + new string('c', 64) + "\",\"converter_name\":\"" + converterName + "\"," +
            "\"converter_version\":\"2.0\",\"settings\":{},\"output_sha256\":\"" + new string('d', 64) + "\"," +
            "\"created_by\":\"" + converterName + " --sidecar\"}";

    private static AssetProvenanceService.ProvenanceNativeCall FakeNative(string converterName)
        => (IntPtr _, string _, IntPtr buffer, uint size, out uint required) =>
        {
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(NativePayload(converterName) + "\0");
            required = (uint)bytes.Length;
            if (size == 0)
                return NativeBridge.ResultCode.Ok;
            if (size < required)
                return NativeBridge.ResultCode.BufferTooSmall;
            Marshal.Copy(bytes, 0, buffer, bytes.Length);
            return NativeBridge.ResultCode.Ok;
        };

    private static void WriteDiskSidecar(string outputFullPath, string converterName)
    {
        var provenance = new MediaConversionProvenance(
            new string('e', 64), converterName, "1.0",
            new Dictionary<string, string>(), new string('f', 64), converterName + " --sidecar");
        File.WriteAllText(outputFullPath + MediaConverterService.SidecarSuffix, provenance.ToJson());
    }

    [Fact]
    public void Provenance_NativeResult_WinsOverDiskSidecar()
    {
        // BLOCKER 2 prob hedefi (servis kolu): delege null'a çekilirse
        // native-önce davranışı kaybolur, disk kazanır → test kızarır.
        using var project = new TempProject();
        Directory.CreateDirectory(Path.Combine(project.Assets, "audio"));
        string output = Path.Combine(project.Assets, "audio", "music.ogg");
        File.WriteAllBytes(output, new byte[] { 5 });
        WriteDiskSidecar(output, "disk-tool");

        var viaNative = AssetProvenanceService.ResolveProvenance(
            "music.ogg", project.Assets, FakeNative("native-tool"), (IntPtr)7);
        Assert.NotNull(viaNative);
        Assert.Equal("native-tool", viaNative.ConverterName);

        var viaDisk = AssetProvenanceService.ResolveProvenance("music.ogg", project.Assets, null);
        Assert.NotNull(viaDisk);
        Assert.Equal("disk-tool", viaDisk.ConverterName);
    }

    [Fact]
    public void Inspector_SelectedAssetProvenance_UsesNativeFirst_FallsBackToDisk()
    {
        // BLOCKER 1+2 VM kapanışı: engine yokken disk, delege verilince native.
        string previousRoot = MainWindowViewModel.ProjectRoot;
        string parent = Path.Combine(Path.GetTempPath(), "RowlProv_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(parent);
        try
        {
            var created = ProjectFactory.CreateNewProject("ProvTest", parent);
            Assert.True(created.Success && created.Info is not null);
            string root = created.Info.Path;
            var vm = new MainWindowViewModel(root, connectEngine: false);
            try
            {
                string assets = Path.Combine(root, "Assets");
                Directory.CreateDirectory(Path.Combine(assets, "audio"));
                string output = Path.Combine(assets, "audio", "music.ogg");
                File.WriteAllBytes(output, new byte[] { 5 });
                WriteDiskSidecar(output, "disk-tool");

                var node = new NodeViewModel(90, "N", 0, 0, bare: true);
                node.AddComponent<AudioComponentViewModel>().BgmTrack = "music.ogg";
                vm.Nodes.Add(node);
                vm.SelectedNode = node;

                // Prod ucu engine ölü → disk-only (null delege).
                Assert.Null(vm.InspectorViewModel.ActiveProvenanceEndpoint().Call);
                var diskLines = vm.InspectorViewModel.SelectedAssetProvenance;
                Assert.Contains(diskLines, l => l.Contains("disk-tool"));
                Assert.True(vm.InspectorViewModel.HasSelectedAssetProvenance);

                // Delege verilince native-önce (BLOCKER 2 prob hedefi: null'a
                // çekilirse native satırı kaybolur → test kızarır).
                vm.InspectorViewModel.TestProvenanceNativeCall = FakeNative("native-tool");
                vm.InspectorViewModel.TestProvenanceEngineHandle = (IntPtr)7;
                Assert.NotNull(vm.InspectorViewModel.ActiveProvenanceEndpoint().Call);
                var nativeLines = vm.InspectorViewModel.SelectedAssetProvenance;
                Assert.Contains(nativeLines, l => l.Contains("native-tool"));
                Assert.DoesNotContain(nativeLines, l => l.Contains("disk-tool"));

                // Fail-closed görünüm: sidecar'sız referansta rozet gizli.
                var bare = new NodeViewModel(91, "B", 0, 0, bare: true);
                bare.AddComponent<AudioComponentViewModel>().BgmTrack = "plain.wav";
                vm.Nodes.Add(bare);
                vm.SelectedNode = bare;
                vm.InspectorViewModel.TestProvenanceNativeCall = null;
                Assert.False(vm.InspectorViewModel.HasSelectedAssetProvenance);
                Assert.Empty(vm.InspectorViewModel.SelectedAssetProvenance);
            }
            finally
            {
                if (string.Equals(MainWindowViewModel.ProjectRoot, root, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = previousRoot;
            }
        }
        finally
        {
            try { Directory.Delete(parent, recursive: true); } catch (Exception) { }
            try
            {
                if (!string.Equals(MainWindowViewModel.ProjectRoot, previousRoot, StringComparison.Ordinal))
                    MainWindowViewModel.ProjectRoot = previousRoot;
            }
            catch (Exception) { }
        }
    }

    // ── Fix turu 1: SHOULD-FIX kapanış kanıtları ──────────────────────

    [Fact]
    public void Provenance_SettingsMissing_IsInvalid()
    {
        // Sıkılaştırma: settings'siz sidecar geçersiz (araçlar hep yazar).
        string withoutSettings = "{\"source_sha256\":\"" + new string('a', 64) + "\",\"converter_name\":\"rowl_oggenc\"," +
            "\"converter_version\":\"1.0\",\"output_sha256\":\"" + new string('b', 64) + "\"," +
            "\"created_by\":\"rowl_oggenc --sidecar\"}";
        Assert.Null(MediaConversionProvenance.TryParse(withoutSettings));
        string withSettings = "{\"source_sha256\":\"" + new string('a', 64) + "\",\"converter_name\":\"rowl_oggenc\"," +
            "\"converter_version\":\"1.0\",\"settings\":{},\"output_sha256\":\"" + new string('b', 64) + "\"," +
            "\"created_by\":\"rowl_oggenc --sidecar\"}";
        Assert.NotNull(MediaConversionProvenance.TryParse(withSettings));
    }

    [Fact]
    public async Task Convert_StampsSourcePath_LeavesOutputHashUntouched()
    {
        // Damga: output_sha256 yeniden hesaplanmaz, çıktı baytları aynı kalır.
        using var project = new TempProject();
        string source = Path.Combine(project.SourceAssets, "hero.webp");
        File.WriteAllBytes(source, new byte[] { 0x52, 0x49, 0x46, 0x46 });
        string output = Path.Combine(project.Assets, "images", "hero.png");

        var result = await MediaConverterService.ConvertFileAsync(
            source, output, MediaConverterService.WebpToolName, FakeOptions(MediaConverterService.WebpToolName));

        Assert.Equal(MediaConverterService.ConversionOutcome.Converted, result.Outcome);
        var provenance = MediaConversionProvenance.TryReadFile(output + MediaConverterService.SidecarSuffix);
        Assert.NotNull(provenance);
        Assert.Equal("hero.webp", provenance.SourcePath);
        Assert.Equal(MediaConverterService.ComputeFileSha256(output), provenance.OutputSha256);
    }

    [Fact]
    public async Task Bulk_StampsSourceAssetsRelativePath()
    {
        using var project = new TempProject();
        string nested = Path.Combine(project.SourceAssets, "sfx");
        Directory.CreateDirectory(nested);
        File.WriteAllBytes(Path.Combine(nested, "theme.mp3"), new byte[] { 7 });
        var options = new MediaConverterService.ConverterOptions("fake-oggenc", "fake-webp2png", "fake-ffmpeg", null, FakeRunner);

        var results = await MediaConverterService.ImportConvertedSourceAssetsAsync(
            project.SourceAssets, project.Assets, options);

        Assert.Single(results);
        string output = Path.Combine(project.Assets, "audio", "sfx", "theme.ogg");
        var provenance = MediaConversionProvenance.TryReadFile(output + MediaConverterService.SidecarSuffix);
        Assert.NotNull(provenance);
        Assert.Equal("sfx/theme.mp3", provenance.SourcePath);
    }

    [Fact]
    public void ConvertedOutputPaths_BulkAndSingleImport_ShareOneRule()
    {
        // Tutarlılık: bulk (ağaç) ve tek-dosya (flat) aynı formülü kullanır;
        // tek-dosya girdisi yalın ad olduğu için flat düşer.
        using var project = new TempProject();
        string bulkFull = MediaConverterService.BuildConvertedOutputFullPath(
            project.Assets, "audio", "sfx/theme.ogg");
        Assert.Equal(Path.Combine(project.Assets, "audio", "sfx", "theme.ogg"), bulkFull);
        string singleFull = MediaConverterService.BuildConvertedOutputFullPath(
            project.Assets, "audio", "theme.ogg");
        Assert.Equal(Path.Combine(project.Assets, "audio", "theme.ogg"), singleFull);
        // Aday kümesi iki yerleşimi de çözer (ağaç + flat).
        Assert.Contains(MediaConverterService.ConvertedOutputCandidates("sfx/theme.mp3"), c => c == "sfx/theme.ogg");
        Assert.Contains(MediaConverterService.ConvertedOutputCandidates("theme.mp3"), c => c == "audio/theme.ogg");
    }
}
