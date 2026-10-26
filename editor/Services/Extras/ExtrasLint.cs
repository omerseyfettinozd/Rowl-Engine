using System;
using System.Collections.Generic;
using System.Linq;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services.Extras;

/// <summary>
/// D07-3 project-lint rules for unlockable content (<c>Assets/json/extras.json</c> and the
/// "unlock" node component). They restate what the native engine does with the catalog
/// (D07-1: <c>docs/EXTRAS_UNLOCKS.md</c> sections 1 and 3), so a clean lint means the player
/// loads every item and can open it:
/// <list type="bullet">
/// <item>an "unlock" component naming an id that is not in the catalog of its kind (or an
/// unknown kind, or no id) is an error: the engine writes nothing and logs a warning;</item>
/// <item>a catalog the engine skips (bad or repeated id, unsafe path, wrong type, too many
/// items) or ignores whole is an error;</item>
/// <item>a catalog image / track missing on disk uses the node asset-reference level (error);</item>
/// <item>a catalog item nothing can open is a warning: a gallery image no node shows as a
/// background or character sprite and no unlock component names, a music track no node
/// plays as BGM and no unlock names, an achievement without an unlock component.</item>
/// </list>
/// The automatic CG / music match uses the engine's canonical path form
/// (<see cref="ExtrasRules.CanonicalMediaPath"/>).
/// </summary>
internal static class ExtrasLint
{
    public static void Check(
        List<NodeViewModel> nodeList,
        string assetsPath,
        ProjectValidationService.DiskIndex disk,
        List<ProjectValidationIssue> issues,
        ProjectLintOptions options)
    {
        var load = ExtrasCatalogStore.Load(assetsPath);
        int added = 0;
        bool CapReached() => added >= options.MaxIssuesPerRule;
        void Add(ProjectValidationIssue issue)
        {
            if (CapReached()) return;
            issues.Add(issue);
            added++;
        }

        // Every enabled unlock component in the story, with its node.
        var unlocks = new List<(NodeViewModel Node, UnlockComponentViewModel Component)>();
        foreach (var node in nodeList)
        foreach (var unlock in node.AllComponents.OfType<UnlockComponentViewModel>())
        {
            if (unlock.IsEnabled) unlocks.Add((node, unlock));
        }

        if (load.Model is null)
        {
            // The file exists but the engine would ignore it whole: nothing in the game unlocks.
            Add(new ProjectValidationIssue(true,
                $"Extras catalog 'json/{ExtrasRules.CatalogFileName}' is unusable ({Describe(load.Error)}); the player ignores it, so no gallery, music or achievement exists.",
                null, $"json/{ExtrasRules.CatalogFileName}"));
            foreach (var (node, unlock) in unlocks)
                Add(NotInCatalog(node, unlock, noCatalog: true));
            return;
        }

        var model = load.Model;
        var catalogIssues = model.Validate();

        // Which catalog items the engine really loads (an item with a problem is skipped).
        var brokenIndexes = new HashSet<(string Kind, int Index)>();
        foreach (var issue in catalogIssues)
        {
            if (issue.IsItemIssue && issue.Kind is not null) brokenIndexes.Add((issue.Kind, issue.Index));
            Add(new ProjectValidationIssue(true, Explain(model, issue), null, null));
        }

        var loaded = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal)
        {
            [ExtrasRules.KindCg] = LoadedIds(model, ExtrasRules.KindCg, brokenIndexes),
            [ExtrasRules.KindMusic] = LoadedIds(model, ExtrasRules.KindMusic, brokenIndexes),
            [ExtrasRules.KindAchievement] = LoadedIds(model, ExtrasRules.KindAchievement, brokenIndexes),
        };

        // Unlock components against the loaded catalog.
        var named = new Dictionary<string, HashSet<string>>(StringComparer.Ordinal)
        {
            [ExtrasRules.KindCg] = new(StringComparer.Ordinal),
            [ExtrasRules.KindMusic] = new(StringComparer.Ordinal),
            [ExtrasRules.KindAchievement] = new(StringComparer.Ordinal),
        };
        foreach (var (node, unlock) in unlocks)
        {
            if (loaded.TryGetValue(unlock.Kind, out var ids) && ids.Contains(unlock.UnlockId))
                named[unlock.Kind].Add(unlock.UnlockId);
            else
                Add(NotInCatalog(node, unlock, noCatalog: model.IsEmpty));
        }

