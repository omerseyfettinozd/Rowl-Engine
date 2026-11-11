using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Avalonia.Threading;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Extras;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Tools.RowlEye;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using RowlEngine.Editor.Views.Components;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D07-3 — the editor's author surface for unlockable content: the "unlock" component, the
/// extras catalog model / store / editor view model, parity of the catalog rules with the native
/// engine, and the project-lint rules.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorD073ExtrasAuthoringTests : IDisposable
{
    private readonly List<string> _temps = new();
    private readonly Func<ExtrasCatalogModel?> _previousProvider = UnlockComponentViewModel.CatalogProvider;

    public void Dispose()
    {
        UnlockComponentViewModel.CatalogProvider = _previousProvider;
        ExtrasCatalogStore.Invalidate();
        foreach (var path in _temps)
        {
            try { Directory.Delete(path, recursive: true); } catch (Exception) { }
        }
    }

    private static string RepoRoot()
    {
        string? directory = AppContext.BaseDirectory;
        while (directory is not null)
        {
            if (Directory.Exists(Path.Combine(directory, "samples", "kayip_tablo")))
                return directory;
            directory = Directory.GetParent(directory)?.FullName;
        }
        throw new Exception("Repository root with samples/ could not be located.");
    }

    private static string SampleAssets => Path.Combine(RepoRoot(), "samples", "kayip_tablo", "Assets");

    private string TempAssets(string? catalogJson = null)
    {
        string root = Path.Combine(Path.GetTempPath(), "d073_" + Guid.NewGuid().ToString("N"));
        string assets = Path.Combine(root, "Assets");
        Directory.CreateDirectory(Path.Combine(assets, "json"));
        Directory.CreateDirectory(Path.Combine(assets, "images"));
        Directory.CreateDirectory(Path.Combine(assets, "audio"));
        _temps.Add(root);
        if (catalogJson is not null)
            File.WriteAllText(Path.Combine(assets, "json", "extras.json"), catalogJson);
        return assets;
    }

    private static ExtrasCatalogModel Model(string json)
    {
        var result = ExtrasCatalogModel.Parse(json);
        Assert.NotNull(result.Model);
        return result.Model!;
    }

    private const string SmallCatalog = """
    {
      "format": 1,
      "gallery": [ { "id": "bogaz", "title": { "tr": "Boğaz", "en": "Bosphorus" }, "image": "bogaz.png" },
                   { "id": "atolye", "image": "atolye.png" } ],
      "music": [ { "id": "nokturn", "title": "Nokturn", "track": "audio/nokturn.ogg" } ],
      "achievements": [ { "id": "son", "title": { "tr": "Son", "en": "End" }, "description": { "tr": "d", "en": "e" } } ]
    }
    """;

    // ═══ unlock component ═══════════════════════════════════════════════════

    [Fact]
    public void Unlock_SerializesTheNativeSchemaAndRoundTrips()
    {
        var source = new UnlockComponentViewModel { Kind = "achievement", UnlockId = "isik_geri_dondu" };
        var data = source.Serialize();

        Assert.Equal(new[] { "id", "kind" }, data.Keys.OrderBy(k => k).ToArray());
        Assert.Equal("achievement", data["kind"]);
        Assert.Equal("isik_geri_dondu", data["id"]);

        var restored = new UnlockComponentViewModel();
        restored.Deserialize(data.ToDictionary(p => p.Key, p => (object?)p.Value));
        Assert.Equal("achievement", restored.Kind);
        Assert.Equal("isik_geri_dondu", restored.UnlockId);
        Assert.Equal("unlock", restored.TypeKey);
    }

    [Fact]
    public void Unlock_IsRegisteredAndHydratesFromAGraph()
    {
        Assert.IsType<UnlockComponentViewModel>(ComponentRegistry.Create("unlock"));
        Assert.Contains("unlock", ComponentRegistry.AvailableTypes);

        const string graph = """
        { "format_version": 4, "start_node_id": 1, "nodes": [ { "id": 1, "title": "N", "editor_x": 0, "editor_y": 0,
          "objects": [ { "id": "o", "name": "O", "is_active": true, "components": [
            { "type": "unlock", "id": "u1", "enabled": true, "data": { "kind": "music", "id": "nokturn" } } ] } ],
          "next_nodes": [] } ] }
        """;
        using var doc = JsonDocument.Parse(graph);
        var loaded = StoryGraphLoaderService.Load(doc);
        var unlock = Assert.IsType<UnlockComponentViewModel>(loaded.Nodes.Single().Objects.Single().Components.Single());
        Assert.Equal("music", unlock.Kind);
        Assert.Equal("nokturn", unlock.UnlockId);
        Assert.Empty(loaded.Warnings);
    }

    [Fact]
    public void Unlock_IdListComesFromTheCatalogOfItsKind()
    {
        var catalog = Model(SmallCatalog);
        UnlockComponentViewModel.CatalogProvider = () => catalog;
        var unlock = new UnlockComponentViewModel { Kind = "cg" };

        Assert.True(unlock.HasCatalogIds);
        Assert.False(unlock.UsesFreeText);
        Assert.Equal(new[] { "bogaz", "atolye" }, unlock.IdOptions.Select(o => o.Id).ToArray());
        Assert.All(unlock.IdOptions, o => Assert.True(o.InCatalog));

        unlock.Kind = "music";
        Assert.Equal(new[] { "nokturn" }, unlock.IdOptions.Select(o => o.Id).ToArray());
        unlock.Kind = "achievement";
        Assert.Equal(new[] { "son" }, unlock.IdOptions.Select(o => o.Id).ToArray());

        // Picking writes the id.
        unlock.SelectedIdOption = unlock.IdOptions[0];
        Assert.Equal("son", unlock.UnlockId);
    }

    [Fact]
    public void Unlock_WithoutACatalogTheIdIsFreeTextAndSurvives()
    {
        UnlockComponentViewModel.CatalogProvider = () => null;
        var unlock = new UnlockComponentViewModel { Kind = "cg", UnlockId = "typed_by_hand" };

        Assert.True(unlock.UsesFreeText);
        Assert.False(unlock.HasCatalogIds);
        Assert.Empty(unlock.IdOptions);
        Assert.Equal("typed_by_hand", unlock.Serialize()["id"]);
        Assert.False(unlock.IdNotInCatalog); // no catalog: nothing to compare with
    }

    [Fact]
    public void Unlock_AnIdTheCatalogLacksIsKeptAndFlagged_NotErased()
    {
        var catalog = Model(SmallCatalog);
        UnlockComponentViewModel.CatalogProvider = () => catalog;
        var unlock = new UnlockComponentViewModel { Kind = "cg", UnlockId = "gone_from_catalog" };

        Assert.True(unlock.HasCatalogIds);
        Assert.True(unlock.IdNotInCatalog);
        Assert.Contains(unlock.IdOptions, o => o.Id == "gone_from_catalog" && !o.InCatalog);
        Assert.Equal("gone_from_catalog", unlock.SelectedIdOption?.Id);

        // A ComboBox reports null whenever its list is replaced: that must not erase the id.
        unlock.SelectedIdOption = null;
        Assert.Equal("gone_from_catalog", unlock.UnlockId);
        unlock.SelectedKindOption = null;
        Assert.Equal("cg", unlock.Kind);

        // Switching the kind keeps the id (the lint flags the mismatch, nothing is lost silently).
        unlock.Kind = "music";
        Assert.Equal("gone_from_catalog", unlock.UnlockId);
        Assert.True(unlock.IdNotInCatalog);
    }

    [Fact]
    public void Unlock_PickingDoesNotRebuildTheListUnderTheComboBox()
    {
        var catalog = Model(SmallCatalog);
        UnlockComponentViewModel.CatalogProvider = () => catalog;
        var unlock = new UnlockComponentViewModel { Kind = "cg" };
        var before = unlock.IdOptions;
        unlock.SelectedIdOption = before[1];
        Assert.Same(before, unlock.IdOptions);
        Assert.Equal("atolye", unlock.UnlockId);
    }

    [Fact]
    public void Unlock_NameIsAnEditorStringInBothLanguages()
    {
        var unlock = new UnlockComponentViewModel();
        Assert.False(string.IsNullOrWhiteSpace(unlock.DisplayName));
        foreach (var option in UnlockComponentViewModel.KindOptions)
        {
            Assert.NotEqual(option.Kind, option.DisplayName);
            Assert.NotEqual("Component.Unlock.Kind." + option.Kind, option.DisplayName);
        }
        foreach (string key in new[] { "Component.Unlock.Title", "Component.Unlock.Id.Label", "Extras.Window.Title", "Menu.ExtrasCatalog" })
        {
            Assert.NotEqual(key, EditorStrings.T(key));
        }
    }

    [Fact]
    public void Unlock_IsNotPushedToTheEditPreview_ButIsSavedWithTheNode()
    {
        var node = new NodeViewModel(1, "N", 0, 0, bare: true);
        node.CreateObject("Arka").AddComponent<BackgroundComponentViewModel>();
        var unlock = node.CreateObject("Unlock").AddComponent<UnlockComponentViewModel>();
        unlock.Kind = "achievement";
        unlock.UnlockId = "son";

        string preview = StoryGraphSerializer.SerializePreviewComponents(node);
        using var previewDoc = JsonDocument.Parse(preview);
        Assert.DoesNotContain(previewDoc.RootElement.EnumerateArray(),
            c => c.GetProperty("type").GetString() == "unlock");
        Assert.Contains(previewDoc.RootElement.EnumerateArray(),
            c => c.GetProperty("type").GetString() == "background");

        string saved = StoryGraphSerializer.SerializeFullStoryGraph(new[] { node }, Array.Empty<ConnectionViewModel>(), 1);
        Assert.Contains("\"unlock\"", saved);
        Assert.Contains("\"son\"", saved);
    }

    [Fact]
    public void Unlock_InspectorBuildsItsCardAndTheUnknownCard()
    {
        RowlEyeHeadlessRuntime.EnsureInitialized();
        Dispatcher.UIThread.Invoke(() =>
        {
            var selector = new ComponentTemplateSelector();
            Assert.IsType<UnlockComponentView>(selector.Build(new UnlockComponentViewModel()));
            var unknown = new UnknownComponentViewModel("weather_fx");
            Assert.IsType<UnknownComponentView>(selector.Build(unknown));
            Assert.Contains("weather_fx", unknown.DisplayName);
        });
    }

    // ═══ catalog model ═════════════════════════════════════════════════════

    [Fact]
    public void Catalog_ShippedSampleRoundTripsThroughTheStoreSemantically()
    {
        string assets = TempAssets();
        string source = File.ReadAllText(Path.Combine(SampleAssets, "json", "extras.json"));
        File.WriteAllText(ExtrasCatalogStore.PathOf(assets), source);

        var load = ExtrasCatalogStore.Load(assets);
        Assert.NotNull(load.Model);
        Assert.Equal(10, load.Model!.Gallery.Count);
        Assert.Equal(5, load.Model.Music.Count);
        Assert.Equal(4, load.Model.Achievements.Count);
        Assert.Empty(load.Model.Validate());

        Assert.Empty(ExtrasCatalogStore.Save(assets, load.Model));
        string written = File.ReadAllText(ExtrasCatalogStore.PathOf(assets));
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse(source), JsonNode.Parse(written)),
            "the saved document must mean exactly what the shipped one means");
        // Turkish text stays literal, not \u-escaped.
        Assert.Contains("Boğaz'da Bir Gündüz", written);
        Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(ExtrasCatalogStore.PathOf(assets))!, ".*.tmp"));
    }

    [Fact]
    public void Catalog_KeepsUnknownKeysAndOtherLanguagesAndPlainStrings()
    {
        const string source = """
        {
          "format": 1,
          "editor_note": { "owner": "yazar", "n": [1, 2, 3] },
          "gallery": [ { "id": "a", "title": "Düz Başlık", "image": "a.png", "licence": "CC0", "tags": ["x", "y"] } ],
          "music": [ { "id": "m", "title": { "tr": "T", "de": "Titel" }, "track": "audio/m.ogg", "bpm": 90 } ],
          "achievements": [ { "id": "k", "title": { "en": "K" }, "hidden": true, "points": 5 } ]
        }
        """;
        var model = Model(source);
        var roundTrip = JsonNode.Parse(model.ToJson());
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse(source), roundTrip), model.ToJson());

        // Editing one language keeps every other entry and unknown key.
        model.Music[0].TitleEn = "Title";
        var edited = JsonNode.Parse(model.ToJson())!;
        Assert.Equal("Titel", edited["music"]![0]!["title"]!["de"]!.GetValue<string>());
        Assert.Equal("Title", edited["music"]![0]!["title"]!["en"]!.GetValue<string>());
        Assert.Equal(90, edited["music"]![0]!["bpm"]!.GetValue<int>());
        Assert.Equal("yazar", edited["editor_note"]!["owner"]!.GetValue<string>());
        Assert.Equal("CC0", edited["gallery"]![0]!["licence"]!.GetValue<string>());
        Assert.Equal(5, edited["achievements"]![0]!["points"]!.GetValue<int>());
    }

    [Fact]
    public void Catalog_EditingAPlainTitleExpandsItWithoutChangingOtherLanguages()
    {
        var model = Model("""{ "format": 1, "music": [ { "id": "m", "title": "Nokturn", "track": "audio/m.ogg" } ] }""");
        var item = model.Music[0];
        Assert.Equal("Nokturn", item.TitleTr);
        Assert.Equal("Nokturn", item.TitleEn);

        item.TitleEn = "Nocturne";
        var title = JsonNode.Parse(model.ToJson())!["music"]![0]!["title"]!;
        Assert.Equal("Nokturn", title["tr"]!.GetValue<string>());
        Assert.Equal("Nocturne", title["en"]!.GetValue<string>());

        item.TitleTr = "";
        item.TitleEn = "";
        Assert.Null(JsonNode.Parse(model.ToJson())!["music"]![0]!["title"]);
    }

    [Fact]
    public void Catalog_EmptyCatalogWritesFormatOnly_AndAMissingFileIsNotAnError()
    {
        string assets = TempAssets();
        var load = ExtrasCatalogStore.Load(assets);
        Assert.False(load.FileExists);
        Assert.NotNull(load.Model);
        Assert.True(load.Model!.IsEmpty);
        Assert.Null(load.Error);

        Assert.Empty(ExtrasCatalogStore.Save(assets, load.Model));
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse("""{"format":1}"""), JsonNode.Parse(File.ReadAllText(ExtrasCatalogStore.PathOf(assets)))));
    }

    [Theory]
    [InlineData("not json at all", "NotJson")]
    [InlineData("[1,2]", "NotAnObject")]
    [InlineData("""{ "gallery": [] }""", "NoFormat1")]
    [InlineData("""{ "format": 2 }""", "NoFormat1")]
    [InlineData("""{ "format": "1" }""", "NoFormat1")]
    [InlineData("""{ "format": 1.0 }""", "NoFormat1")]
    public void Catalog_AFileTheEngineIgnoresIsAnErrorNotAModel(string json, string error)
    {
        var result = ExtrasCatalogModel.Parse(json);
        Assert.Null(result.Model);
        Assert.Equal(error, result.Error);
    }

    [Fact]
    public void Catalog_StoreRefusesToSaveAnInvalidCatalog_AndWritesNothing()
    {
        string assets = TempAssets();
        var model = Model("""{ "format": 1, "gallery": [ { "id": "a", "image": "a.png" } ] }""");
        model.Gallery[0].Id = "Not Valid";
        var issues = ExtrasCatalogStore.Save(assets, model);
        Assert.Contains(issues, i => i.Code == ExtrasIssueCode.InvalidId);
        Assert.False(File.Exists(ExtrasCatalogStore.PathOf(assets)));
    }

    // ── validation rules (also the native parity table) ──

    /// <summary>One document per rule; the parity test feeds each to the native engine too.</summary>
    private static IEnumerable<(string Name, string Json)> RuleDocuments()
    {
        yield return ("valid", """
            { "format": 1,
              "gallery": [ { "id": "ok1", "image": "a.png" }, { "id": "a.b-c_9", "image": "nested/dir/c.png", "thumbnail": "t.png" } ],
              "music": [ { "id": "m1", "track": "audio/a.ogg", "title": "Plain" } ],
              "achievements": [ { "id": "a1" }, { "id": "a3", "hidden": true, "description": { "tr": "d" } } ] }
            """);
        yield return ("mixed-bad-gallery", """
            { "format": 1, "gallery": [
              { "id": "ok1", "image": "a.png" },
              { "id": "Bad", "image": "a.png" },
              { "id": "", "image": "a.png" },
              { "id": "ok1", "image": "b.png" },
              { "id": "no_image" },
              { "id": "abs", "image": "/etc/x.png" },
              { "id": "dots", "image": "../x.png" },
              { "id": "dots2", "image": "a\\..\\x.png" },
              { "id": "drive", "image": "C:\\x.png" },
              { "id": "back", "image": "\\x.png" },
              { "id": "thumb_bad", "image": "a.png", "thumbnail": "../t.png" },
              { "id": "thumb_wrongtype", "image": "a.png", "thumbnail": 5 },
              { "id": "image_wrongtype", "image": 5 },
              { "id": "title_obj", "image": "a.png", "title": { "tr": "x", "en": "y" } },
              { "id": "title_bad", "image": "a.png", "title": { "tr": 5 } },
              { "id": "title_num", "image": "a.png", "title": 5 },
              { "id": "id_num", "image": "a.png" },
              { "id": 5, "image": "a.png" },
              { "id": "ok2", "image": "nested/dir/c.png" },
              5, "text", null
            ] }
            """);
        yield return ("mixed-bad-music", """
            { "format": 1, "music": [
              { "id": "m1", "track": "audio/a.ogg" }, { "id": "m1", "track": "audio/b.ogg" },
              { "id": "m2" }, { "id": "m3", "track": 5 }, { "id": "m4", "track": "audio/x.ogg", "title": "Plain" },
              { "id": "m5", "track": "" } ] }
            """);
        yield return ("mixed-bad-achievements", """
            { "format": 1, "achievements": [
              { "id": "a1" }, { "id": "a2", "hidden": "yes" }, { "id": "a3", "hidden": true, "description": { "tr": "d" } },
              { "id": "a4", "description": 7 }, { "id": "a1" }, { "id": "a5", "hidden": 1 },
              { "id": "a6", "description": { "tr": "d", "en": 3 } } ] }
            """);
        yield return ("id-length", "{ \"format\": 1, \"music\": [ { \"id\": \"" + new string('a', 64) + "\", \"track\": \"a.ogg\" }, { \"id\": \""
            + new string('b', 65) + "\", \"track\": \"a.ogg\" } ] }");
        yield return ("text-limits-ascii", "{ \"format\": 1, \"music\": [ { \"id\": \"t512\", \"track\": \"a.ogg\", \"title\": \"" + new string('x', 512)
            + "\" }, { \"id\": \"t513\", \"track\": \"a.ogg\", \"title\": \"" + new string('x', 513) + "\" } ] }");
        yield return ("text-limits-multibyte", "{ \"format\": 1, \"music\": [ { \"id\": \"s256\", \"track\": \"a.ogg\", \"title\": \"" + new string('ş', 256)
            + "\" }, { \"id\": \"s257\", \"track\": \"a.ogg\", \"title\": \"" + new string('ş', 257) + "\" } ] }");
        yield return ("language-tag-limit", "{ \"format\": 1, \"music\": [ { \"id\": \"l16\", \"track\": \"a.ogg\", \"title\": { \"" + new string('l', 16)
            + "\": \"x\" } }, { \"id\": \"l17\", \"track\": \"a.ogg\", \"title\": { \"" + new string('l', 17) + "\": \"x\" } } ] }");
        yield return ("path-limits", "{ \"format\": 1, \"gallery\": [ { \"id\": \"p260\", \"image\": \"" + new string('p', 256) + ".png\" }, { \"id\": \"p261\", \"image\": \""
            + new string('p', 257) + ".png\" } ] }");
        yield return ("path-limit-multibyte", "{ \"format\": 1, \"gallery\": [ { \"id\": \"w130\", \"image\": \"" + new string('ş', 128) + ".png\" }, { \"id\": \"w131\", \"image\": \""
            + new string('ş', 129) + ".png\" } ] }");
        var many = new StringBuilder("{ \"format\": 1, \"achievements\": [");
        for (int i = 0; i < 257; i++) many.Append(i == 0 ? "" : ",").Append("{ \"id\": \"a").Append(i).Append("\" }");
        yield return ("too-many-items", many.Append("] }").ToString());
        yield return ("unknown-keys-only", """{ "format": 1, "weird": true, "gallery": "not a list", "music": { "id": "x" } }""");
        yield return ("no-format", """{ "gallery": [ { "id": "a", "image": "a.png" } ] }""");
        yield return ("format-2", """{ "format": 2, "gallery": [ { "id": "a", "image": "a.png" } ] }""");
        yield return ("format-float", """{ "format": 1.0, "gallery": [ { "id": "a", "image": "a.png" } ] }""");
        yield return ("not-json", "{ nope");
        yield return ("top-level-array", """[ { "format": 1 } ]""");
    }

    private static (List<string> Gallery, List<string> Music, List<string> Achievements) ManagedLoadedIds(string json)
    {
        var parsed = ExtrasCatalogModel.Parse(json);
        if (parsed.Model is null) return (new(), new(), new());
        var model = parsed.Model;
        var broken = model.Validate().Where(i => i.IsItemIssue && i.Kind is not null)
            .Select(i => (i.Kind!, i.Index)).ToHashSet();
        List<string> Ids(string kind)
        {
            var items = model.ItemsOf(kind);
            var ids = new List<string>();
            for (int i = 0; i < items.Count && ids.Count < ExtrasRules.MaxItemsPerList; i++)
                if (!broken.Contains((kind, i))) ids.Add(items[i].Id);
            return ids;
        }
        return (Ids(ExtrasRules.KindCg), Ids(ExtrasRules.KindMusic), Ids(ExtrasRules.KindAchievement));
    }

    [Fact]
    public void Catalog_ValidationFlagsExactlyWhatTheRulesForbid()
    {
        var docs = RuleDocuments().ToDictionary(d => d.Name, d => d.Json);

        Assert.Empty(Model(docs["valid"]).Validate());

        var gallery = ManagedLoadedIds(docs["mixed-bad-gallery"]).Gallery;
        Assert.Equal(new[] { "ok1", "title_obj", "id_num", "ok2" }, gallery.ToArray());

        var music = ManagedLoadedIds(docs["mixed-bad-music"]).Music;
        Assert.Equal(new[] { "m1", "m4" }, music.ToArray());

        var achievements = ManagedLoadedIds(docs["mixed-bad-achievements"]).Achievements;
        Assert.Equal(new[] { "a1", "a3" }, achievements.ToArray());

        Assert.Equal(new[] { new string('a', 64) }, ManagedLoadedIds(docs["id-length"]).Music.ToArray());
        Assert.Equal(new[] { "t512" }, ManagedLoadedIds(docs["text-limits-ascii"]).Music.ToArray());
        Assert.Equal(new[] { "s256" }, ManagedLoadedIds(docs["text-limits-multibyte"]).Music.ToArray());
        Assert.Equal(new[] { "l16" }, ManagedLoadedIds(docs["language-tag-limit"]).Music.ToArray());
        Assert.Equal(new[] { "p260" }, ManagedLoadedIds(docs["path-limits"]).Gallery.ToArray());
        Assert.Equal(new[] { "w130" }, ManagedLoadedIds(docs["path-limit-multibyte"]).Gallery.ToArray());

        var many = Model(docs["too-many-items"]);
        Assert.Contains(many.Validate(), i => i.Code == ExtrasIssueCode.TooManyItems && i.Kind == ExtrasRules.KindAchievement);
        Assert.Equal(256, ManagedLoadedIds(docs["too-many-items"]).Achievements.Count);

        var codes = Model(docs["mixed-bad-gallery"]).Validate().Select(i => i.Code).ToHashSet();
        Assert.Contains(ExtrasIssueCode.InvalidId, codes);
        Assert.Contains(ExtrasIssueCode.DuplicateId, codes);
        Assert.Contains(ExtrasIssueCode.MissingImage, codes);
        Assert.Contains(ExtrasIssueCode.UnsafePath, codes);
        Assert.Contains(ExtrasIssueCode.WrongType, codes);
    }

    [Fact]
    public void Catalog_RulesAgreeWithTheNativeEngine_OnEveryDocument()
    {
        NativeEnvironment.EnsureDisplayFreeDrivers();
        using var host = new EngineHost();
        Assert.True(host.Initialize(64, 64, false), host.LastError);

        foreach (var (name, json) in RuleDocuments())
        {
            string assets = TempAssets(json);
            string projectRoot = Path.GetDirectoryName(assets)!;
            host.EndSession();
            Assert.True(host.SetProjectDirectoryChecked(projectRoot, out string detail), $"{name}: mount rejected: {detail}");

            string native = host.InvokeOnEngineThread(NativeProfileBridge.GetExtrasJson, string.Empty);
            Assert.False(string.IsNullOrEmpty(native), $"{name}: no native extras JSON");
            using var doc = JsonDocument.Parse(native);
            List<string> NativeIds(string list) => doc.RootElement.GetProperty(list).EnumerateArray()
                .Select(e => e.GetProperty("id").GetString()!).ToList();

            var managed = ManagedLoadedIds(json);
            Assert.True(managed.Gallery.SequenceEqual(NativeIds("gallery")),
                $"{name}: gallery managed [{string.Join(",", managed.Gallery.Take(8))}] native [{string.Join(",", NativeIds("gallery").Take(8))}]");
            Assert.True(managed.Music.SequenceEqual(NativeIds("music")),
                $"{name}: music managed [{string.Join(",", managed.Music.Take(8))}] native [{string.Join(",", NativeIds("music").Take(8))}]");
            Assert.True(managed.Achievements.SequenceEqual(NativeIds("achievements")),
                $"{name}: achievements managed [{string.Join(",", managed.Achievements.Take(8))}] native [{string.Join(",", NativeIds("achievements").Take(8))}]");
        }
    }

    [Fact]
    public void Catalog_ShippedSampleIsCleanForBothSides()
    {
        string json = File.ReadAllText(Path.Combine(SampleAssets, "json", "extras.json"));
        var managed = ManagedLoadedIds(json);
        Assert.Equal(10, managed.Gallery.Count);
        Assert.Equal(5, managed.Music.Count);
        Assert.Equal(4, managed.Achievements.Count);
    }

    [Theory]
    [InlineData("bogaz.jpg", "images/", "bogaz.jpg")]
    [InlineData("images/bogaz.jpg", "images/", "bogaz.jpg")]
    [InlineData("Assets/images/Bogaz.JPG", "images/", "bogaz.jpg")]
    [InlineData("./images/bogaz.jpg", "images/", "bogaz.jpg")]
    [InlineData("/images/bogaz.jpg", "images/", "bogaz.jpg")]
    [InlineData("images\\sub\\Bogaz.jpg", "images/", "sub/bogaz.jpg")]
    [InlineData("images/images/x.jpg", "images/", "images/x.jpg")]
    [InlineData("Assets/Assets/x.jpg", "images/", "assets/x.jpg")]
    [InlineData("audio/Nokturn.OGG", "audio/", "nokturn.ogg")]
    [InlineData("images/", "images/", "images/")]
    public void CanonicalMediaPath_MatchesTheEngineForm(string input, string prefix, string expected)
    {
        Assert.Equal(expected, ExtrasRules.CanonicalMediaPath(input, prefix));
    }

    // ═══ catalog editor view model ═════════════════════════════════════════

    [Fact]
    public void Editor_AddRemoveMoveAndSaveFlow()
    {
        string assets = TempAssets(SmallCatalog);
        var vm = new ExtrasCatalogEditorViewModel(assets);
        Assert.True(vm.CanEdit);
        Assert.False(vm.IsDirty);
        Assert.Equal(0, vm.IssueCount);
        Assert.Equal(2, vm.Gallery.Count);
        Assert.False(vm.SaveCommand.CanExecute(null));

        vm.SelectedTabIndex = ExtrasCatalogEditorViewModel.GalleryTab;
        vm.AddItemCommand.Execute(null);
        Assert.Equal(3, vm.Gallery.Count);
        Assert.True(vm.IsDirty);
        Assert.Equal("cg_1", vm.SelectedGallery!.Id);
        Assert.True(vm.SelectedGallery.HasIssue);              // no image yet
        Assert.False(vm.SaveCommand.CanExecute(null));
        Assert.Equal(1, vm.IssueCount);

        vm.SelectedGallery.Image = "yeni.png";
        Assert.Equal(0, vm.IssueCount);
        Assert.True(vm.SaveCommand.CanExecute(null));

        // Move up / down keep the selection.
        var added = vm.SelectedGallery;
        vm.MoveItemUpCommand.Execute(null);
        Assert.Equal(1, vm.Gallery.IndexOf(added));
        Assert.Same(added, vm.SelectedGallery);
        vm.MoveItemUpCommand.Execute(null);
        vm.MoveItemUpCommand.Execute(null);  // already first: no-op
        Assert.Equal(0, vm.Gallery.IndexOf(added));
        vm.MoveItemDownCommand.Execute(null);
        Assert.Equal(1, vm.Gallery.IndexOf(added));

        // Duplicate id blocks saving and names the item.
        vm.SelectedGallery!.Id = "bogaz";
        Assert.False(vm.SaveCommand.CanExecute(null));
        Assert.True(vm.SelectedGallery.HasIssue);
        Assert.Contains(EditorStrings.T("Extras.Issue.DuplicateId"), vm.SelectedGallery.IssueText);
        vm.SelectedGallery.Id = "yeni";

        vm.SaveCommand.Execute(null);
        Assert.False(vm.IsDirty);
        var saved = Model(File.ReadAllText(ExtrasCatalogStore.PathOf(assets)));
        Assert.Equal(new[] { "bogaz", "yeni", "atolye" }, saved.Gallery.Select(i => i.Id).ToArray());

        // Remove selects a neighbour; removing the rest leaves no selection.
        vm.RemoveItemCommand.Execute(null);
        Assert.Equal(2, vm.Gallery.Count);
        Assert.NotNull(vm.SelectedGallery);
        vm.RemoveItemCommand.Execute(null);
        vm.RemoveItemCommand.Execute(null);
        Assert.Empty(vm.Gallery);
        Assert.Null(vm.SelectedGallery);
    }

    [Fact]
    public void Editor_MusicAndAchievementTabsEditTheirOwnLists()
    {
        string assets = TempAssets(SmallCatalog);
        var vm = new ExtrasCatalogEditorViewModel(assets);

        vm.SelectedTabIndex = ExtrasCatalogEditorViewModel.MusicTab;
        vm.AddItemCommand.Execute(null);
        Assert.Equal(2, vm.Music.Count);
        Assert.Equal(2, vm.Gallery.Count);
        vm.SelectedMusic!.Track = "audio/yeni.ogg";
        vm.SelectedMusic.TitleTr = "Yeni";
        vm.SelectedMusic.TitleEn = "New";

        vm.SelectedTabIndex = ExtrasCatalogEditorViewModel.AchievementsTab;
        vm.AddItemCommand.Execute(null);
        vm.SelectedAchievement!.DescriptionTr = "Tanım";
        vm.SelectedAchievement.DescriptionEn = "Text";
        vm.SelectedAchievement.Hidden = true;
        Assert.Equal(0, vm.IssueCount);

        vm.SaveCommand.Execute(null);
        var saved = JsonNode.Parse(File.ReadAllText(ExtrasCatalogStore.PathOf(assets)))!;
        Assert.Equal("audio/yeni.ogg", saved["music"]![1]!["track"]!.GetValue<string>());
        Assert.Equal("New", saved["music"]![1]!["title"]!["en"]!.GetValue<string>());
        Assert.True(saved["achievements"]![1]!["hidden"]!.GetValue<bool>());
        Assert.Equal("Tanım", saved["achievements"]![1]!["description"]!["tr"]!.GetValue<string>());
    }

    [Fact]
    public void Editor_ACorruptFileIsNeverOverwritten()
    {
        const string corrupt = "{ \"gallery\": [ ";
        string assets = TempAssets(corrupt);
        var vm = new ExtrasCatalogEditorViewModel(assets);

        Assert.False(vm.CanEdit);
        Assert.False(string.IsNullOrWhiteSpace(vm.LoadErrorText));
        Assert.Equal(vm.LoadErrorText, vm.StatusText);
        Assert.False(vm.SaveCommand.CanExecute(null));
        vm.AddItemCommand.Execute(null);
        Assert.Empty(vm.Gallery);
        Assert.Equal(corrupt, File.ReadAllText(ExtrasCatalogStore.PathOf(assets)));
    }

    [Fact]
    public void Editor_PickedFilesAreImportedAndStoredAsProjectRelativeNames()
    {
        string assets = TempAssets(SmallCatalog);
        var vm = new ExtrasCatalogEditorViewModel(assets)
        {
            ImportImage = path => Path.GetFileName(path),
            ImportAudio = path => Path.GetFileName(path),
        };
        vm.SelectedGallery = vm.Gallery[0];
        Assert.True(vm.ApplyPickedImage("/somewhere/new_cg.jpg", thumbnail: false));
        Assert.True(vm.ApplyPickedImage("/somewhere/new_thumb.jpg", thumbnail: true));
        Assert.Equal("new_cg.jpg", vm.SelectedGallery!.Image);
        Assert.Equal("new_thumb.jpg", vm.SelectedGallery.Thumbnail);

        vm.SelectedMusic = vm.Music[0];
        Assert.True(vm.ApplyPickedTrack("/somewhere/song.ogg"));
        Assert.Equal("audio/song.ogg", vm.SelectedMusic!.Track);
        Assert.True(vm.IsDirty);
    }

    [Fact]
    public void Editor_ReloadDropsUnsavedEdits()
    {
        string assets = TempAssets(SmallCatalog);
        var vm = new ExtrasCatalogEditorViewModel(assets);
        vm.SelectedGallery!.Id = "changed";
        Assert.True(vm.IsDirty);
        vm.ReloadCommand.Execute(null);
        Assert.False(vm.IsDirty);
        Assert.Equal("bogaz", vm.Gallery[0].Id);
    }

    [Fact]
    public void Editor_SavingNotifiesOpenUnlockCardsThroughTheSharedCache()
    {
        string assets = TempAssets(SmallCatalog);
        UnlockComponentViewModel.CatalogProvider = () => ExtrasCatalogStore.ReadShared(assets);
        var unlock = new UnlockComponentViewModel { Kind = "cg" };
        Assert.Equal(2, unlock.IdOptions.Count);

        int saved = 0;
        void OnSaved() => saved++;
        ExtrasCatalogStore.Saved += OnSaved;
        try
        {
            var vm = new ExtrasCatalogEditorViewModel(assets);
            vm.SelectedTabIndex = ExtrasCatalogEditorViewModel.GalleryTab;
            vm.AddItemCommand.Execute(null);
            vm.SelectedGallery!.Image = "n.png";
            vm.SaveCommand.Execute(null);
        }
        finally
        {
            ExtrasCatalogStore.Saved -= OnSaved;
        }
        Assert.Equal(1, saved);
        unlock.RefreshCatalog();
        Assert.Equal(3, unlock.IdOptions.Count);
    }

    // ═══ project lint ═════════════════════════════════════════════════════

    private static NodeViewModel Node(ulong id, Action<NodeViewModel> build)
    {
        var node = new NodeViewModel(id, "N" + id, 0, 0, bare: true);
        build(node);
        return node;
    }

    private static T Add<T>(NodeViewModel node, string name = "O") where T : NodeComponentViewModel, new() =>
        node.CreateObject(name).AddComponent<T>();

    private static IReadOnlyList<ProjectValidationIssue> Lint(string assets, params NodeViewModel[] nodes) =>
        ProjectLintService.Lint(nodes, Array.Empty<ConnectionViewModel>(), assets, nodes[0].Id)
            .Where(i => i.Message.Contains("Extras catalog") || i.Message.Contains("unlock component"))
            .ToList();

    private static void Touch(string assets, string rel)
    {
        string full = Path.Combine(assets, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(full)!);
        File.WriteAllBytes(full, new byte[] { 1 });
    }

    private const string LintCatalog = """
    { "format": 1,
      "gallery": [ { "id": "cg_bg", "image": "bg.png" }, { "id": "cg_unl", "image": "other.png" }, { "id": "cg_never", "image": "never.png" } ],
      "music": [ { "id": "mu_bgm", "track": "audio/song.ogg" }, { "id": "mu_unl", "track": "audio/b.ogg" }, { "id": "mu_never", "track": "audio/never.ogg" } ],
      "achievements": [ { "id": "ac_unl" }, { "id": "ac_never" } ] }
    """;

    private string LintProject(string catalog = LintCatalog)
    {
        string assets = TempAssets(catalog);
        foreach (string rel in new[] { "images/bg.png", "images/other.png", "images/never.png", "audio/song.ogg", "audio/b.ogg", "audio/never.ogg" })
            Touch(assets, rel);
        return assets;
    }

    private static NodeViewModel FullStory() => Node(1, n =>
    {
        Add<BackgroundComponentViewModel>(n, "Bg").Texture = "Assets/images/BG.PNG";   // canonical form of bg.png
        Add<AudioComponentViewModel>(n, "Au").BgmTrack = "./audio/Song.ogg";            // canonical form of audio/song.ogg
        var cg = Add<UnlockComponentViewModel>(n, "U1"); cg.Kind = "cg"; cg.UnlockId = "cg_unl";
        var mu = Add<UnlockComponentViewModel>(n, "U2"); mu.Kind = "music"; mu.UnlockId = "mu_unl";
        var ac = Add<UnlockComponentViewModel>(n, "U3"); ac.Kind = "achievement"; ac.UnlockId = "ac_unl";
    });

    [Fact]
    public void Lint_CleanWhenEveryItemCanBeOpenedAndEveryUnlockIsInTheCatalog()
    {
        string assets = LintProject();
        // cg_never / mu_never / ac_never are the only unreachable ones.
        var issues = Lint(assets, FullStory());
        Assert.DoesNotContain(issues, i => i.IsError);
        var warnings = issues.Select(i => i.Message).ToList();
        Assert.Equal(3, warnings.Count);
        Assert.Contains(warnings, m => m.Contains("gallery 'cg_never'") && m.Contains("can never be unlocked"));
        Assert.Contains(warnings, m => m.Contains("music 'mu_never'") && m.Contains("can never be unlocked"));
        Assert.Contains(warnings, m => m.Contains("achievement 'ac_never'") && m.Contains("can never be unlocked"));
        Assert.DoesNotContain(warnings, m => m.Contains("cg_bg") || m.Contains("mu_bgm") || m.Contains("cg_unl") || m.Contains("mu_unl") || m.Contains("ac_unl"));
    }

    [Fact]
    public void Lint_AnUnlockNamingAnIdOutsideTheCatalogIsAnError()
    {
        string assets = LintProject();
        var story = FullStory();
        var ghost = Add<UnlockComponentViewModel>(story, "Ghost"); ghost.Kind = "achievement"; ghost.UnlockId = "ghost_id";
        var wrongKind = Add<UnlockComponentViewModel>(story, "WrongKind"); wrongKind.Kind = "cg"; wrongKind.UnlockId = "ac_unl";

        var errors = Lint(assets, story).Where(i => i.IsError).ToList();
        Assert.Equal(2, errors.Count);
        Assert.Contains(errors, e => e.Message.Contains("achievement 'ghost_id'") && e.Message.Contains("not in the extras catalog") && e.NodeId == 1);
        Assert.Contains(errors, e => e.Message.Contains("cg 'ac_unl'") && e.NodeId == 1);
    }

    [Fact]
    public void Lint_UnknownKindAndEmptyIdAreErrors()
    {
        string assets = LintProject();
        var story = FullStory();
        var badKind = Add<UnlockComponentViewModel>(story, "BadKind"); badKind.Kind = "audio"; badKind.UnlockId = "x";
        var noId = Add<UnlockComponentViewModel>(story, "NoId"); noId.Kind = "cg"; noId.UnlockId = "";

        var errors = Lint(assets, story).Where(i => i.IsError).Select(i => i.Message).ToList();
        Assert.Contains(errors, m => m.Contains("unknown kind 'audio'"));
        Assert.Contains(errors, m => m.Contains("has no id"));
    }

    [Fact]
    public void Lint_ADisabledUnlockIsIgnoredAndDoesNotOpenAnything()
    {
        string assets = LintProject();
        var story = FullStory();
        var disabledGhost = Add<UnlockComponentViewModel>(story, "Off"); disabledGhost.Kind = "cg"; disabledGhost.UnlockId = "ghost";
        disabledGhost.IsEnabled = false;
        var disabledReal = Add<UnlockComponentViewModel>(story, "OffReal"); disabledReal.Kind = "achievement"; disabledReal.UnlockId = "ac_never";
        disabledReal.IsEnabled = false;

        var issues = Lint(assets, story);
        Assert.DoesNotContain(issues, i => i.IsError);
        Assert.Contains(issues, i => i.Message.Contains("achievement 'ac_never'") && i.Message.Contains("can never be unlocked"));
    }

    [Fact]
    public void Lint_AnUnlockWithoutAUsableCatalogIsAnError_AndAnUnusableFileIsReportedOnce()
    {
        string assets = TempAssets();   // no extras.json at all
        var story = Node(1, n => { var u = Add<UnlockComponentViewModel>(n); u.Kind = "cg"; u.UnlockId = "a"; });
        var none = Lint(assets, story);
        Assert.Single(none);
        Assert.True(none[0].IsError);
        Assert.Contains("no usable extras catalog", none[0].Message);

        // No catalog and no unlock component: nothing to say.
        Assert.Empty(Lint(assets, Node(1, n => Add<BackgroundComponentViewModel>(n))));

        string broken = TempAssets("{ \"format\": 3 }");
        var issues = Lint(broken, story);
        Assert.Equal(2, issues.Count);
        Assert.Contains(issues, i => i.IsError && i.Message.Contains("unusable") && i.AssetPath == "json/extras.json");
    }

    [Fact]
    public void Lint_CatalogProblemsAreErrors_DuplicateIdsAmongThem()
    {
        string assets = TempAssets("""
            { "format": 1,
              "gallery": [ { "id": "dup", "image": "a.png" }, { "id": "dup", "image": "b.png" }, { "id": "BAD", "image": "a.png" },
                           { "id": "abs", "image": "/x.png" } ],
              "music": [ { "id": "m" } ] }
            """);
        Touch(assets, "images/a.png");
        Touch(assets, "images/b.png");
        var errors = Lint(assets, Node(1, n => Add<BackgroundComponentViewModel>(n))).Where(i => i.IsError).Select(i => i.Message).ToList();
        Assert.Contains(errors, m => m.Contains("gallery[1] 'dup'") && m.Contains("repeats an earlier id"));
        Assert.Contains(errors, m => m.Contains("gallery[2] 'BAD'") && m.Contains("invalid id"));
        Assert.Contains(errors, m => m.Contains("gallery[3] 'abs'") && m.Contains("unusable"));
        Assert.Contains(errors, m => m.Contains("music[0] 'm'") && m.Contains("no track"));
    }

    [Fact]
    public void Lint_MissingCatalogFilesUseTheNodeAssetLevel()
    {
        string assets = TempAssets("""
            { "format": 1,
              "gallery": [ { "id": "g", "image": "present.png", "thumbnail": "gone_thumb.png" }, { "id": "g2", "image": "gone.png" } ],
              "music": [ { "id": "m", "track": "audio/gone.ogg" } ] }
            """);
        Touch(assets, "images/present.png");
        var errors = Lint(assets, Node(1, n => Add<BackgroundComponentViewModel>(n))).Where(i => i.IsError).ToList();

        Assert.Contains(errors, e => e.Message.Contains("gallery 'g2'") && e.Message.Contains("missing asset 'gone.png'") && e.AssetPath == "gone.png");
        Assert.Contains(errors, e => e.Message.Contains("gallery 'g'") && e.Message.Contains("missing asset 'gone_thumb.png'"));
        Assert.Contains(errors, e => e.Message.Contains("music 'm'") && e.Message.Contains("missing asset 'audio/gone.ogg'"));
        Assert.DoesNotContain(errors, e => e.Message.Contains("'present.png'"));

        // The same level a node's missing asset gets in the batch lint (error, asset target).
        var nodeIssue = ProjectLintService.Lint(
                new[] { Node(1, n => Add<BackgroundComponentViewModel>(n).Texture = "gone.png") },
                Array.Empty<ConnectionViewModel>(), assets, 1)
            .Single(i => i.Message.Contains("missing asset 'gone.png'") && i.Message.StartsWith("Node #"));
        Assert.True(nodeIssue.IsError);
    }

    [Fact]
    public void Lint_CharacterSpritesOpenCgsAndPathFormsAreCanonical()
    {
        string assets = LintProject();
        var story = Node(1, n =>
        {
            Add<CharacterComponentViewModel>(n, "Ch").Sprite = "images\\BG.png";
            Add<AudioComponentViewModel>(n, "Au").BgmTrack = "Assets/audio/SONG.ogg";
            var cg = Add<UnlockComponentViewModel>(n); cg.Kind = "cg"; cg.UnlockId = "cg_unl";
            var mu = Add<UnlockComponentViewModel>(n); mu.Kind = "music"; mu.UnlockId = "mu_unl";
            var ac = Add<UnlockComponentViewModel>(n); ac.Kind = "achievement"; ac.UnlockId = "ac_unl";
        });
        var messages = Lint(assets, story).Select(i => i.Message).ToList();
        Assert.DoesNotContain(messages, m => m.Contains("cg_bg"));
        Assert.DoesNotContain(messages, m => m.Contains("mu_bgm"));
    }

    [Fact]
    public void Lint_ADisabledBackgroundOrAudioDoesNotOpenItsItem()
    {
        string assets = LintProject();
        var story = Node(1, n =>
        {
            Add<BackgroundComponentViewModel>(n, "Bg").Texture = "bg.png";
            n.Objects[0].Components[0].IsEnabled = false;
            Add<AudioComponentViewModel>(n, "Au").BgmTrack = "audio/song.ogg";
            n.Objects[1].Components[0].IsEnabled = false;
        });
        var messages = Lint(assets, story).Select(i => i.Message).ToList();
        Assert.Contains(messages, m => m.Contains("gallery 'cg_bg'") && m.Contains("can never be unlocked"));
        Assert.Contains(messages, m => m.Contains("music 'mu_bgm'") && m.Contains("can never be unlocked"));
    }

    [Fact]
    public void Lint_ACatalogImageOnlyTheCatalogUsesIsNotAnUnusedAsset()
    {
        string assets = LintProject();
        var all = ProjectLintService.Lint(new[] { FullStory() }, Array.Empty<ConnectionViewModel>(), assets, 1);
        Assert.DoesNotContain(all, i => i.Message.Contains("is not referenced by any node") && i.Message.Contains("never.png"));
        Assert.DoesNotContain(all, i => i.Message.Contains("is not referenced by any node") && i.Message.Contains("other.png"));
        Assert.DoesNotContain(all, i => i.Message.Contains("is not referenced by any node") && i.Message.Contains("audio/b.ogg"));

        // The rule still fires for a file nothing names.
        Touch(assets, "images/orphan.png");
        var again = ProjectLintService.Lint(new[] { FullStory() }, Array.Empty<ConnectionViewModel>(), assets, 1);
        Assert.Contains(again, i => i.Message.Contains("is not referenced by any node") && i.Message.Contains("orphan.png"));
    }

    [Fact]
    public void Lint_TheCheckCanBeSwitchedOff()
    {
        string assets = LintProject();
        var issues = ProjectLintService.Lint(new[] { FullStory() }, Array.Empty<ConnectionViewModel>(), assets, 1,
            lintOptions: new ProjectLintOptions(CheckExtras: false));
        Assert.DoesNotContain(issues, i => i.Message.Contains("Extras catalog"));
    }

    [Fact]
    public void Lint_ShippedSampleKayipTabloRaisesNoneOfTheExtrasRules()
    {
        string graphPath = Path.Combine(SampleAssets, "json", "full_story_graph.json");
        using var doc = JsonDocument.Parse(File.ReadAllText(graphPath));
        var loaded = StoryGraphLoaderService.Load(doc, ensureDefaultObjects: false);
        Assert.True(loaded.Success);
        Assert.DoesNotContain(loaded.Warnings, w => w.Contains("Unknown component"));

        var issues = ProjectLintService.Lint(loaded.Nodes, loaded.Connections, SampleAssets, 101)
            .Where(i => i.Message.Contains("Extras catalog") || i.Message.Contains("unlock component")
                        || (i.Message.Contains("not referenced by any node")
                            && (i.AssetPath?.StartsWith("images/") == true || i.AssetPath?.StartsWith("audio/") == true)))
            .ToList();
        Assert.True(issues.Count == 0, string.Join("\n", issues.Select(i => i.Message)));
        Assert.Equal(4, loaded.Nodes.SelectMany(n => n.AllComponents).OfType<UnlockComponentViewModel>().Count());
    }
}
