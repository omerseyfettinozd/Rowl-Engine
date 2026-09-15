using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Services.Localization;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 4 Dilim 5 — batch project linter. Runs on top of
/// <see cref="ProjectValidationService"/>: it first inherits every batch
/// rule via <c>Validate</c> (reachability, content ids, disk index,
/// asset references, structure contract) and then appends lint-only rules
/// (Lua pre-scan, choice/condition targets, translation + glyph coverage,
/// unused assets). Results are <see cref="ProjectValidationIssue"/> records
/// — the same type the Issues panel shows — so no conversion is needed.
/// Pure static, no UI access; heavy callers run it off the UI thread
/// (see <see cref="AssetScanWorker"/>).
/// </summary>
internal static class ProjectLintService
{
    /// <summary>Matches Lua require("mod"), require 'mod', require("a/b").</summary>
    private static readonly Regex LuaRequirePattern = new(
        @"\brequire\s*(?:\(\s*)?[""']([^""']+)[""']",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    /// <summary>Matches dofile("...") / loadfile('...') path literals.</summary>
    private static readonly Regex LuaLoadFilePattern = new(
        @"\b(?:dofile|loadfile)\s*\(\s*[""']([^""']+)[""']",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    private static readonly string[] AssetPrefixes = { "", "images/", "audio/", "fonts/", "scripts/" };

    public static IReadOnlyList<ProjectValidationIssue> Lint(
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        string assetsPath,
        ulong? startNodeId = null,
        GraphStructureDocument? structure = null,
        ProjectLintOptions? lintOptions = null)
    {
        var options = lintOptions ?? new ProjectLintOptions();
        var nodeList = nodes.ToList();
        var connectionList = connections.ToList();

        // Single directory scan shared by Validate and every lint rule below
        // (2000-node p95 budget: O(N+E+files), no rescans).
        var disk = ProjectValidationService.BuildDiskIndex(assetsPath);
        var issues = new List<ProjectValidationIssue>(
            ProjectValidationService.Validate(nodeList, connectionList, assetsPath, startNodeId, structure, disk));

        RunRule(issues, () => CheckChoiceAndConditionTargets(nodeList, issues, options));
        if (options.CheckLuaScripts)
            RunRule(issues, () => CheckLuaScripts(nodeList, assetsPath, disk, issues, options));
        if (options.CheckTranslations || options.CheckGlyphCoverage)
            RunRule(issues, () => CheckDialogueText(nodeList, assetsPath, issues, options));
        if (options.CheckUnusedAssets)
            RunRule(issues, () => CheckUnusedAssets(nodeList, disk, issues, options));

        return issues;
    }

    /// <summary>Runs the batch lint on a worker thread (UI never blocks).</summary>
    public static Task<IReadOnlyList<ProjectValidationIssue>> LintAsync(
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        string assetsPath,
        ulong? startNodeId = null,
        GraphStructureDocument? structure = null,
        ProjectLintOptions? lintOptions = null,
        CancellationToken cancellationToken = default)
    {
        // Materialize the live collections on the caller (UI) thread; only
        // the scan + rule evaluation leaves it.
        var nodeSnapshot = nodes.ToList();
        var connectionSnapshot = connections.ToList();
        var options = lintOptions ?? new ProjectLintOptions();
        return Task.Run(
            () => Lint(nodeSnapshot, connectionSnapshot, assetsPath, startNodeId, structure, options),
            cancellationToken);
    }

    /// <summary>Throw yerine issue: a failing rule degrades to one warning.</summary>
    private static void RunRule(
        List<ProjectValidationIssue> issues, Action rule)
    {
        try
        {
            rule();
        }
        catch (Exception error)
        {
            issues.Add(new(false,
                $"Project linter rule failed ({error.GetType().Name}: {error.Message}). Other results are unaffected."));
        }
    }

    // ── Rule 1: choice / condition targets ──────────────────────────
    // Widens the inline TargetNodeId==0 check (error there, error here) to
    // the batch pass and to deleted-node targets, on every node regardless
    // of reachability. Disabled components/options are skipped.

    private static void CheckChoiceAndConditionTargets(
        List<NodeViewModel> nodeList,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        var byId = new HashSet<ulong>(nodeList.Select(n => n.Id));
        int added = 0;
        bool CapReached() => added >= options.MaxIssuesPerRule;

        foreach (var node in nodeList)
        {
            foreach (var choice in node.AllComponents.OfType<ChoiceComponentViewModel>())
            {
                if (!choice.IsEnabled || CapReached())
                    continue;
                foreach (var option in choice.Options)
                {
                    if (!option.IsEnabled || CapReached())
                        continue;
                    if (option.TargetNodeId == 0)
                    {
                        issues.Add(new(true,
                            $"Node #{node.Id} choice option '{option.Text}' has no target node (target 0). Connect it or disable the option.",
                            node.Id));
                        added++;
                    }
                    else if (!byId.Contains(option.TargetNodeId))
                    {
                        issues.Add(new(true,
                            $"Node #{node.Id} choice option '{option.Text}' points to deleted node #{option.TargetNodeId}. Reconnect it or disable the option.",
                            node.Id));
                        added++;
                    }
                }
            }

            foreach (var condition in node.AllComponents.OfType<ConditionComponentViewModel>())
            {
                if (!condition.IsEnabled || CapReached())
                    continue;
                string raw = (condition.FailTargetNodeId ?? string.Empty).Trim();
                if (raw.Length == 0)
                    continue;
                if (!ulong.TryParse(raw, out ulong target) || target == 0)
                {
                    issues.Add(new(true,
                        $"Node #{node.Id} condition has an invalid fail target '{condition.FailTargetNodeId}' (expected a node id).",
                        node.Id));
                    added++;
                }
                else if (!byId.Contains(target))
                {
                    issues.Add(new(true,
                        $"Node #{node.Id} condition fail target points to deleted node #{target}.",
                        node.Id));
                    added++;
                }
            }
        }
    }

    // ── Rule 2: Lua pre-scan ─────────────────────────────────────────
    // Empty / oversized .lua files warn; a require/dofile/loadfile literal
    // escaping the project Assets tree errors. Serialize() is the only
    // component access (no reflection); file reads stay under assetsPath.

    private static void CheckLuaScripts(
        List<NodeViewModel> nodeList,
        string assetsPath,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        string root = Path.GetFullPath(assetsPath);
        var seenFiles = new HashSet<string>(StringComparer.Ordinal);
        int added = 0;

        void ScanLuaFile(string relPath, ulong? nodeId, string display)
        {
            if (!seenFiles.Add(relPath) || added >= options.MaxIssuesPerRule)
                return;
            string full;
            try
            {
                full = Path.GetFullPath(Path.Combine(root, relPath));
                if (!ProjectFileSystem.IsSameOrDescendant(full, root) || !File.Exists(full))
                    return;
            }
            catch (Exception)
            {
                return;
            }

            FileInfo info;
            string source;
            try
            {
                info = new FileInfo(full);
                if (!string.Equals(info.Extension, ".lua", StringComparison.OrdinalIgnoreCase))
                    return;
                if (info.Length == 0)
                {
                    issues.Add(new(false,
                        $"Lua script '{relPath}' ({display}) is empty.", nodeId, relPath));
                    added++;
                    return;
                }
                if (info.Length > options.MaxLuaBytes)
                {
                    issues.Add(new(false,
                        $"Lua script '{relPath}' ({display}) is {info.Length / 1024} KB (over {options.MaxLuaBytes / 1024} KB); split it for load-time safety.",
                        nodeId, relPath));
                    added++;
                }
                source = File.ReadAllText(full);
            }
            catch (Exception)
            {
                return; // Unreadable here; the disk-scan warning already covers IO failures.
            }

            foreach (Match match in LuaRequirePattern.Matches(source).Cast<Match>().Concat(LuaLoadFilePattern.Matches(source).Cast<Match>()))
            {
                if (added >= options.MaxIssuesPerRule)
                    return;
                string literal = match.Groups[1].Value.Trim();
                if (IsOutsideAssetsRequire(literal, full, root, out string? reason))
                {
                    issues.Add(new(true,
                        $"Lua script '{relPath}' ({display}) loads '{literal}' outside the project Assets tree ({reason}). Keep scripts and their requires under Assets.",
                        nodeId, relPath));
                    added++;
                    break; // One outside-load error per file is enough to block.
                }
            }
        }

        // Referenced scripts first (node attribution), then an Scripts/
        // sweep for orphan .lua files (batch-only; inline never scans disk).
        foreach (var node in nodeList)
        {
            foreach (var script in node.AllComponents.OfType<ScriptComponentViewModel>())
            {
                if (!script.IsEnabled)
                    continue;
                string asset;
                try
                {
                    if (!script.Serialize().TryGetValue("path", out object? raw) || raw is not string s)
                        continue;
                    asset = s;
                }
                catch (Exception)
                {
                    continue;
                }
                if (string.IsNullOrWhiteSpace(asset))
                    continue;
                string? rel = ResolveDiskRelative(asset, root, disk);
                if (rel is not null)
                    ScanLuaFile(rel, node.Id, $"node #{node.Id}");
            }
        }

        foreach (string scriptsDir in new[] { "Scripts", "scripts" })
        {
            string fullDir = Path.Combine(root, scriptsDir);
            if (!Directory.Exists(fullDir))
                continue;
            string[] files;
            try
            {
                files = Directory.GetFiles(fullDir, "*.lua", SearchOption.AllDirectories);
            }
            catch (Exception)
            {
                continue;
            }
            foreach (string file in files)
            {
                if (added >= options.MaxIssuesPerRule)
                    return;
                string rel = Path.GetRelativePath(root, file).Replace('\\', '/');
                ScanLuaFile(rel, null, "Scripts sweep");
            }
        }
    }

    private static bool IsOutsideAssetsRequire(
        string literal, string requiringFile, string assetsRoot, out string? reason)
    {
        reason = null;
        if (string.IsNullOrWhiteSpace(literal))
            return false;
        // Bare module names (no path shape) resolve through Lua package.path;
        // only explicit path shapes are judged here.
        if (Path.IsPathFullyQualified(literal))
        {
            reason = "absolute path";
            return true;
        }
        if (literal.Length >= 3 && char.IsLetter(literal[0]) && literal[1] == ':'
            && (literal[2] == '\\' || literal[2] == '/'))
        {
            reason = "drive-rooted path";
            return true;
        }
        string normalized = literal.Replace('\\', '/');
        if (normalized.StartsWith('/'))
        {
            reason = "filesystem-absolute path";
            return true;
        }
        bool hasPathShape = normalized.Contains('/') || normalized.Contains("..")
            || normalized.EndsWith(".lua", StringComparison.OrdinalIgnoreCase);
        if (!hasPathShape)
            return false;
        try
        {
            string? baseDir = Path.GetDirectoryName(requiringFile);
            string resolved = Path.GetFullPath(Path.Combine(baseDir ?? assetsRoot, normalized));
            if (!ProjectFileSystem.IsSameOrDescendant(resolved, assetsRoot))
            {
                reason = "'..' escapes Assets";
                return true;
            }
        }
        catch (Exception)
        {
            return false;
        }
        return false;
    }

    /// <summary>Resolves an asset reference to its disk-relative path (exact match only).</summary>
    private static string? ResolveDiskRelative(
        string asset, string root, ProjectValidationService.DiskIndex disk)
    {
        string normalized = asset.Replace('\\', '/');
        foreach (string prefix in AssetPrefixes)
        {
            string candidate = prefix + normalized;
            if (disk.ExactPaths.Contains(candidate))
                return candidate;
        }
        return null;
    }

    // ── Rule 3: dialogue text / translation / glyph ──────────────────
    // Empty speaker/text warn (same severity as the inline inspector rule).
    // Translation coverage reuses the Faz 3 desk contract: missing catalog
    // entries and stale source hashes warn per locale. Unshapable glyphs
    // (controls, U+FFFD, noncharacters) warn per node — same severity as
    // the Faz 3 font-coverage gate findings.

    private static void CheckDialogueText(
        List<NodeViewModel> nodeList,
        string assetsPath,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        var inventory = new List<(ulong nodeId, string contentId, string speaker, string text)>();
        int added = 0;

        foreach (var node in nodeList)
        {
            foreach (var dialogue in node.AllComponents.OfType<DialogueComponentViewModel>())
            {
                if (!dialogue.IsEnabled || added >= options.MaxIssuesPerRule)
                    continue;
                if (options.CheckTranslations || options.CheckGlyphCoverage)
                {
                    if (string.IsNullOrWhiteSpace(dialogue.DialogueText))
                    {
                        issues.Add(new(false,
                            $"Node #{node.Id} dialogue text is empty.", node.Id));
                        added++;
                    }
                    else if (string.IsNullOrWhiteSpace(dialogue.Speaker))
                    {
                        issues.Add(new(false,
                            $"Node #{node.Id} speaker is empty.", node.Id));
                        added++;
                    }
                }

                if (options.CheckGlyphCoverage && !string.IsNullOrEmpty(dialogue.DialogueText))
                {
                    var bad = FindUnshapableCodepoints(dialogue.DialogueText, dialogue.Speaker);
                    if (bad.Count > 0 && added < options.MaxIssuesPerRule)
                    {
                        issues.Add(new(false,
                            $"Node #{node.Id} dialogue contains {bad.Count} potentially unshapable character(s) ({string.Join(", ", bad)}); verify them in the font-coverage gate.",
                            node.Id));
                        added++;
                    }
                }

                if (options.CheckTranslations)
                {
                    string? normalized = ContentIdService.Normalize(dialogue.ContentId ?? string.Empty);
                    if (normalized is not null)
                        inventory.Add((node.Id, normalized, dialogue.Speaker ?? string.Empty, dialogue.DialogueText ?? string.Empty));
                }
            }
        }

        if (options.CheckTranslations && inventory.Count > 0 && added < options.MaxIssuesPerRule)
            CheckTranslationCoverage(inventory, assetsPath, issues, options, ref added);
    }

    private static List<string> FindUnshapableCodepoints(string? text, string? speaker)
    {
        var bad = new List<string>();
        var seen = new HashSet<int>();
        foreach (var part in new[] { text, speaker })
        {
            if (string.IsNullOrEmpty(part))
                continue;
            foreach (var rune in part.EnumerateRunes())
            {
            int value = rune.Value;
            // Cc controls (minus TAB/LF/CR) genuinely break shaping runs.
            bool isControl = value < 0x20 || (value >= 0x7F && value <= 0x9F);
            bool unshapable =
                ((isControl && value != '\t' && value != '\n' && value != '\r') ||
                value == 0xFFFD ||
                (value >= 0xFDD0 && value <= 0xFDEF) ||
                value == 0xFFFE || value == 0xFFFF ||
                (value >= 0x10000 && (value & 0xFFFE) == 0xFFFE));
            if (unshapable && seen.Add(value) && bad.Count < 8)
                bad.Add($"U+{value:X4}");
            }
        }
        return bad;
    }

    private static void CheckTranslationCoverage(
        List<(ulong nodeId, string contentId, string speaker, string text)> inventory,
        string assetsPath,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options,
        ref int added)
    {
        string localesDir;
        try
        {
            localesDir = Path.Combine(Path.GetFullPath(assetsPath), "locales");
        }
        catch (Exception)
        {
            return;
        }
        if (!Directory.Exists(localesDir))
            return; // No locales yet: nothing to cover, no noise.

        string defaultLocale = ReadDefaultLocale(assetsPath);
        string[] catalogs;
        try
        {
            catalogs = Directory.GetFiles(localesDir, "*.json", SearchOption.TopDirectoryOnly);
        }
        catch (Exception)
        {
            return;
        }

        foreach (string catalogPath in catalogs.OrderBy(p => p, StringComparer.Ordinal))
        {
            if (added >= options.MaxIssuesPerRule)
                return;
            string locale = Path.GetFileNameWithoutExtension(catalogPath);
            if (string.Equals(LocalizationService.NormalizeLocale(locale), defaultLocale, StringComparison.Ordinal))
                continue; // Source language needs no translation rows.

            Dictionary<string, (string text, string? sourceHash)> entries;
            try
            {
                entries = ReadCatalogEntries(catalogPath);
            }
            catch (Exception error) when (error is IOException or JsonException or UnauthorizedAccessException)
            {
                issues.Add(new(false,
                    $"Locale catalog '{locale}.json' could not be read ({error.GetType().Name}); translation coverage for '{locale}' was skipped."));
                added++;
                continue;
            }

            foreach (var (nodeId, contentId, speaker, text) in inventory)
            {
                if (added >= options.MaxIssuesPerRule)
                    return;
                if (!entries.TryGetValue(contentId, out var entry) || string.IsNullOrEmpty(entry.text))
                {
                    issues.Add(new(false,
                        $"Node #{nodeId} content_id '{contentId}' has no '{locale}' translation (missing).",
                        nodeId));
                    added++;
                }
                else if (entry.sourceHash is not null &&
                         !string.Equals(entry.sourceHash,
                             TranslationInventoryService.ComputeSourceHash(speaker, text),
                             StringComparison.Ordinal))
                {
                    issues.Add(new(false,
                        $"Node #{nodeId} content_id '{contentId}' source text changed since the '{locale}' translation (stale).",
                        nodeId));
                    added++;
                }
            }
        }
    }

    private static string ReadDefaultLocale(string assetsPath)
    {
        try
        {
            string? assetsRoot = Path.GetDirectoryName(Path.GetFullPath(assetsPath));
            if (assetsRoot is null)
                return LocalizationService.FallbackDefaultLocale;
            string manifest = Path.Combine(assetsRoot, "project.rowlproj");
            if (!File.Exists(manifest))
                return LocalizationService.FallbackDefaultLocale;
            var locales = LocalizationService.ParseManifestLocales(File.ReadAllText(manifest));
            return LocalizationService.NormalizeLocale(locales.DefaultLocale)
                ?? LocalizationService.FallbackDefaultLocale;
        }
        catch (Exception)
        {
            return LocalizationService.FallbackDefaultLocale;
        }
    }

    private static Dictionary<string, (string text, string? sourceHash)> ReadCatalogEntries(string catalogPath)
    {
        var result = new Dictionary<string, (string, string?)>(StringComparer.Ordinal);
        using var document = JsonDocument.Parse(File.ReadAllText(catalogPath));
        if (document.RootElement.ValueKind != JsonValueKind.Object)
            throw new JsonException("Locale catalog root must be a JSON object.");
        if (!document.RootElement.TryGetProperty("entries", out JsonElement entries) ||
            entries.ValueKind != JsonValueKind.Object)
            throw new JsonException("Locale catalog entries must be an object.");
        foreach (JsonProperty row in entries.EnumerateObject())
        {
            if (row.Value.ValueKind != JsonValueKind.Object)
                continue;
            string text = row.Value.TryGetProperty("text", out JsonElement textElement) &&
                textElement.ValueKind == JsonValueKind.String
                    ? textElement.GetString() ?? string.Empty : string.Empty;
            string? hash = row.Value.TryGetProperty("source_hash", out JsonElement hashElement) &&
                hashElement.ValueKind == JsonValueKind.String
                    ? hashElement.GetString() : null;
            result[row.Name] = (text, hash);
        }
        return result;
    }

    // ── Rule 4: unused assets ────────────────────────────────────────
    // ExactPaths MINUS the referenced-asset candidate set (normalized +
    // images/audio/fonts/scripts prefixes, mirroring batch resolution) =
    // unused warnings. NodeId is null (file-level), AssetPath is set.
    // Batch-only: never merged into the inline inspector. Own recovery
    // bookkeeping (.recovery/) and non-referencable files are excluded.

    private static void CheckUnusedAssets(
        List<NodeViewModel> nodeList,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        var referenced = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in nodeList)
        {
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
                    if (string.IsNullOrWhiteSpace(asset))
                        continue;
                    string normalized = asset.Replace('\\', '/');
                    foreach (string prefix in AssetPrefixes)
                        referenced.Add(prefix + normalized);
                }
            }
        }

