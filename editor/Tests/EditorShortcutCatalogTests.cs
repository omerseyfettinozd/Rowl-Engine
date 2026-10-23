using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using Avalonia.Input;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests;

// Katalog: çözülebilirlik + tekrarsızlık + yerelleştirme.
public sealed class EditorShortcutCatalogTests
{
    private static HashSet<string> ReadEntries(string lang)
    {
        string root = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
        string path = Path.Combine(root, "editor", "Assets", "locales", $"editor.{lang}.json");
        using var stream = File.OpenRead(path);
        using var doc = JsonDocument.Parse(stream);
        var entries = doc.RootElement.GetProperty("entries");
        var keys = new HashSet<string>();
        foreach (var prop in entries.EnumerateObject())
            keys.Add(prop.Name);
        return keys;
    }

    [Fact]
    public void All_Gestures_Parse()
    {
        foreach (var s in EditorShortcutCatalog.All)
        {
            var g = KeyGesture.Parse(s.Gesture);
            Assert.NotNull(g);
        }
    }

    [Fact]
    public void No_Duplicate_Gestures()
    {
        var dups = EditorShortcutCatalog.All.GroupBy(s => s.Gesture)
            .Where(g => g.Count() > 1).Select(g => g.Key).ToList();
        Assert.True(dups.Count == 0, "Tekrarlı gesture: " + string.Join(",", dups));
    }

    [Fact]
    public void DescriptionKeys_Exist_In_Both_Locales()
    {
        var tr = ReadEntries("tr");
        var en = ReadEntries("en");
        foreach (var s in EditorShortcutCatalog.All)
        {
            Assert.True(tr.Contains(s.DescriptionKey), "tr eksik: " + s.DescriptionKey);
            Assert.True(en.Contains(s.DescriptionKey), "en eksik: " + s.DescriptionKey);
        }
    }

    [Fact]
    public void Contains_F6_And_ShiftF6()
    {
        Assert.Contains(EditorShortcutCatalog.All, s => s.Gesture == "F6");
        Assert.Contains(EditorShortcutCatalog.All, s => s.Gesture == "Shift+F6");
    }
}
