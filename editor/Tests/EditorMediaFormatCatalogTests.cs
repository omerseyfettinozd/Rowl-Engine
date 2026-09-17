using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorMediaFormatCatalogTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 33]: Faz 1 medya format sözleşmesi (katalog + import + validation)...");

        // Step 33.1: single capability table
        Console.WriteLine("    [Step 33.1]: MediaFormatCatalog accepted/rejected sets...");
        foreach (string accepted in new[] { "hero.png", "photo.JPG", "photo.jpeg", "old.bmp", "frame.tga", "tone.wav", "music.OGG", "body.ttf", "head.otf" })
        {
            if (!MediaFormatCatalog.IsAcceptedMediaExtension(accepted) || MediaFormatCatalog.RequiresExplicitRejection(accepted))
                throw new Exception($"MediaFormatCatalog should accept '{accepted}'.");
        }
        // Faz 5 Dilim 5: dönüştürülebilir kaynaklar kabul-dönüştürülür —
        // reddedilmez, doğrudan kabul de edilmez.
        foreach (string convertible in new[] { "theme.mp3", "voice.flac", "hero.webp", "THEME.MP3", "PHOTO.WEBP" })
        {
            if (!MediaFormatCatalog.IsConverterPendingExtension(convertible)
                || MediaFormatCatalog.RequiresExplicitRejection(convertible)
                || MediaFormatCatalog.IsAcceptedMediaExtension(convertible))
                throw new Exception($"MediaFormatCatalog should convert-accept '{convertible}'.");
        }
        foreach (string rejected in new[] { "fun.gif", "clip.psd", "take.aiff", "song.m4a" })
        {
            if (!MediaFormatCatalog.RequiresExplicitRejection(rejected) || MediaFormatCatalog.IsAcceptedMediaExtension(rejected))
                throw new Exception($"MediaFormatCatalog should reject '{rejected}'.");
        }
        if (!MediaFormatCatalog.IsSupportedImageExtension(".PNG") || !MediaFormatCatalog.IsSupportedAudioExtension(".ogg") || !MediaFormatCatalog.IsSupportedFontExtension(".TTF"))
            throw new Exception("MediaFormatCatalog must accept bare extensions regardless of case.");
        if (MediaFormatCatalog.TryGetAssetSubdirectory("song.mp3", out _) || MediaFormatCatalog.TryGetAssetSubdirectory("pic.webp", out _))
            throw new Exception("Rejected formats must not resolve to an asset subdirectory.");
        if (!MediaFormatCatalog.TryGetAssetSubdirectory("hero.png", out var imgDir) || imgDir != "images"
            || !MediaFormatCatalog.TryGetAssetSubdirectory("tone.ogg", out var audioDir) || audioDir != "audio"
            || !MediaFormatCatalog.TryGetAssetSubdirectory("body.ttf", out var fontDir) || fontDir != "fonts")
            throw new Exception("MediaFormatCatalog subdirectory mapping is wrong.");
        if (!MediaFormatCatalog.ImagePickerPatterns.All(p => p != "*.webp" && p != "*.gif")
            || !MediaFormatCatalog.AudioPickerPatterns.All(p => p != "*.mp3" && p != "*.flac"))
            throw new Exception("Picker patterns must not offer converter-pending formats.");
        if (!MediaFormatCatalog.RejectionMessage("theme.mp3").Contains("converted")
            || MediaFormatCatalog.RejectionMessage("fun.gif").Contains("Faz 5"))
            throw new Exception("Only converter sources may promise conversion; true rejects must not.");

        // Step 33.2: import converts converter sources (fake tools), rejects gif
        Console.WriteLine("    [Step 33.2]: ImportAssetFiles convert-accept...");
        string importSrc = Path.Combine(Path.GetTempPath(), $"RowlFormatImportSrc_{Guid.NewGuid():N}");
        string importAssets = Path.Combine(Path.GetTempPath(), $"RowlFormatImportAssets_{Guid.NewGuid():N}");
        string rejectionLog = string.Empty;
        string? savedOggPath = Environment.GetEnvironmentVariable("ROWL_OGGENC_PATH");
        string? savedWebpPath = Environment.GetEnvironmentVariable("ROWL_WEBP2PNG_PATH");
        string? savedFfmpegPath = Environment.GetEnvironmentVariable("FFMPEG_PATH");
        try
        {
            Directory.CreateDirectory(importSrc);
            File.WriteAllBytes(Path.Combine(importSrc, "ok.png"), new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            File.WriteAllBytes(Path.Combine(importSrc, "theme.mp3"), new byte[] { 0x49, 0x44, 0x33 });
            File.WriteAllBytes(Path.Combine(importSrc, "hero.webp"), new byte[] { 0x52, 0x49, 0x46, 0x46 });
            File.WriteAllBytes(Path.Combine(importSrc, "fun.gif"), new byte[] { 0x47, 0x49, 0x46 });
            string fakeOgg = WriteFakeConverterScript("rowl_oggenc");
            string fakeWebp = WriteFakeConverterScript("rowl_webp2png");
            string fakeFfmpeg = WriteFakeConverterScript("ffmpeg");
            Environment.SetEnvironmentVariable("ROWL_OGGENC_PATH", fakeOgg);
            Environment.SetEnvironmentVariable("ROWL_WEBP2PNG_PATH", fakeWebp);
            Environment.SetEnvironmentVariable("FFMPEG_PATH", fakeFfmpeg);
            var imported = EditorAssetImportService.ImportAssetFiles(
                Directory.GetFiles(importSrc), importAssets, msg => rejectionLog += msg + "\n");
            if (!imported.Any(p => p.EndsWith("ok.png"))
                || !imported.Any(p => p == "audio/theme.ogg")
                || !imported.Any(p => p == "images/hero.png"))
                throw new Exception($"Expected ok.png + converted outputs, got [{string.Join(",", imported)}].");
            if (!rejectionLog.Contains("fun.gif"))
                throw new Exception("Import rejection log must name the truly-unsupported file.");
            if (!File.Exists(Path.Combine(importAssets, "audio", "theme.ogg.rowlconv.json"))
                || !File.Exists(Path.Combine(importAssets, "images", "hero.png.rowlconv.json")))
                throw new Exception("Converted outputs must ship adjacent .rowlconv.json sidecars.");
            if (Directory.EnumerateFiles(importAssets, "*", SearchOption.AllDirectories).Any(f => f.EndsWith(".gif") || f.EndsWith(".mp3") || f.EndsWith(".webp")))
                throw new Exception("Raw sources and rejected formats must not be copied into the project.");
        }
        finally
        {
            Environment.SetEnvironmentVariable("ROWL_OGGENC_PATH", savedOggPath);
            Environment.SetEnvironmentVariable("ROWL_WEBP2PNG_PATH", savedWebpPath);
            Environment.SetEnvironmentVariable("FFMPEG_PATH", savedFfmpegPath);
            try { Directory.Delete(importSrc, true); } catch { }
            try { Directory.Delete(importAssets, true); } catch { }
        }

        // Step 33.3: build-blocking validation audits
        Console.WriteLine("    [Step 33.3]: Validation format/case/collision/outside audits...");
        string scenarioRoot = Path.Combine(Path.GetTempPath(), $"RowlFormatAudit_{Guid.NewGuid():N}");
        try
        {
            Directory.CreateDirectory(scenarioRoot);

            // 33.3a accepted references pass silently
            string okAssets = Path.Combine(scenarioRoot, "ok", "Assets");
            Directory.CreateDirectory(Path.Combine(okAssets, "images"));
            Directory.CreateDirectory(Path.Combine(okAssets, "audio"));
            File.WriteAllBytes(Path.Combine(okAssets, "images", "ok.png"), new byte[] { 1 });
            File.WriteAllBytes(Path.Combine(okAssets, "audio", "tone.ogg"), new byte[] { 2 });
            var okNode = new NodeViewModel(9601, "Ok", 0, 0, bare: true);
            okNode.AddComponent<BackgroundComponentViewModel>().Texture = "ok.png";
            okNode.AddComponent<AudioComponentViewModel>().BgmTrack = "tone.ogg";
            var okIssues = ProjectValidationService.Validate(new[] { okNode }, Array.Empty<ConnectionViewModel>(), okAssets, okNode.Id);
            if (okIssues.Any(issue => issue.IsError))
                throw new Exception("Accepted MVP references must not produce errors: " + string.Join("; ", okIssues.Select(i => i.Message)));

            // 33.3b converter sources warn (never block), true rejects error
            AssertSingleWarning(scenarioRoot, "theme.mp3", "converted");
            AssertSingleWarning(scenarioRoot, "sprite.webp", "converted");
            AssertSingleError(scenarioRoot, "fun.gif", "unsupported format");

            // 33.3c outside-project paths are build-blocking errors
            AssertSingleError(scenarioRoot, "/etc/passwd", "outside the project");
            AssertSingleError(scenarioRoot, "../evil.png", "outside the project");
            AssertSingleError(scenarioRoot, "C:\\temp\\stolen.png", "outside the project");

            // 33.3d case-only mismatch is a case error, not a missing error
            string caseAssets = Path.Combine(scenarioRoot, "case", "Assets");
            Directory.CreateDirectory(Path.Combine(caseAssets, "images"));
            File.WriteAllBytes(Path.Combine(caseAssets, "images", "Hero.png"), new byte[] { 3 });
            var caseNode = new NodeViewModel(9602, "Case", 0, 0, bare: true);
            caseNode.AddComponent<BackgroundComponentViewModel>().Texture = "hero.png";
            var caseIssues = ProjectValidationService.Validate(new[] { caseNode }, Array.Empty<ConnectionViewModel>(), caseAssets, caseNode.Id);
            if (!caseIssues.Any(issue => issue.IsError && issue.Message.Contains("different letter case") && issue.Message.Contains("Hero.png")))
                throw new Exception("Case-only mismatch must report the on-disk spelling, not a plain missing asset.");

            // 33.3e case collisions on disk are build-blocking errors.
            // Tur-10: on a case-insensitive filesystem (Windows/NTFS) the
            // second WriteAllBytes just overwrites the first, so two
            // case-only-distinct files are unstageable and the validator
            // cannot see a collision. Probe the FS and skip staging there;
            // the validator logic is unchanged and stays fully covered on
            // case-sensitive filesystems.
            string collisionAssets = Path.Combine(scenarioRoot, "collision", "Assets");
            string collisionImages = Path.Combine(collisionAssets, "images");
            Directory.CreateDirectory(collisionImages);
            if (IsFileSystemCaseSensitive(collisionImages))
            {
                File.WriteAllBytes(Path.Combine(collisionImages, "Hero.png"), new byte[] { 4 });
                File.WriteAllBytes(Path.Combine(collisionImages, "hero.png"), new byte[] { 5 });
                var collisionNode = new NodeViewModel(9603, "Collision", 0, 0, bare: true);
                var collisionIssues = ProjectValidationService.Validate(new[] { collisionNode }, Array.Empty<ConnectionViewModel>(), collisionAssets, collisionNode.Id);
                if (!collisionIssues.Any(issue => issue.IsError && issue.Message.Contains("collision")))
                    throw new Exception("Case-only duplicate files must be reported as a name collision.");
            }
            else
            {
                Console.WriteLine("    [Step 33.3e]: skipped (filesystem is case-insensitive; case-only duplicates unstageable).");
            }
        }
        finally
        {
            try { Directory.Delete(scenarioRoot, true); } catch { }
        }

        Console.WriteLine("  ✅ [PASS] Media format contract, import rejection and validation audits verified");
    }

    /// <summary>
    /// Tur-10: true when <paramref name="directory"/> lives on a
    /// case-sensitive filesystem (two names differing only by case can
    /// coexist). Probes with a temp file; fails open (returns true) so a
    /// probe failure can never silently skip the collision audit.
    /// </summary>
    private static bool IsFileSystemCaseSensitive(string directory)
    {
        string probe = Path.Combine(directory, "RowlCaseProbe.tmp");
        string upper = Path.Combine(directory, "ROWLCASEPROBE.tmp");
        try
        {
            File.WriteAllBytes(probe, new byte[] { 0 });
            try
            {
                return !File.Exists(upper);
            }
            finally
            {
                try { File.Delete(probe); } catch { }
            }
        }
        catch
        {
            return true;
        }
    }

    private static void AssertSingleWarning(string scenarioRoot, string assetRef, string expectedFragment)
    {
        string assets = Path.Combine(scenarioRoot, $"ref_{Guid.NewGuid():N}", "Assets");
        Directory.CreateDirectory(Path.Combine(assets, "images"));
        var node = new NodeViewModel(9611, "Ref", 0, 0, bare: true);
        node.AddComponent<BackgroundComponentViewModel>().Texture = assetRef;
        var issues = ProjectValidationService.Validate(new[] { node }, Array.Empty<ConnectionViewModel>(), assets, node.Id);
        if (issues.Any(issue => issue.IsError && issue.Message.Contains(assetRef)))
            throw new Exception($"Reference '{assetRef}' must never be a build-blocking error.");
        if (!issues.Any(issue => !issue.IsError && issue.Message.Contains(assetRef) && issue.Message.Contains(expectedFragment)))
            throw new Exception($"Reference '{assetRef}' must be an advisory warning containing '{expectedFragment}'.");
    }

    /// <summary>
    /// Headless-suite fake araçları (sözleşme CLI sırasını doğrular):
    /// <c>ffmpeg</c> sahtesi decode eder (<c>-hide_banner -loglevel error -i
    /// IN -ar 44100 -ac 2 -sample_fmt s16 -f s16le OUT.pcm</c> →
    /// <c>b'PCM:' + girdi</c>); <c>rowl_oggenc</c> sahtesi PCM'i okuyup OGG +
    /// sidecar yazar (<c>--rate 44100 --channels 2 -o OUT --sidecar FILE
    /// IN.pcm</c>, source_sha256 = PCM hash'i); <c>rowl_webp2png</c> sahtesi
    /// <c>-o OUT --sidecar FILE IN.webp</c> yazar (source_sha256 = dosya
    /// hash'i). Çalıştırılabilir sarmalayıcı yolu döner (Unix sh + exec biti,
    /// Windows cmd).
    /// </summary>
    private static string WriteFakeConverterScript(string toolName)
    {
        string kind = toolName.Contains("ffmpeg", StringComparison.OrdinalIgnoreCase) ? "ffmpeg"
            : toolName.Contains("webp", StringComparison.OrdinalIgnoreCase) ? "webp" : "ogg";
        string script = Path.Combine(Path.GetTempPath(), $"RowlFakeConv_{Guid.NewGuid():N}.py");
        File.WriteAllText(script,
            "import hashlib, json, sys\n" +
            "kind = '" + kind + "'\n" +
            "tool = '" + toolName + "'\n" +
            "a = sys.argv[1:]\n" +
            "if kind == 'ffmpeg':\n" +
            "    assert a[0] == '-hide_banner' and a[1] == '-loglevel' and a[2] == 'error' and a[3] == '-i', a\n" +
            "    assert a[5] == '-ar' and a[7] == '-ac' and a[9] == '-sample_fmt' and a[10] == 's16', a\n" +
            "    assert a[11] == '-f' and a[12] == 's16le', a\n" +
            "    data = open(a[4], 'rb').read()\n" +
            "    open(a[13], 'wb').write(b'PCM:' + data)\n" +
            "else:\n" +
            "    o = a.index('-o'); s = a.index('--sidecar')\n" +
            "    assert a[o + 2] == '--sidecar' if kind == 'webp' else (a[o - 4] == '--rate' and s == o + 2), a\n" +
            "    out, sidecar, src = a[o + 1], a[s + 1], a[-1]\n" +
            "    assert sidecar.endswith('.rowlconv.json'), a\n" +
            "    data = open(src, 'rb').read()\n" +
            "    blob = b'FAKE:' + data\n" +
            "    open(out, 'wb').write(blob)\n" +
            "    json.dump({" +
            "'source_sha256': hashlib.sha256(data).hexdigest(), " +
            "'converter_name': tool, " +
            "'converter_version': '0.0-headless', " +
            "'settings': {}, " +
            "'output_sha256': hashlib.sha256(blob).hexdigest(), " +
            "'created_by': tool + ' --sidecar'}, " +
            "open(sidecar, 'w'))\n");
        if (OperatingSystem.IsWindows())
        {
            string wrapper = Path.ChangeExtension(script, ".cmd");
            File.WriteAllText(wrapper, "@echo off\r\npython \"%~dp0" + Path.GetFileName(script) + "\" %*\r\n");
            return wrapper;
        }
        string shWrapper = script + ".sh";
        File.WriteAllText(shWrapper,
            "#!/bin/sh\nexec python3 \"" + script + "\" \"$@\"\n");
        try
        {
            File.SetUnixFileMode(shWrapper,
                UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute
                | UnixFileMode.GroupRead | UnixFileMode.GroupExecute
                | UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
        }
        catch (Exception)
        {
            // Exec biti verilemezse çalıştırma hatası testte fail olarak görülür.
        }
        return shWrapper;
    }

    private static void AssertSingleError(string scenarioRoot, string assetRef, string expectedFragment)
    {
        string assets = Path.Combine(scenarioRoot, $"ref_{Guid.NewGuid():N}", "Assets");
        Directory.CreateDirectory(Path.Combine(assets, "images"));
        var node = new NodeViewModel(9610, "Ref", 0, 0, bare: true);
        node.AddComponent<BackgroundComponentViewModel>().Texture = assetRef;
        var issues = ProjectValidationService.Validate(new[] { node }, Array.Empty<ConnectionViewModel>(), assets, node.Id);
        if (!issues.Any(issue => issue.IsError && issue.Message.Contains(assetRef) && issue.Message.Contains(expectedFragment)))
            throw new Exception($"Reference '{assetRef}' must be a build-blocking error containing '{expectedFragment}'.");
    }
}
