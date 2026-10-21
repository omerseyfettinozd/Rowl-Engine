using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Localization;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C01 BUG-1 — editörde düzenlenen diyalog satırı, varsayılan-dil kataloğunun
/// gölgesinde kalmamalı (önizleme + paketli oyun aynı katalogları okur).
/// Diğer dillerdeki çeviri korunur ama "Değişmiş" olarak işaretlenir.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorSourceCatalogSyncTests : IDisposable
{
    private const string Id = "e3421d4a-c61f-5f2b-8dd0-1984d5c4e911";
    private const string OldText = "The relay crackles awake.";
    private const string NewText = "Şişli'de ığdır";

    private readonly string _root =
        Path.Combine(Path.GetTempPath(), "RowlSrcSync_" + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        try { Directory.Delete(_root, true); } catch (Exception) { }
    }

    private string JsonDir => Path.Combine(_root, "Assets", "json");
    private string Locale(string code) => Path.Combine(_root, "Assets", "locales", code + ".json");

    private void Scaffold()
    {
        Directory.CreateDirectory(JsonDir);
        Directory.CreateDirectory(Path.Combine(_root, "Assets", "locales"));
        File.WriteAllText(Path.Combine(_root, "project.rowlproj"),
            "{\"defaultLocale\":\"en\",\"supportedLocales\":[\"en\",\"tr\"]}");
        File.WriteAllText(Locale("en"),
            "{\"schema_version\":1,\"locale\":\"en\",\"entries\":{\"" + Id +
            "\":{\"speaker\":\"Margot\",\"text\":\"" + OldText + "\",\"alt_text\":\"alt\"}}}");
        File.WriteAllText(Locale("tr"),
            "{\"schema_version\":1,\"locale\":\"tr\",\"entries\":{\"" + Id +
            "\":{\"speaker\":\"Margot\",\"text\":\"Röle uyanıyor.\",\"alt_text\":\"\"}}}");
        WriteGraph(OldText);
    }

    private static StoryGraphSaveSnapshot Snapshot(string text)
    {
        var data = new Dictionary<string, object>
        {
            ["content_id"] = Id,
            ["speaker"] = "Margot",
            ["dialogue"] = text,
        };
        var node = new StoryGraphSaveSnapshot.SnapshotNode
        {
            Id = 1,
            Title = "n",
            Speaker = "Margot",
            DialogueText = text,
            Objects =
            {
                new StoryGraphSaveSnapshot.SnapshotObject
                {
                    Id = "o1", Name = "frame", IsActive = true,
                    Components = { new StoryGraphSaveSnapshot.SnapshotComponent
                        { Type = "dialogue", Id = "c1", Enabled = true, Data = data } },
                },
            },
        };
        return new StoryGraphSaveSnapshot { StartNodeId = 1, Nodes = { node }, ActiveNode = node };
    }

    private void WriteGraph(string text)
    {
        Assert.True(StoryGraphSaveService.TryWriteSnapshot(Snapshot(text), JsonDir, 1, () => 1));
    }

    private static string CatalogText(string path)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(path));
        return doc.RootElement.GetProperty("entries").GetProperty(Id).GetProperty("text").GetString()!;
    }

    [Fact]
    public void Save_EditedDialogue_UpdatesDefaultLocaleCatalog()
    {
        Scaffold();
        WriteGraph(NewText);
        Assert.Equal(NewText, CatalogText(Locale("en")));
    }

    [Fact]
    public void Save_EditedDialogue_KeepsTranslationButFlagsItChanged()
    {
        Scaffold();
        WriteGraph(NewText);
        Assert.Equal("Röle uyanıyor.", CatalogText(Locale("tr")));
        var inventory = TranslationInventoryService.BuildInventory(_root, out _);
        var rows = TranslationStatusService.BuildRows(inventory, _root, "tr", out _);
        Assert.Equal(TranslationState.Changed, Assert.Single(rows).State);
    }

    [Fact]
    public void Save_UnchangedDialogue_DoesNotRewriteCatalogs()
    {
        Scaffold();
        string before = File.ReadAllText(Locale("tr"));
        WriteGraph(OldText);
        Assert.Equal(before, File.ReadAllText(Locale("tr")));
    }

    [Fact]
    public void Save_PreservesPluralTableEntriesInOtherRows()
    {
        Scaffold();
        File.WriteAllText(Locale("en"),
            "{\"schema_version\":1,\"locale\":\"en\",\"entries\":{\"" + Id +
            "\":{\"speaker\":\"Margot\",\"text\":\"" + OldText + "\",\"alt_text\":\"\"}," +
            "\"coins\":{\"speaker\":\"\",\"text\":{\"one\":\"{n} coin\",\"other\":\"{n} coins\"},\"alt_text\":\"\"}}}");
        WriteGraph(NewText);
        using var doc = JsonDocument.Parse(File.ReadAllText(Locale("en")));
        var coins = doc.RootElement.GetProperty("entries").GetProperty("coins").GetProperty("text");
        Assert.Equal(JsonValueKind.Object, coins.ValueKind);
    }

    [Fact]
    public void Preview_AfterSaveAndReload_ShowsEditedTextInEngine()
    {
        Scaffold();
        WriteGraph(NewText);

        IntPtr handle = NativeBridge.RowlEngine_Create();
        Assert.NotEqual(IntPtr.Zero, handle);
        try
        {
            Assert.NotEqual(0, NativeBridge.RowlEngine_Init(handle, 960, 540, 0));
            NativeBridge.RowlEngine_SetProjectDirectory(handle, _root);
            NativeBridge.RowlEngine_LoadStoryGraph(handle, Path.Combine(JsonDir, "full_story_graph.json"));
            NativeBridge.RowlEngine_Step(handle, 0.0f);
            Assert.Equal(NewText, ReadDialogue(handle));

            // Çeviri dilinde çeviri görünmeye devam eder.
            Assert.Equal(NativeBridge.ResultCode.Ok, NativeBridge.RowlEngine_SetLocale(handle, "tr"));
            NativeBridge.RowlEngine_Step(handle, 0.0f);
            Assert.Equal("Röle uyanıyor.", ReadDialogue(handle));
        }
        finally
        {
            NativeBridge.RowlEngine_Shutdown(handle);
            NativeBridge.RowlEngine_Destroy(handle);
        }
    }

    private static string ReadDialogue(IntPtr handle) => NativeBridge.PtrToString(
        NativeBridge.RowlEngine_GetDialogueWithLength(handle, out uint len), len);
}
