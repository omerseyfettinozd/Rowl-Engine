using System.Collections.Generic;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using RowlEngine.Editor.Services.NodeLibrary;

namespace RowlEngine.Editor.Tests;

public sealed class NodeLibraryTests
{
    private static string NewTempDir()
    {
        string dir = Path.Combine(Path.GetTempPath(), "RowlNodeLib_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return dir;
    }

    private static NodeTemplate BasicTemplate()
    {
        var choiceValues = new JsonObject
        {
            ["title"] = "Choose",
            ["options"] = new JsonArray
            {
                new JsonObject { ["text"] = "Go", ["target_node_id"] = 5 },
                new JsonObject { ["text"] = "Stay", ["target_node_id"] = 7 }
            }
        };
        var condValues = new JsonObject
        {
            ["expression"] = "gold >= 10",
            ["fail_target_node_id"] = "node-9",
            ["other"] = "keep"
        };
        var spriteValues = new JsonObject { ["source"] = "ada.png", ["opacity"] = 0.7 };
        return new NodeTemplate("Yol", new List<TemplateObject>
        {
            new("dialogue", true, new List<TemplateComponent>
            {
                new("choice", choiceValues),
                new("condition", condValues),
                new("sprite", spriteValues)
            })
        });
    }

    private static void AssertStatesEqual(NodeLibraryState expected, NodeLibraryState actual)
    {
        Assert.Equal(expected.Folders.Count, actual.Folders.Count);
        Assert.Equal(expected.Items.Count, actual.Items.Count);
        for (int i = 0; i < expected.Folders.Count; i++)
        {
            Assert.Equal(expected.Folders[i].Id, actual.Folders[i].Id);
            Assert.Equal(expected.Folders[i].Name, actual.Folders[i].Name);
            Assert.Equal(expected.Folders[i].ParentId, actual.Folders[i].ParentId);
        }
        for (int i = 0; i < expected.Items.Count; i++)
        {
            var e = expected.Items[i];
            var a = actual.Items[i];
            Assert.Equal(e.Id, a.Id);
            Assert.Equal(e.FolderId, a.FolderId);
            Assert.Equal(e.SourceKey, a.SourceKey);
            Assert.Equal(e.SavedAtUtc, a.SavedAtUtc);
            Assert.Equal(e.Template.Title, a.Template.Title);
            Assert.Equal(e.Template.Objects.Count, a.Template.Objects.Count);
            for (int j = 0; j < e.Template.Objects.Count; j++)
            {
                Assert.Equal(e.Template.Objects[j].Name, a.Template.Objects[j].Name);
                Assert.Equal(e.Template.Objects[j].IsActive, a.Template.Objects[j].IsActive);
                Assert.Equal(e.Template.Objects[j].Components.Count, a.Template.Objects[j].Components.Count);
                for (int k = 0; k < e.Template.Objects[j].Components.Count; k++)
                {
                    Assert.Equal(e.Template.Objects[j].Components[k].TypeKey, a.Template.Objects[j].Components[k].TypeKey);
                    Assert.True(JsonNode.DeepEquals(e.Template.Objects[j].Components[k].Values, a.Template.Objects[j].Components[k].Values));
                }
            }
        }
    }

    [Fact]
    public void Reusable_ClearsLinks_LeavesOthers_AndDeepCopies()
    {
        var template = BasicTemplate();
        var reusable = NodeLibraryOperations.Reusable(template);

        var choice = reusable.Objects[0].Components[0];
        var options = Assert.IsType<JsonArray>(choice.Values["options"]);
        Assert.Equal(0, options[0]!.AsObject()["target_node_id"]!.GetValue<int>());
        Assert.Equal(0, options[1]!.AsObject()["target_node_id"]!.GetValue<int>());

        var cond = reusable.Objects[0].Components[1];
        Assert.Equal("", cond.Values["fail_target_node_id"]!.GetValue<string>());
        Assert.Equal("gold >= 10", cond.Values["expression"]!.GetValue<string>());
        Assert.Equal("keep", cond.Values["other"]!.GetValue<string>());

        var sprite = reusable.Objects[0].Components[2];
        Assert.Equal("ada.png", sprite.Values["source"]!.GetValue<string>());

        // Kaynak şablon hâlâ bağlantıları taşır.
        var srcOptions = Assert.IsType<JsonArray>(template.Objects[0].Components[0].Values["options"]);
        Assert.Equal(5, srcOptions[0]!.AsObject()["target_node_id"]!.GetValue<int>());

        // Dönen kopyayı bozmak kaynağı etkilemez.
        options[0]!.AsObject()["target_node_id"] = 99;
        cond.Values["expression"] = "değişti";
        Assert.Equal(5, srcOptions[0]!.AsObject()["target_node_id"]!.GetValue<int>());
        Assert.Equal("gold >= 10", template.Objects[0].Components[1].Values["expression"]!.GetValue<string>());
    }

    [Fact]
    public void Save_DeepCopy_PreventsLeak_ThroughStore()
    {
        var state = NodeLibraryState.Empty;
        var template = BasicTemplate();
        var now = DateTimeOffset.UtcNow;
        state = NodeLibraryOperations.Save(state, "p:1", template, null, now, "item1");

        template.Objects[0].Components[1].Values["fail_target_node_id"] = "bozuldu";
        template.Objects[0].Components[2].Values["source"] = "bozuldu.png";

        var stored = NodeLibraryOperations.FindBySource(state, "p:1");
        Assert.NotNull(stored);
        Assert.Equal("", stored!.Template.Objects[0].Components[1].Values["fail_target_node_id"]!.GetValue<string>());
        Assert.Equal("ada.png", stored.Template.Objects[0].Components[2].Values["source"]!.GetValue<string>());

        stored.Template.Objects[0].Components[2].Values["source"] = "başka.png";
        var again = NodeLibraryOperations.FindBySource(state, "p:1");
        Assert.Equal("ada.png", again!.Template.Objects[0].Components[2].Values["source"]!.GetValue<string>());
    }

    [Fact]
    public void Save_Twice_SameSource_ThrowsAlreadySaved_UpdateKeepsIds()
    {
        var state = NodeLibraryState.Empty;
        state = NodeLibraryOperations.CreateFolder(state, "Bölüm", null, "a");
        var now = new DateTimeOffset(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);
        state = NodeLibraryOperations.Save(state, "p:1", BasicTemplate(), "a", now, "item1");
        var ex = Assert.Throws<NodeLibraryException>(() => NodeLibraryOperations.Save(state, "p:1", BasicTemplate(), null, now, "item2"));
        Assert.Equal(NodeLibraryError.AlreadySaved, ex.Error);

        var changed = new NodeTemplate("Yeni", new List<TemplateObject>());
        var later = now.AddHours(1);
        var updated = NodeLibraryOperations.Update(state, "item1", changed, later);
        var item = Assert.Single(updated.Items);
        Assert.Equal("item1", item.Id);
        Assert.Equal("a", item.FolderId);
        Assert.Equal("p:1", item.SourceKey);
        Assert.Equal("Yeni", item.Template.Title);
        Assert.Equal(later, item.SavedAtUtc);

        Assert.Throws<NodeLibraryException>(() => NodeLibraryOperations.Update(state, "yok", changed, later));
        try
        {
            NodeLibraryOperations.Update(state, "yok", changed, later);
        }
        catch (NodeLibraryException e)
        {
            Assert.Equal(NodeLibraryError.ItemNotFound, e.Error);
        }
    }

    [Fact]
    public void RemoveFolder_Collision_GetsSuffix_2_Then_3()
    {
        var state = NodeLibraryState.Empty;
        state = NodeLibraryOperations.CreateFolder(state, "Silinecek", null, "a");
        state = NodeLibraryOperations.CreateFolder(state, "Giriş", "a", "b");
        state = NodeLibraryOperations.CreateFolder(state, "Giriş", null, "c");
        var removed = NodeLibraryOperations.RemoveFolder(state, "a");
        var moved = removed.Folders.First(f => f.Id == "b");
        Assert.Equal("Giriş (2)", moved.Name);
        Assert.Null(moved.ParentId);

        var state2 = NodeLibraryState.Empty;
        state2 = NodeLibraryOperations.CreateFolder(state2, "Silinecek", null, "a");
        state2 = NodeLibraryOperations.CreateFolder(state2, "Giriş", "a", "b");
        state2 = NodeLibraryOperations.CreateFolder(state2, "Giriş", null, "c");
        state2 = NodeLibraryOperations.CreateFolder(state2, "Giriş (2)", null, "d");
        var removed2 = NodeLibraryOperations.RemoveFolder(state2, "a");
        var moved2 = removed2.Folders.First(f => f.Id == "b");
        Assert.Equal("Giriş (3)", moved2.Name);
    }

    [Fact]
    public void MoveFolder_CycleThrows_RenameInPlaceAllowed()
    {
        var state = NodeLibraryState.Empty;
        state = NodeLibraryOperations.CreateFolder(state, "Bölüm", null, "a");
        state = NodeLibraryOperations.CreateFolder(state, "Alt", "a", "b");

        var ex = Assert.Throws<NodeLibraryException>(() => NodeLibraryOperations.MoveFolder(state, "a", "b", "Bölüm"));
        Assert.Equal(NodeLibraryError.CycleNotAllowed, ex.Error);

        var self = Assert.Throws<NodeLibraryException>(() => NodeLibraryOperations.MoveFolder(state, "a", "a", "Bölüm"));
        Assert.Equal(NodeLibraryError.CycleNotAllowed, self.Error);

        var same = NodeLibraryOperations.MoveFolder(state, "a", null, "Bölüm");
        Assert.Equal("Bölüm", same.Folders.First(f => f.Id == "a").Name);
    }

    [Fact]
    public void CreateFolder_TurkishCasing_Duplicate()
    {
        var state = NodeLibraryState.Empty;
        state = NodeLibraryOperations.CreateFolder(state, "IŞIK", null, "a");
        var ex = Assert.Throws<NodeLibraryException>(() => NodeLibraryOperations.CreateFolder(state, "ışık", null, "c"));
        Assert.Equal(NodeLibraryError.DuplicateName, ex.Error);

        // Farklı konumda aynı ad kabul edilir.
        var ok = NodeLibraryOperations.CreateFolder(state, "IŞIK", "a", "b");
        Assert.Equal(2, ok.Folders.Count);
    }

    [Fact]
    public void NestedFolders_SameNameDifferentLocation_Accepts_DuplicateSameLocation_Rejects()
    {
        var state = NodeLibraryState.Empty;
        state = NodeLibraryOperations.CreateFolder(state, "Bölüm", null, "a");
        state = NodeLibraryOperations.CreateFolder(state, "Bölüm", "a", "b");
        Assert.Null(state.Folders.First(f => f.Id == "a").ParentId);
        Assert.Throws<NodeLibraryException>(() => NodeLibraryOperations.CreateFolder(state, "Bölüm", null, "c"));
    }

    [Fact]
    public void RemoveFolder_MovesChildrenAndItems_PreservesTemplate()
    {
        var state = NodeLibraryState.Empty;
        state = NodeLibraryOperations.CreateFolder(state, "Bölüm", null, "a");
        state = NodeLibraryOperations.CreateFolder(state, "Alt bölüm", "a", "b");
        var now = DateTimeOffset.UtcNow;
        state = NodeLibraryOperations.Save(state, "p:node", new NodeTemplate("Giriş", new List<TemplateObject>()), "a", now, "node");
        var before = NodeLibraryOperations.FindBySource(state, "p:node");

        var result = NodeLibraryOperations.RemoveFolder(state, "a");
        Assert.Equal(1, result.Folders.Count);
        Assert.Null(result.Folders[0].ParentId);
        Assert.Equal("b", result.Folders[0].Id);
        var item = Assert.Single(result.Items);
        Assert.Null(item.FolderId);
        Assert.Equal("Giriş", item.Template.Title);
        Assert.Equal(before!.Template.Title, item.Template.Title);
        // Girdi değişmedi.
        Assert.Equal("a", state.Items[0].FolderId);
    }

    [Fact]
    public void Store_RoundTrip_Equality()
    {
        string dir = NewTempDir();
        try
        {
            string path = Path.Combine(dir, NodeLibraryStore.FileName);
            var state = NodeLibraryState.Empty;
            state = NodeLibraryOperations.CreateFolder(state, "Bölüm", null, "a");
            var now = new DateTimeOffset(2026, 5, 1, 12, 0, 0, TimeSpan.Zero);
            state = NodeLibraryOperations.Save(state, "p:1", BasicTemplate(), "a", now, "item1");
            NodeLibraryStore.Save(path, state);
            var loaded = NodeLibraryStore.Load(path);
            Assert.False(loaded.ReadOnly);
            Assert.Null(loaded.Warning);
            AssertStatesEqual(state, loaded.State);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Store_MissingFile_ReturnsEmpty()
    {
        string dir = NewTempDir();
        try
        {
            string path = Path.Combine(dir, NodeLibraryStore.FileName);
            var result = NodeLibraryStore.Load(path);
            Assert.Empty(result.State.Folders);
            Assert.Empty(result.State.Items);
            Assert.False(result.ReadOnly);
            Assert.Null(result.Warning);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Store_CorruptFile_BackupAndEmpty()
    {
        string dir = NewTempDir();
        try
        {
            string path = Path.Combine(dir, NodeLibraryStore.FileName);
            byte[] original = System.Text.Encoding.UTF8.GetBytes("{ not json {{{");
            File.WriteAllBytes(path, original);
            var result = NodeLibraryStore.Load(path);
            Assert.Empty(result.State.Folders);
            Assert.Empty(result.State.Items);
            Assert.NotNull(result.Warning);
            var backups = Directory.GetFiles(dir, Path.GetFileName(path) + ".corrupt-*");
            Assert.Single(backups);
            Assert.Equal(original, File.ReadAllBytes(backups[0]));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Store_FutureSchema_ReadOnlyAndSaveRefuses()
    {
        string dir = NewTempDir();
        try
        {
            string path = Path.Combine(dir, NodeLibraryStore.FileName);
            var json = new JsonObject
            {
                ["schema_version"] = 99,
                ["folders"] = new JsonArray { new JsonObject { ["id"] = "a", ["name"] = "Bölüm", ["parent_id"] = null } },
                ["items"] = new JsonArray()
            }.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(path, json);
            var loaded = NodeLibraryStore.Load(path);
            Assert.True(loaded.ReadOnly);
            Assert.NotNull(loaded.Warning);
            Assert.Single(loaded.State.Folders);
            Assert.Throws<InvalidOperationException>(() => NodeLibraryStore.Save(path, loaded.State));
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }

    [Fact]
    public void Store_DanglingParent_Repaired()
    {
        string dir = NewTempDir();
        try
        {
            string path = Path.Combine(dir, NodeLibraryStore.FileName);
            var json = new JsonObject
            {
                ["schema_version"] = 1,
                ["folders"] = new JsonArray { new JsonObject { ["id"] = "a", ["name"] = "Yetim", ["parent_id"] = "yok" } },
                ["items"] = new JsonArray()
            }.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(path, json);
            var loaded = NodeLibraryStore.Load(path);
            Assert.NotNull(loaded.Warning);
            Assert.Single(loaded.State.Folders);
            Assert.Null(loaded.State.Folders[0].ParentId);
        }
        finally
        {
            Directory.Delete(dir, true);
        }
    }
}
