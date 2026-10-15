using System.ComponentModel;
using System.Text.Json;
using System.Text.RegularExpressions;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 2b kapıları (EDITOR_UI_PORT_PLAN K5-K8): editör arayüzü çeviri altyapısı.
/// Paylaşılan <see cref="EditorStrings.Instance"/> değiştirilmez; her test kendi örneğini kullanır.
/// </summary>
public sealed class EditorStringsTests
{
    [Fact]
    public void Catalogs_SourceAndEnglishShareTheSameKeys()
    {
        var strings = new EditorStrings();
        Assert.Contains("tr", EditorStrings.AvailableLanguages);
        Assert.Contains("en", EditorStrings.AvailableLanguages);
        var tr = strings.KeysOf("tr").OrderBy(k => k, StringComparer.Ordinal).ToArray();
        var en = strings.KeysOf("en").OrderBy(k => k, StringComparer.Ordinal).ToArray();
        Assert.NotEmpty(tr);
        Assert.Equal(tr, en);
    }

    [Fact]
    public void Get_FallsBackAndNeverThrows()
    {
        var strings = new EditorStrings();
        Assert.Equal("Hiyerarşi", strings.Get("Panel.Hierarchy"));
        Assert.Equal("Yok.Böyle.Anahtar", strings.Get("Yok.Böyle.Anahtar"));
        strings.SetLanguage("xx-YY");
        Assert.Equal(EditorStrings.SourceLanguage, strings.Language);
        strings.SetLanguage("EN");
        Assert.Equal("en", strings.Language);
        Assert.Equal("Hierarchy", strings.Get("Panel.Hierarchy"));
    }

    [Fact]
    public void PseudoLocale_TransformsEnglishText()
    {
        var strings = new EditorStrings();
        strings.SetLanguage(EditorStrings.PseudoLanguage);
        string pseudo = strings.Get("Panel.Hierarchy");
        Assert.NotEqual("Hierarchy", pseudo);
        Assert.True(pseudo.Length > "Hierarchy".Length, "Sahte dil metni uzatmalı (taşma testi).");
    }

    [Fact]
    public void RightToLeft_IsDetectedFromThePrimaryTag()
    {
        Assert.True(EditorStrings.IsRightToLeft("ar"));
        Assert.True(EditorStrings.IsRightToLeft("fa-IR"));
        Assert.True(EditorStrings.IsRightToLeft("he"));
        Assert.False(EditorStrings.IsRightToLeft("tr"));
        Assert.False(EditorStrings.IsRightToLeft("en-US"));
    }

    [Fact]
    public void SetLanguage_RaisesIndexerChangeForLiveRefresh()
    {
        var strings = new EditorStrings();
        var raised = new List<string?>();
        strings.PropertyChanged += (_, e) => raised.Add(e.PropertyName);
        strings.SetLanguage("en");
        Assert.Contains(EditorStrings.IndexerPropertyName, raised);
        Assert.Contains(nameof(EditorStrings.FlowDirection), raised);
        raised.Clear();
        strings.SetLanguage("en");
        Assert.Empty(raised); // değişiklik yoksa bildirim yok
    }

    [Fact]
    public void Xaml_HardcodedTextCount_OnlyGoesDown()
    {
        string root = ProjectFileSystem.ResolveProjectRootFrom(AppContext.BaseDirectory);
        string editor = Path.Combine(root, "editor");
        using var baselineDoc = JsonDocument.Parse(File.ReadAllText(
            Path.Combine(editor, "Tests", "editor-hardcoded-text-baseline.json")));
        var baseline = baselineDoc.RootElement.GetProperty("files").EnumerateObject()
            .ToDictionary(p => p.Name, p => p.Value.GetInt32(), StringComparer.Ordinal);

        var attribute = new Regex("\\b(Header|Text|Content|ToolTip\\.Tip|Watermark|Title|PlaceholderText)=\"([^\"{][^\"]*)\"");
        var letter = new Regex("[A-Za-zÇĞİÖŞÜçğıöşü]");
        var regressions = new List<string>();
        foreach (string sub in new[] { "Views", "Controls" })
        {
            foreach (string file in Directory.GetFiles(Path.Combine(editor, sub), "*.axaml", SearchOption.AllDirectories))
            {
                string relative = Path.GetRelativePath(editor, file).Replace('\\', '/');
                int count = attribute.Matches(File.ReadAllText(file))
                    .Count(m => letter.IsMatch(m.Groups[2].Value));
                int allowed = baseline.TryGetValue(relative, out int b) ? b : 0;
                if (count > allowed)
                    regressions.Add($"{relative}: {count} > taban {allowed}");
            }
        }
        Assert.True(regressions.Count == 0,
            "Koda gömülü arayüz metni arttı; metni katalog anahtarına taşı ({l:T ...}):\n" +
            string.Join("\n", regressions));
    }
}