        int added = 0;
        foreach (string rel in disk.ExactPaths.OrderBy(p => p, StringComparer.Ordinal))
        {
            if (added >= options.MaxIssuesPerRule)
            {
                issues.Add(new(false,
                    $"Unused-asset scan capped at {options.MaxIssuesPerRule} entries; remaining files unchecked."));
                break;
            }
            if (referenced.Contains(rel) || !IsReferencableAsset(rel))
                continue;
            issues.Add(new(false, $"Asset '{rel}' is not referenced by any node.", null, rel));
            added++;
        }
    }

    private static bool IsReferencableAsset(string rel)
    {
        // Own crash-recovery bookkeeping is never an unused asset.
        if (rel.StartsWith(".recovery/", StringComparison.Ordinal) || rel.Contains("/.recovery/"))
            return false;
        // Story graph / catalog / manifest documents are load-bearing, not node assets.
        if (rel.EndsWith(".json", StringComparison.OrdinalIgnoreCase) &&
            !rel.StartsWith("scripts/", StringComparison.OrdinalIgnoreCase) &&
            !rel.StartsWith("Scripts/", StringComparison.Ordinal))
            return false;
        string lower = rel.ToLowerInvariant();
        if (lower.StartsWith("images/") || lower.StartsWith("audio/") ||
            lower.StartsWith("fonts/") || lower.StartsWith("scripts/"))
            return true;
        string ext = Path.GetExtension(lower);
        return MediaFormatCatalog.IsAcceptedMediaExtension(ext) ||
               MediaFormatCatalog.IsConverterPendingExtension(ext) ||
               MediaFormatCatalog.IsKnownUnsupportedMediaExtension(ext) ||
               string.Equals(ext, ".lua", StringComparison.OrdinalIgnoreCase);
    }
}

/// <summary>Toggles + caps for <see cref="ProjectLintService"/> batch rules.</summary>
internal sealed record ProjectLintOptions(
    bool CheckLuaScripts = true,
    bool CheckTranslations = true,
    bool CheckGlyphCoverage = true,
    bool CheckUnusedAssets = true,
    long MaxLuaBytes = 256 * 1024,
    int MaxIssuesPerRule = 200);
