using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Localization;
using RowlEngine.Editor.ViewModels.Localization;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 3 Dilim 4 — Translation Desk: inventory scan, Missing/Changed/
/// Translated mapping, filters + search, pseudo-locale, CSV/JSON exchange
/// (roundtrip + fail-closed import) and headless ViewModel flow.
/// Everything runs on temp project directories; no native library needed.
/// </summary>
public sealed class EditorLocalizationSlice4Tests
{
    private const string IdA = "e3421d4a-c61f-5f2b-8dd0-1984d5c4e911";
    private const string IdB = "35dce5af-9522-58ef-a7af-7306f0fb7dbd";

    // ── Temp project scaffolding ─────────────────────────────────────

    private static string CreateTempProject(
        string? graphJson = null,
        string? manifestJson = null,
        Dictionary<string, string>? locales = null)
    {
        string root = Path.Combine(
            Path.GetTempPath(), "rowl-loc4-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "Assets", "json"));
        Directory.CreateDirectory(Path.Combine(root, "Assets", "locales"));
        File.WriteAllText(
            Path.Combine(root, "Assets", "json", "full_story_graph.json"),
            graphJson ?? DefaultGraph());
        File.WriteAllText(
            Path.Combine(root, "project.rowlproj"),
            manifestJson ?? "{\"default_locale\": \"en\", \"supported_locales\": [\"en\", \"tr\"]}");
        if (locales is not null)
        {
            foreach (var pair in locales)
                File.WriteAllText(
                    Path.Combine(root, "Assets", "locales", pair.Key), pair.Value);
        }
        return root;
    }

    private static void DeleteTempProject(string root)
    {
        try { Directory.Delete(root, recursive: true); } catch (Exception) { }
    }

    private static string DialogueComponent(
        string contentId, string speaker, string dialogue) =>
        "{\"type\": \"dialogue\", \"id\": \"c1\", \"enabled\": true, \"data\": {" +
        $"\"content_id\": \"{contentId}\", \"speaker\": \"{speaker}\", " +
        $"\"dialogue\": \"{dialogue}\"}}}}";

    private static string DefaultGraph() =>
        "{\"nodes\": [{" +
        "\"id\": 101, \"title\": \"Intro\", \"objects\": [{" +
        "\"id\": \"o1\", \"name\": \"frame\", \"is_active\": true, \"components\": [" +
        DialogueComponent(IdA, "Margot", "The relay crackles awake.") + "," +
        DialogueComponent(IdB, "Voice", "You pick up the receiver.") +
        "]}]}]}";

    private static string CatalogJson(
        string locale, string entriesJson) =>
        "{\"schema_version\": 1, \"locale\": \"" + locale +
        "\", \"entries\": {" + entriesJson + "}}";

    private static string CatalogEntry(
        string speaker, string text, string alt, string? sourceHash) =>
        $"\"speaker\": \"{speaker}\", \"text\": \"{text}\", \"alt_text\": \"{alt}\"" +
        (sourceHash is null ? string.Empty : $", \"source_hash\": \"{sourceHash}\"");

    // ── Inventory ────────────────────────────────────────────────────

    [Fact]
    public void Inventory_CollectsDialoguesWithNodeContext()
    {
        using var document = JsonDocument.Parse(DefaultGraph());
        IReadOnlyList<SourceDialogue> inventory =
            TranslationInventoryService.ExtractFromDocument(document, out string? error);
        Assert.Null(error);
        Assert.Equal(2, inventory.Count);
        SourceDialogue first = inventory.First(d => d.ContentId == IdA);
        Assert.Equal("Margot", first.Speaker);
        Assert.Equal("The relay crackles awake.", first.SourceText);
        Assert.Equal(101UL, first.NodeId);
        Assert.Equal("Intro", first.NodeTitle);
    }

    [Fact]
    public void Inventory_SkipsNonDialogueAndInvalidIds()
    {
        string graph = "{\"nodes\": [{\"id\": 7, \"title\": \"T\", \"objects\": [{" +
            "\"id\": \"o\", \"name\": \"f\", \"is_active\": true, \"components\": [" +
            "{\"type\": \"background\", \"id\": \"b\", \"enabled\": true, " +
            "\"data\": {\"texture\": \"x.png\"}}," +
            DialogueComponent("not-a-uuid", "Ghost", "Nowhere.") + "," +
            "{\"type\": \"dialogue\", \"id\": \"c9\", \"enabled\": true, " +
            "\"data\": {\"speaker\": \"NoId\", \"dialogue\": \"Missing id.\"}}," +
            DialogueComponent(IdA, "Margot", "Kept.") +
            "]}]}]}";
        using var document = JsonDocument.Parse(graph);
        IReadOnlyList<SourceDialogue> inventory =
            TranslationInventoryService.ExtractFromDocument(document, out _);
        Assert.Single(inventory);
        Assert.Equal(IdA, inventory[0].ContentId);
    }

    [Fact]
    public void Inventory_LastDuplicateWins()
    {
        string graph = "{\"nodes\": [{\"id\": 1, \"title\": \"A\", \"objects\": [{" +
            "\"id\": \"o\", \"name\": \"f\", \"is_active\": true, \"components\": [" +
            DialogueComponent(IdA, "Margot", "First.") +
            "]}]}, {\"id\": 2, \"title\": \"B\", \"objects\": [{" +
            "\"id\": \"o\", \"name\": \"f\", \"is_active\": true, \"components\": [" +
            DialogueComponent(IdA, "Margot", "Second.") +
            "]}]}]}";
        using var document = JsonDocument.Parse(graph);
        IReadOnlyList<SourceDialogue> inventory =
            TranslationInventoryService.ExtractFromDocument(document, out _);
        Assert.Single(inventory);
        Assert.Equal("Second.", inventory[0].SourceText);
        Assert.Equal(2UL, inventory[0].NodeId);
    }

    [Fact]
    public void Inventory_MissingGraphFailsClosed()
    {
        string root = Path.Combine(Path.GetTempPath(), "rowl-loc4-missing-" + Guid.NewGuid().ToString("N"));
        try
        {
            Directory.CreateDirectory(root);
            IReadOnlyList<SourceDialogue> inventory =
                TranslationInventoryService.BuildInventory(root, out string? error);
            Assert.Empty(inventory);
            Assert.NotNull(error);
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    [Fact]
    public void SourceHash_IsStableHexAndChangesWithSource()
    {
        string hash = TranslationInventoryService.ComputeSourceHash("Margot", "Hello.");
        Assert.Equal(64, hash.Length);
        Assert.True(hash.All(c => "0123456789abcdef".Contains(c)));
        Assert.Equal(hash, TranslationInventoryService.ComputeSourceHash("Margot", "Hello."));
        Assert.NotEqual(hash, TranslationInventoryService.ComputeSourceHash("Margot", "Hello!"));
        Assert.NotEqual(hash, TranslationInventoryService.ComputeSourceHash("Voice", "Hello."));
    }

    // ── Status mapping ───────────────────────────────────────────────

    [Fact]
    public void Status_MissingCatalogYieldsAllMissing()
    {
        string root = CreateTempProject();
        try
        {
            var inventory = TranslationInventoryService.BuildInventory(root, out _);
            List<TranslationRow> rows = TranslationStatusService.BuildRows(
                inventory, root, "tr", out string? error);
            Assert.Null(error);
            Assert.Equal(2, rows.Count);
            Assert.All(rows, r => Assert.Equal(TranslationState.Missing, r.State));
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    [Fact]
    public void Status_MapsTranslatedChangedLegacyAndEmpty()
    {
        string hashA = TranslationInventoryService.ComputeSourceHash("Margot", "The relay crackles awake.");
        string staleHash = TranslationInventoryService.ComputeSourceHash("Voice", "stale source");
        string catalog = CatalogJson("tr",
            $"\"{IdA}\": {{{CatalogEntry("Margot", "Röle uyandı.", "alt", hashA)}}}," +
            $"\"{IdB}\": {{{CatalogEntry("Voice", "Alıcıyı kaldırdın.", "", staleHash)}}}," +
            "\"aaaaaaaa-1111-2222-3333-444444444444\": " +
            $"{{{CatalogEntry("Ghost", "Hayalet.", "", null)}}}");
        string root = CreateTempProject(locales: new Dictionary<string, string> { ["tr.json"] = catalog });
        try
        {
            var inventory = TranslationInventoryService.BuildInventory(root, out _);
            List<TranslationRow> rows = TranslationStatusService.BuildRows(
                inventory, root, "tr", out _);
            TranslationRow rowA = rows.First(r => r.ContentId == IdA);
            TranslationRow rowB = rows.First(r => r.ContentId == IdB);
            Assert.Equal(TranslationState.Translated, rowA.State);
            Assert.False(rowA.LegacyNoHash);
            Assert.Equal(TranslationState.Changed, rowB.State);

            // Legacy entry without source_hash counts as Translated, flagged.
            string legacy = CatalogJson("tr",
                $"\"{IdA}\": {{{CatalogEntry("Margot", "Röle uyandı.", "alt", null)}}}");
            File.WriteAllText(Path.Combine(root, "Assets", "locales", "tr.json"), legacy);
            List<TranslationRow> legacyRows = TranslationStatusService.BuildRows(
                inventory, root, "tr", out _);
            TranslationRow legacyA = legacyRows.First(r => r.ContentId == IdA);
            Assert.Equal(TranslationState.Translated, legacyA.State);
            Assert.True(legacyA.LegacyNoHash);
            TranslationRow legacyB = legacyRows.First(r => r.ContentId == IdB);
            Assert.Equal(TranslationState.Missing, legacyB.State);

            // Present-but-empty text is Missing, not Translated.
            string empty = CatalogJson("tr",
                $"\"{IdA}\": {{{CatalogEntry("Margot", "", "", hashA)}}}");
            File.WriteAllText(Path.Combine(root, "Assets", "locales", "tr.json"), empty);
            List<TranslationRow> emptyRows = TranslationStatusService.BuildRows(
                inventory, root, "tr", out _);
            Assert.Equal(TranslationState.Missing,
                emptyRows.First(r => r.ContentId == IdA).State);
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    [Fact]
    public void Filter_SearchAndCounts()
    {
        var rows = new List<TranslationRow>
        {
            new() { ContentId = IdA, Speaker = "Margot", SourceText = "The relay crackles.", TranslatedText = "Röle.", State = TranslationState.Translated },
            new() { ContentId = IdB, Speaker = "Voice", SourceText = "Pick up.", TranslatedText = "", State = TranslationState.Missing },
        };
        Assert.Equal(2, TranslationStatusService.ApplyFilter(rows, TranslationFilter.All, null).Count);
        Assert.Single(TranslationStatusService.ApplyFilter(rows, TranslationFilter.MissingOnly, null));
        Assert.Empty(TranslationStatusService.ApplyFilter(rows, TranslationFilter.ChangedOnly, null));
        Assert.Single(TranslationStatusService.ApplyFilter(rows, TranslationFilter.All, "margot"));
        Assert.Single(TranslationStatusService.ApplyFilter(rows, TranslationFilter.All, "relay"));
        Assert.Single(TranslationStatusService.ApplyFilter(rows, TranslationFilter.All, IdB[..8]));
        Assert.Empty(TranslationStatusService.ApplyFilter(rows, TranslationFilter.All, "zzz-no-match"));
        var (translated, missing, changed) = TranslationStatusService.CountStates(rows);
        Assert.Equal((1, 1, 0), (translated, missing, changed));
    }

    // ── Atomic save ──────────────────────────────────────────────────

    [Fact]
    public void SaveCatalog_RoundtripMarksTranslatedAndPreservesOrphans()
    {
        string orphanId = "bbbbbbbb-1111-2222-3333-444444444444";
        string catalog = CatalogJson("tr",
            $"\"{orphanId}\": {{{CatalogEntry("Ghost", "Eski hayalet.", "", null)}}}");
        string root = CreateTempProject(locales: new Dictionary<string, string> { ["tr.json"] = catalog });
        try
        {
            var inventory = TranslationInventoryService.BuildInventory(root, out _);
            List<TranslationRow> rows = TranslationStatusService.BuildRows(
                inventory, root, "tr", out _);
            foreach (TranslationRow row in rows)
                row.TranslatedText = "Ç: " + row.SourceText;
            Assert.Null(TranslationStatusService.SaveCatalog(root, "tr", rows));

            string saved = File.ReadAllText(Path.Combine(root, "Assets", "locales", "tr.json"));
            Assert.Null(LocalizationService.ValidateCatalog(saved, "tr", out int count));
            Assert.Equal(3, count); // 2 inventory rows + 1 preserved orphan

            List<TranslationRow> reloaded = TranslationStatusService.BuildRows(
                inventory, root, "tr", out _);
            Assert.All(reloaded, r => Assert.Equal(TranslationState.Translated, r.State));
            Assert.All(reloaded, r => Assert.False(r.LegacyNoHash));
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    [Fact]
    public void SaveCatalog_InvalidLocaleWritesNothing()
    {
        string root = CreateTempProject();
        try
        {
            var inventory = TranslationInventoryService.BuildInventory(root, out _);
            List<TranslationRow> rows = TranslationStatusService.BuildRows(
                inventory, root, "tr", out _);
            Assert.NotNull(TranslationStatusService.SaveCatalog(root, "!!!", rows));
            Assert.False(File.Exists(Path.Combine(root, "Assets", "locales", "!!!.json")));
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    // ── Pseudo-locale ────────────────────────────────────────────────

    [Fact]
    public void Pseudo_MatchesContractExample()
    {
        Assert.Equal("[!!! Ƥľȧẏ Ɠȧṁē !!!]", PseudoLocaleGenerator.Generate("Play Game"));
    }

    [Fact]
    public void Pseudo_PreservesTagsAndIsDeterministic()
    {
        const string source = "A<pause=0.5><b>fi</b> end 123!";
        string once = PseudoLocaleGenerator.Generate(source);
        Assert.Equal(once, PseudoLocaleGenerator.Generate(source));
        Assert.StartsWith(PseudoLocaleGenerator.Prefix, once);
        Assert.EndsWith(PseudoLocaleGenerator.Suffix, once);
        Assert.Contains("<pause=0.5>", once);
        Assert.Contains("<b>", once);
        Assert.DoesNotContain("pause", once.Replace("<pause=0.5>", string.Empty));
        Assert.Equal("[!!!  !!!]", PseudoLocaleGenerator.Generate(string.Empty));
        Assert.True(once.Length > source.Length);
    }

    [Fact]
    public void Pseudo_CatalogValidatesAndReportsTranslated()
    {
        string root = CreateTempProject();
        try
        {
            var inventory = TranslationInventoryService.BuildInventory(root, out _);
            string json = PseudoLocaleGenerator.GenerateCatalog(inventory);
            Assert.Null(LocalizationService.ValidateCatalog(json, "qps-ploc", out int count));
            Assert.Equal(2, count);
            File.WriteAllText(
                Path.Combine(root, "Assets", "locales", PseudoLocaleGenerator.PseudoFileName), json);
            List<TranslationRow> rows = TranslationStatusService.BuildRows(
                inventory, root, "qps-ploc", out _);
            Assert.All(rows, r => Assert.Equal(TranslationState.Translated, r.State));
            Assert.Contains("[!!!", rows[0].TranslatedText);
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    // ── CSV exchange ─────────────────────────────────────────────────

    private static List<TranslationRow> SampleRows() => new()
    {
        new TranslationRow
        {
            ContentId = IdA, Speaker = "Margot, the \"operator\"",
            SourceSpeaker = "Margot, the \"operator\"",
            SourceText = "Line one,\nline two.", TranslatedText = "Satır bir,",
            AltText = "alt", State = TranslationState.Translated,
        },
        new TranslationRow
        {
            ContentId = IdB, Speaker = "Voice", SourceSpeaker = "Voice",
            SourceText = "Pick up.", TranslatedText = "", AltText = "",
            State = TranslationState.Missing,
        },
    };

    [Fact]
    public void Csv_RoundtripPreservesQuotingAndCrlf()
    {
        List<TranslationRow> rows = SampleRows();
        string csv = TranslationExchangeService.ExportCsv(rows);
        Assert.StartsWith("content_id,speaker,source_text,translated_text,alt_text\r\n", csv);
        Assert.Contains("\"Margot, the \"\"operator\"\"\"", csv);
        Assert.Contains("\"Line one,\nline two.\"", csv);

        var fresh = new List<TranslationRow>
        {
            new() { ContentId = IdA, Speaker = "?", SourceSpeaker = "?", SourceText = "?", State = TranslationState.Missing },
            new() { ContentId = IdB, Speaker = "Voice", SourceSpeaker = "Voice", SourceText = "Pick up.", State = TranslationState.Missing },
        };
        ImportResult result = TranslationExchangeService.TryImportCsv(csv, fresh);
        Assert.True(result.Accepted);
        Assert.Equal(2, result.UpdatedCount);
        Assert.Equal(0, result.SkippedUnknownCount);
        TranslationRow rowA = fresh.First(r => r.ContentId == IdA);
        Assert.Equal("Margot, the \"operator\"", rowA.Speaker);
        Assert.Equal("Satır bir,", rowA.TranslatedText);
        Assert.Equal(TranslationState.Translated, rowA.State);
    }

    [Fact]
    public void Csv_ImportRejectsMissingHeaderColumnAtomically()
    {
        List<TranslationRow> rows = SampleRows();
        rows[0].TranslatedText = "PRE-EXISTING";
        string bad = "content_id,speaker,source_text,translated_text\r\n" +
            $"{IdA},Margot,src,New text\r\n";
        ImportResult result = TranslationExchangeService.TryImportCsv(bad, rows);
        Assert.False(result.Accepted);
        Assert.NotNull(result.Error);
        Assert.Equal("PRE-EXISTING", rows[0].TranslatedText);
    }

    [Fact]
    public void Csv_ImportRejectsUnbalancedQuotesAtomically()
    {
        List<TranslationRow> rows = SampleRows();
        string bad = "content_id,speaker,source_text,translated_text,alt_text\r\n" +
            $"{IdA},\"unterminated,Margot,src,New,alt\r\n";
        ImportResult result = TranslationExchangeService.TryImportCsv(bad, rows);
        Assert.False(result.Accepted);
        Assert.Equal("Satır bir,", rows[0].TranslatedText);
    }

    [Fact]
    public void Csv_ImportRejectsBadContentIdAndShortRow()
    {
        List<TranslationRow> rows = SampleRows();
        ImportResult badId = TranslationExchangeService.TryImportCsv(
            "content_id,speaker,source_text,translated_text,alt_text\r\n" +
            "nope,Margot,src,New,alt\r\n", rows);
        Assert.False(badId.Accepted);

        ImportResult shortRow = TranslationExchangeService.TryImportCsv(
            "content_id,speaker,source_text,translated_text,alt_text\r\n" +
            $"{IdA},Margot,src\r\n", rows);
        Assert.False(shortRow.Accepted);
        Assert.Equal("Satır bir,", rows[0].TranslatedText);
    }

    [Fact]
    public void Csv_ImportSkipsUnknownKeysAndMarksEmptyMissing()
    {
        List<TranslationRow> rows = SampleRows();
        string unknownId = "cccccccc-1111-2222-3333-444444444444";
        string csv = "content_id,speaker,source_text,translated_text,alt_text\r\n" +
            $"{IdA},Margot,src,Yeni metin,yeni alt\r\n" +
            $"{unknownId},Ghost,src,Boo,alt\r\n" +
            $"{IdB},Voice,Pick up.,,alt2\r\n";
        ImportResult result = TranslationExchangeService.TryImportCsv(csv, rows);
        Assert.True(result.Accepted);
        Assert.Equal(2, result.UpdatedCount);
        Assert.Equal(1, result.SkippedUnknownCount);
        Assert.Equal(new[] { unknownId }, result.SkippedUnknownIds);
        Assert.Equal("Yeni metin", rows[0].TranslatedText);
        Assert.Equal(TranslationState.Missing, rows[1].State);
    }

    [Fact]
    public void JsonExport_ValidatesAgainstCatalogContract()
    {
        string json = TranslationExchangeService.ExportCatalogJson("tr", SampleRows());
        Assert.Null(LocalizationService.ValidateCatalog(json, "tr", out int count));
        Assert.Equal(2, count);
    }

    // ── Headless ViewModel flow ──────────────────────────────────────

    [Fact]
    public void DeskViewModel_RefreshFilterSaveImportPseudoEndToEnd()
    {
        string root = CreateTempProject();
        try
        {
            var desk = new LocalizationDeskViewModel(root);
            Assert.Equal(2, desk.FilteredRows.Count);
            Assert.Contains("2 satır", desk.StatusLine);

            desk.SelectedFilter = TranslationFilter.MissingOnly;
            Assert.Equal(2, desk.FilteredRows.Count);
            desk.SearchText = "margot";
            Assert.Single(desk.FilteredRows);

            desk.SearchText = string.Empty;
            desk.SelectedFilter = TranslationFilter.All;
            desk.SelectedRow = desk.FilteredRows.First(r => r.ContentId == IdA);
            Assert.Equal("Margot", desk.EditSpeaker);
            desk.EditTranslation = "Röle cızırtıyla uyanıyor.";
            desk.EditAltText = "Operatör bekliyor.";
            desk.SaveCommand.Execute(null);
            Assert.Contains("kaydedildi", desk.StatusLine);

            var inventory = TranslationInventoryService.BuildInventory(root, out _);
            List<TranslationRow> reloaded = TranslationStatusService.BuildRows(
                inventory, root, desk.TargetLocale, out _);
            Assert.Equal(TranslationState.Translated,
                reloaded.First(r => r.ContentId == IdA).State);

            string reply = desk.ImportCsvText(
                "content_id,speaker,source_text,translated_text,alt_text\r\n" +
                $"{IdB},Voice,Pick up.,Alıcıyı kaldırdın.,\r\n");
            Assert.StartsWith("✅", reply);
            desk.SaveCommand.Execute(null);

            desk.GeneratePseudoCatalogCommand.Execute(null);
            Assert.True(File.Exists(Path.Combine(
                root, "Assets", "locales", PseudoLocaleGenerator.PseudoFileName)));
            Assert.Contains("pseudo katalog", desk.StatusLine);
        }
        finally
        {
            DeleteTempProject(root);
        }
    }

    [Fact]
    public void DeskViewModel_MissingProjectFailsClosed()
    {
        var desk = new LocalizationDeskViewModel(
            Path.Combine(Path.GetTempPath(), "rowl-loc4-absent-" + Guid.NewGuid().ToString("N")));
        Assert.Empty(desk.FilteredRows);
        Assert.Contains("0 satır", desk.StatusLine);
    }
}