        // Missing files: the same level as a node's missing asset (error).
        for (int i = 0; i < model.Gallery.Count; i++)
        {
            var item = model.Gallery[i];
            if (brokenIndexes.Contains((ExtrasRules.KindCg, i))) continue;
            if (CapReached()) break;
            string who = $"Extras catalog gallery '{item.Id}'";
            CheckAsset(who, "image", item.Image, disk, issues, ref added, options);
            if (item.Thumbnail.Length > 0)
                CheckAsset(who, "thumbnail", item.Thumbnail, disk, issues, ref added, options);
        }
        for (int i = 0; i < model.Music.Count; i++)
        {
            var item = model.Music[i];
            if (brokenIndexes.Contains((ExtrasRules.KindMusic, i))) continue;
            if (CapReached()) break;
            CheckAsset($"Extras catalog music '{item.Id}'", "track", item.Track, disk, issues, ref added, options);
        }

        // Items nothing can open.
        var shownImages = new HashSet<string>(StringComparer.Ordinal);
        var playedTracks = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in nodeList)
        foreach (var component in node.AllComponents)
        {
            if (!component.IsEnabled) continue;
            switch (component)
            {
                case BackgroundComponentViewModel background when !string.IsNullOrEmpty(background.Texture):
                    shownImages.Add(ExtrasRules.CanonicalMediaPath(background.Texture, "images/"));
                    break;
                case CharacterComponentViewModel character when !string.IsNullOrEmpty(character.Sprite):
                    shownImages.Add(ExtrasRules.CanonicalMediaPath(character.Sprite, "images/"));
                    break;
                case AudioComponentViewModel audio when !string.IsNullOrEmpty(audio.BgmTrack):
                    playedTracks.Add(ExtrasRules.CanonicalMediaPath(audio.BgmTrack, "audio/"));
                    break;
            }
        }

        for (int i = 0; i < model.Gallery.Count; i++)
        {
            var item = model.Gallery[i];
            if (brokenIndexes.Contains((ExtrasRules.KindCg, i))) continue;
            if (shownImages.Contains(ExtrasRules.CanonicalMediaPath(item.Image, "images/"))
                || named[ExtrasRules.KindCg].Contains(item.Id)) continue;
            Add(new ProjectValidationIssue(false,
                $"Extras catalog gallery '{item.Id}' can never be unlocked: no node shows '{item.Image}' as a background or character sprite and no unlock component names it (the player can never open it).",
                null, null));
        }
        for (int i = 0; i < model.Music.Count; i++)
        {
            var item = model.Music[i];
            if (brokenIndexes.Contains((ExtrasRules.KindMusic, i))) continue;
            if (playedTracks.Contains(ExtrasRules.CanonicalMediaPath(item.Track, "audio/"))
                || named[ExtrasRules.KindMusic].Contains(item.Id)) continue;
            Add(new ProjectValidationIssue(false,
                $"Extras catalog music '{item.Id}' can never be unlocked: no node plays '{item.Track}' as its BGM and no unlock component names it (the player can never open it).",
                null, null));
        }
        for (int i = 0; i < model.Achievements.Count; i++)
        {
            var item = model.Achievements[i];
            if (brokenIndexes.Contains((ExtrasRules.KindAchievement, i))) continue;
            if (named[ExtrasRules.KindAchievement].Contains(item.Id)) continue;
            Add(new ProjectValidationIssue(false,
                $"Extras catalog achievement '{item.Id}' can never be unlocked: no unlock component names it (the player can never open it).",
                null, null));
        }
    }

    /// <summary>
    /// Catalog image / thumbnail / track paths, so the unused-asset rule does not call a gallery
    /// picture "not referenced by any node" when only the catalog (or an unlock component) uses it.
    /// Empty when there is no usable catalog.
    /// </summary>
    public static IReadOnlyList<string> CatalogAssetPaths(string assetsPath)
    {
        var model = ExtrasCatalogStore.Load(assetsPath).Model;
        if (model is null) return Array.Empty<string>();
        var paths = new List<string>();
        foreach (var item in model.Gallery)
        {
            if (item.Image.Length > 0) paths.Add(item.Image);
            if (item.Thumbnail.Length > 0) paths.Add(item.Thumbnail);
        }
        foreach (var item in model.Music)
        {
            if (item.Track.Length > 0) paths.Add(item.Track);
        }
        return paths;
    }

    private static HashSet<string> LoadedIds(ExtrasCatalogModel model, string kind, HashSet<(string Kind, int Index)> broken)
    {
        var ids = new HashSet<string>(StringComparer.Ordinal);
        var items = model.ItemsOf(kind);
        for (int i = 0; i < items.Count; i++)
        {
            if (!broken.Contains((kind, i))) ids.Add(items[i].Id);
        }
        return ids;
    }

    private static void CheckAsset(
        string who, string field, string asset,
        ProjectValidationService.DiskIndex disk, List<ProjectValidationIssue> issues,
        ref int added, ProjectLintOptions options)
    {
        if (added >= options.MaxIssuesPerRule) return;
        int before = issues.Count;
        ProjectValidationService.ValidateAssetReference(who, null, field, asset, disk, issues);
        added += issues.Count - before;
    }

    private static ProjectValidationIssue NotInCatalog(NodeViewModel node, UnlockComponentViewModel unlock, bool noCatalog)
    {
        string where = $"Node #{node.Id} unlock component";
        if (!ExtrasRules.IsKnownKind(unlock.Kind))
            return new(true,
                $"{where} has the unknown kind '{unlock.Kind}' (use cg, music or achievement); nothing is unlocked.",
                node.Id);
        if (unlock.UnlockId.Length == 0)
            return new(true, $"{where} ({unlock.Kind}) has no id; nothing is unlocked.", node.Id);
        return new(true,
            noCatalog
                ? $"{where} names {unlock.Kind} '{unlock.UnlockId}' but the project has no usable extras catalog; nothing is unlocked."
                : $"{where} names {unlock.Kind} '{unlock.UnlockId}' which is not in the extras catalog; nothing is unlocked.",
            node.Id);
    }

    private static string Describe(string? error) => error switch
    {
        "FileTooLarge" => "the file is larger than 256 KiB",
        "NotJson" => "it is not valid JSON",
        "NotAnObject" => "it is not a JSON object",
        "NoFormat1" => "it does not declare \"format\": 1",
        "Unreadable" => "it cannot be read",
        _ => "unknown reason",
    };

    /// <summary>One catalog problem as a sentence (English, like every lint message).</summary>
    internal static string Explain(ExtrasCatalogModel model, ExtrasIssue issue)
    {
        string list = issue.Kind switch
        {
            ExtrasRules.KindCg => "gallery",
            ExtrasRules.KindMusic => "music",
            ExtrasRules.KindAchievement => "achievements",
            _ => "catalog",
        };
        string name = "Extras catalog";
        if (issue.IsItemIssue && issue.Kind is not null)
        {
            var items = model.ItemsOf(issue.Kind);
            string id = issue.Index < items.Count ? items[issue.Index].Id : string.Empty;
            name = $"Extras catalog {list}[{issue.Index}]" + (id.Length > 0 ? $" '{id}'" : string.Empty);
        }
        else if (issue.Kind is not null)
        {
            name = $"Extras catalog '{list}'";
        }
        string field = issue.Field.Length > 0 ? $" '{issue.Field}'" : string.Empty;
        return issue.Code switch
        {
            ExtrasIssueCode.InvalidId => $"{name} has a missing or invalid id (use [a-z0-9_.-], 1-64 characters); the player skips it.",
            ExtrasIssueCode.DuplicateId => $"{name} repeats an earlier id in the same list; the player skips it.",
            ExtrasIssueCode.MissingImage => $"{name} has no image; the player skips it.",
            ExtrasIssueCode.MissingTrack => $"{name} has no track; the player skips it.",
            ExtrasIssueCode.UnsafePath => $"{name} has an unusable{field} path (absolute, '..', or over 260 bytes); the player skips it.",
            ExtrasIssueCode.TextTooLong => $"{name} has{field} text over 512 bytes (or a language tag over 16 bytes); the player skips it.",
            ExtrasIssueCode.WrongType => $"{name} has a wrong value type in{field}; the player skips it.",
            ExtrasIssueCode.TooManyItems => $"{name} holds more than {ExtrasRules.MaxItemsPerList} items; the player skips the rest.",
            ExtrasIssueCode.FileTooLarge => $"Extras catalog is larger than {ExtrasRules.MaxFileBytes / 1024} KiB; the player ignores the whole file.",
            _ => $"{name} has a problem.",
        };
    }
}
