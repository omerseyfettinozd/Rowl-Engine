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
        foreach (string rejected in new[] { "theme.mp3", "voice.flac", "hero.webp", "fun.gif", "THEME.MP3", "PHOTO.WEBP" })
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
        if (!MediaFormatCatalog.RejectionMessage("theme.mp3").Contains("Faz 5")
            || MediaFormatCatalog.RejectionMessage("fun.gif").Contains("Faz 5"))
            throw new Exception("Only converter-pending formats may promise the Faz 5 converter.");

        // Step 33.2: import refuses rejected formats with an explicit log
        Console.WriteLine("    [Step 33.2]: ImportAssetFiles rejection...");
        string importSrc = Path.Combine(Path.GetTempPath(), $"RowlFormatImportSrc_{Guid.NewGuid():N}");
        string importAssets = Path.Combine(Path.GetTempPath(), $"RowlFormatImportAssets_{Guid.NewGuid():N}");
        string rejectionLog = string.Empty;
        try
        {
            Directory.CreateDirectory(importSrc);
            File.WriteAllBytes(Path.Combine(importSrc, "ok.png"), new byte[] { 0x89, 0x50, 0x4E, 0x47 });
            File.WriteAllBytes(Path.Combine(importSrc, "theme.mp3"), new byte[] { 0x49, 0x44, 0x33 });
            File.WriteAllBytes(Path.Combine(importSrc, "hero.webp"), new byte[] { 0x52, 0x49, 0x46, 0x46 });
            var imported = EditorAssetImportService.ImportAssetFiles(
                Directory.GetFiles(importSrc), importAssets, msg => rejectionLog += msg + "\n");
            if (imported.Count != 1 || !imported[0].EndsWith("ok.png"))
                throw new Exception($"Expected only ok.png imported, got [{string.Join(",", imported)}].");
            if (!rejectionLog.Contains("theme.mp3") || !rejectionLog.Contains("hero.webp") || !rejectionLog.Contains("Faz 5"))
                throw new Exception("Import rejection log must name the rejected files and the converter.");
            if (Directory.EnumerateFiles(importAssets, "*", SearchOption.AllDirectories).Any(f => f.EndsWith(".mp3") || f.EndsWith(".webp")))
                throw new Exception("Rejected formats must not be copied into the project.");
        }
        finally
        {
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

            // 33.3b converter-pending references are build-blocking errors
            AssertSingleError(scenarioRoot, "theme.mp3", "Faz 5");
            AssertSingleError(scenarioRoot, "sprite.webp", "Faz 5");
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

            // 33.3e case collisions on disk are build-blocking errors
            string collisionAssets = Path.Combine(scenarioRoot, "collision", "Assets");
            Directory.CreateDirectory(Path.Combine(collisionAssets, "images"));
            File.WriteAllBytes(Path.Combine(collisionAssets, "images", "Hero.png"), new byte[] { 4 });
            File.WriteAllBytes(Path.Combine(collisionAssets, "images", "hero.png"), new byte[] { 5 });
            var collisionNode = new NodeViewModel(9603, "Collision", 0, 0, bare: true);
            var collisionIssues = ProjectValidationService.Validate(new[] { collisionNode }, Array.Empty<ConnectionViewModel>(), collisionAssets, collisionNode.Id);
            if (!collisionIssues.Any(issue => issue.IsError && issue.Message.Contains("collision")))
                throw new Exception("Case-only duplicate files must be reported as a name collision.");
        }
        finally
        {
            try { Directory.Delete(scenarioRoot, true); } catch { }
        }

        Console.WriteLine("  ✅ [PASS] Media format contract, import rejection and validation audits verified");
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
