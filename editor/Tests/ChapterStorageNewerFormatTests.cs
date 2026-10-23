using System;
using System.IO;
using System.Text.Json.Nodes;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C04 — daha yeni format_version taşıyan chapter dosyası/indeksi okunmaz
/// (kullanıcıya mesajlı hata) ve Split tarafından ezilmez.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class ChapterStorageNewerFormatTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "RowlChapNewer_" + Guid.NewGuid().ToString("N"));

    public ChapterStorageNewerFormatTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        try { Directory.Delete(_dir, recursive: true); } catch (Exception) { }
    }

    private const string Full = """
        { "format_version": 5, "start_node_id": 1,
          "chapters": [ { "id": "c1", "title": "C1", "order": 0, "summary": "" } ],
          "nodes": [ { "id": 1, "chapter_id": "c1", "next_nodes": [] } ] }
        """;

    private string Bump(string file, int version)
    {
        string path = Path.Combine(_dir, file);
        var doc = JsonNode.Parse(File.ReadAllText(path))!;
        doc["format_version"] = version;
        File.WriteAllText(path, doc.ToJsonString());
        return path;
    }

    [Fact]
    public void LoadChapterFile_Newer_Throws_WithMessage()
    {
        ChapterStorageService.Split(Full, _dir);
        string path = Bump("c1.json", GraphStructureLimits.CurrentVersion + 1);
        var ex = Assert.Throws<InvalidOperationException>(() => ChapterStorageService.LoadChapterFile(path));
        Assert.Contains("v6", ex.Message);
    }

    [Fact]
    public void Merge_NewerChapterFile_Throws()
    {
        ChapterStorageService.Split(Full, _dir);
        Bump("c1.json", GraphStructureLimits.CurrentVersion + 1);
        Assert.Throws<InvalidOperationException>(() => ChapterStorageService.Merge(_dir));
    }

    [Fact]
    public void Merge_NewerIndex_Throws()
    {
        ChapterStorageService.Split(Full, _dir);
        Bump(ChapterStorageService.IndexFileName, GraphStructureLimits.CurrentVersion + 1);
        Assert.Throws<InvalidOperationException>(() => ChapterStorageService.Merge(_dir));
    }

    [Theory]
    [InlineData("c1.json")]
    [InlineData("chapter_index.json")]
    public void Split_NeverOverwritesNewerFiles(string newerFile)
    {
        ChapterStorageService.Split(Full, _dir);
        string path = Bump(newerFile, GraphStructureLimits.CurrentVersion + 1);
        string before = File.ReadAllText(path);
        string other = Path.Combine(_dir, newerFile == "c1.json" ? "chapter_index.json" : "c1.json");
        string otherBefore = File.ReadAllText(other);

        Assert.Throws<InvalidOperationException>(() => ChapterStorageService.Split(Full, _dir));

        Assert.Equal(before, File.ReadAllText(path));
        Assert.Equal(otherBefore, File.ReadAllText(other));
    }

    [Fact]
    public void CurrentAndLegacyFiles_StillRoundTrip()
    {
        ChapterStorageService.Split(Full, _dir);
        Assert.Contains("\"nodes\"", ChapterStorageService.Merge(_dir));
        ChapterStorageService.Split(Full, _dir); // yeniden yazma serbest
    }
}
