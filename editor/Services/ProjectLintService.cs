using System;
using System.Collections.Generic;
using System.Collections.Immutable;
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
    internal sealed record CapturedGraph(
        string Json,
        ImmutableArray<CapturedConnection> Connections,
        ImmutableArray<ulong> StartNodeIds,
        ImmutableArray<CanvasGroup> Groups,
        ImmutableArray<SubgraphDefinition> Subgraphs,
        ImmutableArray<ChapterDefinition> Chapters,
        ulong? StartNodeId);

    internal sealed record CapturedConnection(ulong? SourceId, ulong? TargetId, string OptionId);

    /// <summary>Freeze all graph data while the caller still owns the UI thread.</summary>
    public static CapturedGraph CaptureGraph(
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong? startNodeId = null,
        GraphStructureDocument? structure = null)
    {
        var nodeList = nodes.ToList();
        var connectionList = connections.ToList();
        var saved = StoryGraphSaveService.Capture(
            nodeList, connectionList, startNodeId ?? 0, activeNode: null);
        return new CapturedGraph(
            StoryGraphSaveService.SerializeFullGraph(saved),
            connectionList.Select(c => new CapturedConnection(
                c.SourceNode?.Id, c.TargetNode?.Id, c.OptionId)).ToImmutableArray(),
            nodeList.Where(n => n.IsStartNode).Select(n => n.Id).ToImmutableArray(),
            structure?.Groups.Select(g => g with {
                NodeIds = g.NodeIds.ToImmutableArray()
            }).ToImmutableArray() ?? ImmutableArray<CanvasGroup>.Empty,
            structure?.Subgraphs.Select(s => s with {
                ExitNodeIds = s.ExitNodeIds.ToImmutableArray(),
                NodeIds = s.NodeIds.ToImmutableArray()
            }).ToImmutableArray() ?? ImmutableArray<SubgraphDefinition>.Empty,
            structure?.Chapters.ToImmutableArray() ?? ImmutableArray<ChapterDefinition>.Empty,
            startNodeId);
    }

    /// <summary>Run lint on worker-owned view models reconstructed from a detached snapshot.</summary>
    public static IReadOnlyList<ProjectValidationIssue> LintCaptured(
        CapturedGraph captured,
        string assetsPath,
        ProjectLintOptions? lintOptions = null)
    {
        using var document = JsonDocument.Parse(captured.Json);
        var loaded = StoryGraphLoaderService.Load(document, ensureDefaultObjects: false);
        if (!loaded.Success)
            return new[] { new ProjectValidationIssue(true,
                $"Graph snapshot could not be loaded for lint: {loaded.ErrorMessage}") };

        var byId = loaded.Nodes.ToDictionary(n => n.Id);
        foreach (var node in loaded.Nodes)
            node.IsStartNode = captured.StartNodeIds.Contains(node.Id);
        var connections = captured.Connections.Select(c => new ConnectionViewModel(
            c.SourceId is { } source && byId.TryGetValue(source, out var sourceNode) ? sourceNode : null,
            c.TargetId is { } target && byId.TryGetValue(target, out var targetNode) ? targetNode : null,
            c.OptionId)).ToArray();
        var structure = new GraphStructureDocument();
        structure.Groups.AddRange(captured.Groups);
        structure.Subgraphs.AddRange(captured.Subgraphs);
        structure.Chapters.AddRange(captured.Chapters);
        return Lint(loaded.Nodes, connections, assetsPath, captured.StartNodeId,
            structure, lintOptions);
    }

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
        if (options.CheckLongAudio)
            RunRule(issues, () => CheckLongAudioStreaming(nodeList, assetsPath, disk, issues, options));
        if (options.CheckCharacterLayers)
            RunRule(issues, () => CheckCharacterLayers(nodeList, assetsPath, disk, issues, options));
        if (options.CheckPrefetch)
            RunRule(issues, () => CheckPrefetchAssets(nodeList, assetsPath, disk, issues, options));
        if (options.CheckConvertedFreshness)
            RunRule(issues, () => CheckConvertedFreshness(assetsPath, disk, issues, options));

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
        var captured = CaptureGraph(nodes, connections, startNodeId, structure);
        var options = lintOptions ?? new ProjectLintOptions();
        return Task.Run(
            () => LintCaptured(captured, assetsPath, options),
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

    // Rule 1: choice / condition targets
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

    // Rule 2: Lua pre-scan
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
        // The Scripts/ sweep probes both "Scripts" and "scripts" spellings.
        // On a case-insensitive filesystem (Windows) both resolve to the
        // same directory, so the same physical file must dedupe to one
        // issue; on case-sensitive systems the spellings are distinct files
        // and stay distinct (tur-6: duplicate outside-Assets error on Windows).
        var seenFiles = new HashSet<string>(OperatingSystem.IsWindows()
            ? StringComparer.OrdinalIgnoreCase
            : StringComparer.Ordinal);
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

    // Rule 3: dialogue text / translation / glyph
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

    // Rule 5: long-audio streaming budget
    // Faz 5 Dilim 1: BGM tracks whose on-disk size exceeds the 64 MiB
    // decoded-PCM budget take the OGG streaming path (header-probed
    // duration over threshold); over-budget non-OGG assets fall back to
    // the RAM decode path and its cap. Advisory warnings only (batch-only,
    // never merged into the inline inspector). Missing files are skipped
    // (Validate owns missing-asset errors).

    private const long LongAudioBudgetBytes = 64L * 1024 * 1024;

    private static void CheckLongAudioStreaming(
        List<NodeViewModel> nodeList,
        string assetsPath,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        string root;
        try
        {
            root = Path.GetFullPath(assetsPath);
        }
        catch (Exception)
        {
            return;
        }
        int added = 0;
        foreach (var node in nodeList)
        {
            foreach (var audio in node.AllComponents.OfType<AudioComponentViewModel>())
            {
                if (added >= options.MaxIssuesPerRule)
                {
                    issues.Add(new(false,
                        $"Long-audio scan capped at {options.MaxIssuesPerRule} entries; remaining BGM tracks unchecked."));
                    return;
                }
                if (!audio.IsEnabled || string.IsNullOrWhiteSpace(audio.BgmTrack))
                    continue;
                string? rel = ResolveDiskRelative(audio.BgmTrack, root, disk);
                if (rel is null)
                    continue;
                string full;
                try
                {
                    full = Path.GetFullPath(Path.Combine(root, rel));
                    if (!ProjectFileSystem.IsSameOrDescendant(full, root) || !File.Exists(full))
                        continue;
                }
                catch (Exception)
                {
                    continue;
                }
                long length;
                try
                {
                    length = new FileInfo(full).Length;
                }
                catch (Exception)
                {
                    continue; // Unreadable here; the disk-scan warning already covers IO failures.
                }
                if (length <= LongAudioBudgetBytes)
                    continue;
                bool isOgg = rel.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase);
                issues.Add(new(false,
                    isOgg
                        ? $"BGM '{rel}' (node #{node.Id}) is {length / (1024 * 1024)} MiB (over 64 MiB); it takes the OGG streaming path."
                        : $"BGM '{rel}' (node #{node.Id}) is {length / (1024 * 1024)} MiB (over 64 MiB); only OGG streams, so this asset falls back to the RAM decode path. Convert to OGG to stream it.",
                    node.Id, rel));
                added++;
            }
        }
    }

    // Rule 5 (Faz 5 Dilim 3): eksik katman asset'i
    // Katman slotu dolu ama dosyası diskte yoksa advisory WARNING verilir
    // (error YOK). Missing-file sahipliği Validate'dedir (orası error
    // verir); bu kural yalnızca erken uyarıdır. Boş slotlar sessizdir.
    // Batch-only: inline inspector'a karışmaz.

    private static void CheckCharacterLayers(
        List<NodeViewModel> nodeList,
        string assetsPath,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        string root;
        try
        {
            root = Path.GetFullPath(assetsPath);
        }
        catch (Exception)
        {
            return;
        }
        int added = 0;
        foreach (var node in nodeList)
        {
            foreach (var character in node.AllComponents.OfType<CharacterComponentViewModel>())
            {
                if (added >= options.MaxIssuesPerRule)
                {
                    issues.Add(new(false,
                        $"Character-layers scan capped at {options.MaxIssuesPerRule} entries; remaining layers unchecked."));
                    return;
                }
                if (!character.IsEnabled)
                    continue;
                Dictionary<string, object> data;
                try
                {
                    data = character.Serialize();
                }
                catch (Exception)
                {
                    continue;
                }
                IReadOnlyList<CharacterLayerAssetRef> refs;
                try
                {
                    refs = CharacterLayersService.CollectAssetRefs(data);
                }
                catch (Exception)
                {
                    continue;
                }
                foreach (var assetRef in refs)
                {
                    if (added >= options.MaxIssuesPerRule)
                    {
                        issues.Add(new(false,
                            $"Character-layers scan capped at {options.MaxIssuesPerRule} entries; remaining layers unchecked."));
                        return;
                    }
                    if (ResolveDiskRelative(assetRef.Asset, root, disk) is not null)
                        continue;
                    issues.Add(new(false,
                        $"Character {assetRef.Location} slot '{assetRef.Slot}' asset '{assetRef.Asset}' (node #{node.Id}) is missing on disk; preview falls back silently.",
                        node.Id, assetRef.Asset));
                    added++;
                }
            }
        }
    }

    // Rule 6 (Faz 5 Dilim 4): eksik prefetch asset'i
    // Prefetch penceresi (aktif + sonraki sahne) üst-seviye image/audio
    // yollarını kuyruğa alır; dosyası diskte olmayan bir asset kuyruğu
    // durdurmaz ama missing sayılır + tanı bırakır. Bu kural eksik
    // dosyayı erken advisory WARNING ile işaretler (error YOK).
    // Missing-file sahipliği Validate'dedir (orası error verir); katman
    // assetleri CheckCharacterLayers'a aittir (çift rapor engellenir:
    // burada yalnız üst-seviye AssetKeys taranır). Boş yollar sessizdir.
    // Batch-only: inline inspector'a karışmaz.

    private static void CheckPrefetchAssets(
        List<NodeViewModel> nodeList,
        string assetsPath,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        string root;
        try
        {
            root = Path.GetFullPath(assetsPath);
        }
        catch (Exception)
        {
            return;
        }
        int added = 0;
        foreach (var node in nodeList)
        {
            foreach (var component in node.AllComponents)
            {
                if (added >= options.MaxIssuesPerRule)
                {
                    issues.Add(new(false,
                        $"Prefetch scan capped at {options.MaxIssuesPerRule} entries; remaining assets unchecked."));
                    return;
                }
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
                    if (added >= options.MaxIssuesPerRule)
                    {
                        issues.Add(new(false,
                            $"Prefetch scan capped at {options.MaxIssuesPerRule} entries; remaining assets unchecked."));
                        return;
                    }
                    if (!ProjectValidationService.AssetKeys.Contains(pair.Key) || pair.Value is not string asset)
                        continue;
                    if (string.IsNullOrWhiteSpace(asset))
                        continue;
                    if (ResolveDiskRelative(asset, root, disk) is not null)
                        continue;
                    issues.Add(new(false,
                        $"Prefetch asset '{asset}' (node #{node.Id}) is missing on disk; chapter prefetch will count it missing and continue.",
                        node.Id, asset));
                    added++;
                }
            }
        }
    }

    // Rule 7 (Faz 5 Dilim 5): dönüştürülmüş asset tazeliği
    // Her <c>&lt;çıktı&gt;.rowlconv.json</c> sidecar için iki hash karşılaştırılır:
    // sidecar'daki output_sha256 vs diskteki çıktı, sidecar'daki source_sha256
    // vs kaynak (SourceAssets/ altında gövde-adı+eşleşen kaynak uzantıyla
    // aranır; bulunamazsa kaynak kolu sessiz geçilir). Sözleşme
    // (docs/MEDIA_CONVERTERS_CONTRACT.md): OGG sidecar'daki source_sha256,
    // decode edilmiş PCM baytlarının hash'idir — ses kolu kaynağı harici
    // ffmpeg ile çözüp PCM hash'ini karşılaştırır (ham MP3/FLAC hash'i DEĞİL);
    // decode edilemezse kol sessiz geçilir (fail-open). WebP kolu dosya
    // baytlarını kullanır (araç girdisi doğrudan .webp dosyasıdır).
    // Uyuşmazlık advisory WARNING'dir (fail değil). Bozuk sidecar da warning
    // verir (error YOK). Batch-only: inline inspector'a karışmaz.

    private static void CheckConvertedFreshness(
        string assetsPath,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        string root;
        string? sourceRoot;
        try
        {
            root = Path.GetFullPath(assetsPath);
            if (!Directory.Exists(root))
                return;
            string? projectRoot = Path.GetDirectoryName(root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            sourceRoot = projectRoot is null ? null : Path.Combine(projectRoot, "SourceAssets");
        }
        catch (Exception)
        {
            return;
        }
        int added = 0;
        foreach (string rel in disk.ExactPaths.OrderBy(p => p, StringComparer.Ordinal))
        {
            if (added >= options.MaxIssuesPerRule)
            {
                issues.Add(new(false,
                    $"Converted-asset scan capped at {options.MaxIssuesPerRule} entries; remaining sidecars unchecked."));
                return;
            }
            if (!rel.EndsWith(MediaConverterService.SidecarSuffix, StringComparison.Ordinal))
                continue;
            string outputRel = rel[..^MediaConverterService.SidecarSuffix.Length];
            string outputFull = Path.Combine(root, outputRel);
            MediaConversionProvenance? provenance;
            try
            {
                provenance = MediaConversionProvenance.TryReadFile(outputFull + MediaConverterService.SidecarSuffix);
            }
            catch (Exception)
            {
                provenance = null;
            }
            if (provenance is null)
            {
                issues.Add(new(false,
                    $"Converted asset '{outputRel}' has an unreadable sidecar ('{rel}'); reconvert to restore provenance.",
                    null, outputRel));
                added++;
                continue;
            }
            string outputHash;
            try
            {
                if (!File.Exists(outputFull))
                    continue; // Missing-file sahipliği Validate'dedir.
                outputHash = MediaConverterService.ComputeFileSha256(outputFull);
            }
            catch (Exception)
            {
                continue; // Unreadable burada; disk-scan uyarısı IO'yu kapsar.
            }
            if (!string.Equals(provenance.OutputSha256, outputHash, StringComparison.OrdinalIgnoreCase))
            {
                issues.Add(new(false,
                    $"Converted asset '{outputRel}' changed on disk (output hash mismatch with '{rel}'); reconvert to refresh it.",
                    null, outputRel));
                added++;
                continue;
            }
            string? sourceFull = FindConversionSource(outputRel, sourceRoot, provenance.SourcePath);
            if (sourceFull is null)
                continue; // Kaynak bulunamadı: yargısız sessiz.
            string? sourceHash;
            try
            {
                if (string.Equals(Path.GetExtension(outputRel), ".ogg", StringComparison.OrdinalIgnoreCase))
                {
                    // Ses: sidecar source_sha256 = decode PCM hash'i.
                    sourceHash = MediaConverterService.TryDecodeSourceHashAsync(
                        sourceFull, options.ConverterOptions ?? new MediaConverterService.ConverterOptions())
                        .GetAwaiter().GetResult();
                    if (sourceHash is null)
                        continue; // Decode yoksa yargısız sessiz (fail-open).
                }
                else
                {
                    sourceHash = MediaConverterService.ComputeFileSha256(sourceFull);
                }
            }
            catch (Exception)
            {
                continue;
            }
            if (!string.Equals(provenance.SourceSha256, sourceHash, StringComparison.OrdinalIgnoreCase))
            {
                issues.Add(new(false,
                    $"Converted asset '{outputRel}' source changed (source hash mismatch with '{rel}'); reconvert to refresh it.",
                    null, outputRel));
                added++;
            }
        }
    }

    /// <summary>
    /// Çıktı yolundan kaynak dosyayı bulur: önce sidecar'daki source_path
    /// (SourceAssets-göreli), sonra gövde-adı + kaynak uzantı araması.
    /// </summary>
    internal static string? FindConversionSource(
        string outputRel, string? sourceRoot, string? provenanceSourcePath = null)
    {
        if (string.IsNullOrWhiteSpace(sourceRoot) || !Directory.Exists(sourceRoot))
            return null;
        if (!string.IsNullOrWhiteSpace(provenanceSourcePath))
        {
            try
            {
                string candidate = Path.GetFullPath(Path.Combine(sourceRoot, provenanceSourcePath.Replace('\\', '/')));
                if (ProjectFileSystem.IsSameOrDescendant(candidate, Path.GetFullPath(sourceRoot))
                    && File.Exists(candidate))
                    return candidate;
            }
            catch (Exception)
            {
                // Düşer: gövde-adı aramasına.
            }
        }
        string stem = Path.GetFileNameWithoutExtension(outputRel).Replace('\\', '/');
        string outputExt = Path.GetExtension(outputRel).ToLowerInvariant();
        string[] sourceExts = outputExt switch
        {
            ".ogg" => new[] { ".mp3", ".flac" },
            ".png" => new[] { ".webp" },
            _ => Array.Empty<string>(),
        };
        foreach (string sourceExt in sourceExts)
        {
            string[] hits;
            try
            {
                hits = Directory.GetFiles(sourceRoot, stem + sourceExt, SearchOption.AllDirectories);
            }
            catch (Exception)
            {
                continue;
            }
            Array.Sort(hits, StringComparer.Ordinal);
            if (hits.Length > 0)
                return hits[0];
        }
        return null;
    }

    // Rule 4: unused assets
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
                // Faz 5 Dilim 3: katmanlı karakter referansları (layers +
                // expressions) iç-içe anahtarlardadır; kullanılmayan sayılmamaları
                // ve pakete dahil bilinmeleri için aynı kümeye eklenir.
                try
                {
                    foreach (var nested in CharacterLayersService.CollectAssetRefs(data))
                    {
                        if (string.IsNullOrWhiteSpace(nested.Asset))
                            continue;
                        string nestedNormalized = nested.Asset.Replace('\\', '/');
                        foreach (string prefix in AssetPrefixes)
                            referenced.Add(prefix + nestedNormalized);
                    }
                }
                catch (Exception)
                {
                    // Bozuk katman datası unused taramasını düşürmez.
                }
            }
        }

        // Faz 5 Dilim 2 fix turu 1: per-node BedB kaldırıldı; kullanılmayan
        // ambience dosyaları normal unused-asset kuralına tabidir.
        // Faz 5 Dilim 4: chapter dosyaları (chapters/) paket asset
        // listesindedir — referans kümesine eklenir, yeni format yok
        // (mevcut chapter_index.json + LoadChapterFile şeması). Diskte
        // duran ama hiçbir node'un referans vermediği chapter JSON'ları
        // kullanılmayan sayılmaz.
        foreach (string chapterRef in PrefetchChaptersService.CollectChapterPackageRefs(disk.ExactPaths))
            referenced.Add(chapterRef);
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
    bool CheckLongAudio = true,
    bool CheckCharacterLayers = true,
    bool CheckPrefetch = true,
    bool CheckConvertedFreshness = true,
    MediaConverterService.ConverterOptions? ConverterOptions = null,
    long MaxLuaBytes = 256 * 1024,
    int MaxIssuesPerRule = 200);
