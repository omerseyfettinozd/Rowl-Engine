using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>Faz 8 B1-B3: import çakışma + ilerleme + iptal (saf, geçici dizin).</summary>
public sealed class EditorAssetImportConflictTests
{
    private sealed class TempDirs : IDisposable
    {
        public string Root { get; } = Path.Combine(Path.GetTempPath(), "RowlImport_" + Guid.NewGuid().ToString("N"));
        public string Source => Path.Combine(Root, "src");
        public string Assets => Path.Combine(Root, "Assets");
        public TempDirs()
        {
            Directory.CreateDirectory(Source);
            Directory.CreateDirectory(Assets);
        }
        public void Dispose()
        {
            try { Directory.Delete(Root, recursive: true); } catch (Exception) { }
        }
    }

    private sealed class CollectProgress : IProgress<ImportProgress>
    {
        public List<ImportProgress> Reports { get; } = new();
        public void Report(ImportProgress value) { Reports.Add(value); }
    }

    private static string WriteSource(string dir, string name, byte[] bytes)
    {
        string p = Path.Combine(dir, name);
        Directory.CreateDirectory(dir);
        File.WriteAllBytes(p, bytes);
        return p;
    }

    [Fact]
    public async Task NewFile_Copies()
    {
        using var tmp = new TempDirs();
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 1, 2, 3 });
        var list = await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        Assert.Equal(new[] { "images/a.png" }, list);
        Assert.True(File.Exists(Path.Combine(tmp.Assets, "images", "a.png")));
    }

    [Fact]
    public async Task SameContent_SkippedIdentical_MtimeUnchanged()
    {
        using var tmp = new TempDirs();
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 9, 9, 9 });
        var first = await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        Assert.Equal(new[] { "images/a.png" }, first);
        string dest = Path.Combine(tmp.Assets, "images", "a.png");
        var old = new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(dest, old);
        var second = await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        Assert.Equal(new[] { "images/a.png" }, second);
        Assert.Equal(old, File.GetLastWriteTimeUtc(dest));
        Assert.False(Directory.GetFiles(tmp.Assets, "*.part", SearchOption.AllDirectories).Any());
    }

    [Fact]
    public async Task DifferentContent_NullResolver_KeepBoth()
    {
        using var tmp = new TempDirs();
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 1, 1, 1 });
        await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        File.WriteAllBytes(src, new byte[] { 2, 2, 2, 2 });
        var list = await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        Assert.Equal(new[] { "images/a (2).png" }, list);
        // Orijinal değişmez.
        Assert.Equal(new byte[] { 1, 1, 1 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a.png")));
        Assert.Equal(new byte[] { 2, 2, 2, 2 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a (2).png")));
    }

    [Fact]
    public async Task Overwrite_Replaces()
    {
        using var tmp = new TempDirs();
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 1 });
        await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        File.WriteAllBytes(src, new byte[] { 7, 7 });
        var list = await EditorAssetImportService.ImportAssetFilesAsync(
            new[] { src }, tmp.Assets, null, null, default, null,
            (_, _) => Task.FromResult(new ImportConflictDecision(ImportConflictChoice.Overwrite)));
        Assert.Equal(new[] { "images/a.png" }, list);
        Assert.Equal(new byte[] { 7, 7 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a.png")));
    }

    [Fact]
    public async Task Skip_KeepsExisting_NotListed()
    {
        using var tmp = new TempDirs();
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 1 });
        await EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, default);
        File.WriteAllBytes(src, new byte[] { 8, 8 });
        var list = await EditorAssetImportService.ImportAssetFilesAsync(
            new[] { src }, tmp.Assets, null, null, default, null,
            (_, _) => Task.FromResult(new ImportConflictDecision(ImportConflictChoice.Skip)));
        Assert.Empty(list);
        Assert.Equal(new byte[] { 1 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a.png")));
    }

    [Fact]
    public async Task ApplyToAll_CalledOnce()
    {
        using var tmp = new TempDirs();
        string a = WriteSource(tmp.Source, "a.png", new byte[] { 1 });
        string b = WriteSource(tmp.Source, "b.png", new byte[] { 2 });
        await EditorAssetImportService.ImportAssetFilesAsync(new[] { a, b }, tmp.Assets, null, null, default);
        File.WriteAllBytes(a, new byte[] { 10 });
        File.WriteAllBytes(b, new byte[] { 20 });
        int calls = 0;
        var list = await EditorAssetImportService.ImportAssetFilesAsync(
            new[] { a, b }, tmp.Assets, null, null, default, null,
            (_, _) =>
            {
                calls++;
                return Task.FromResult(new ImportConflictDecision(ImportConflictChoice.Overwrite, ApplyToAll: true));
            });
        Assert.Equal(1, calls);
        Assert.Equal(2, list.Count);
        Assert.Equal(new byte[] { 10 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a.png")));
        Assert.Equal(new byte[] { 20 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "b.png")));
    }

    [Fact]
    public void UniqueName_Sequential()
    {
        using var tmp = new TempDirs();
        string dir = Path.Combine(tmp.Root, "u");
        Directory.CreateDirectory(dir);
        File.WriteAllBytes(Path.Combine(dir, "a.png"), new byte[] { 1 });
        Assert.Equal("a (2).png", EditorAssetImportService.UniqueName(dir, "a.png"));
        File.WriteAllBytes(Path.Combine(dir, "a (2).png"), new byte[] { 2 });
        Assert.Equal("a (3).png", EditorAssetImportService.UniqueName(dir, "a.png"));
        // Uzantısız.
        File.WriteAllBytes(Path.Combine(dir, "README"), new byte[] { 3 });
        Assert.Equal("README (2)", EditorAssetImportService.UniqueName(dir, "README"));
    }

    [Fact]
    public async Task Cancel_PreCancelled_Throws_NoPart()
    {
        using var tmp = new TempDirs();
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 1, 2 });
        using var cts = new CancellationTokenSource();
        cts.Cancel();
        await Assert.ThrowsAsync<OperationCanceledException>(() =>
            EditorAssetImportService.ImportAssetFilesAsync(new[] { src }, tmp.Assets, null, null, cts.Token));
        Assert.Empty(Directory.GetFiles(tmp.Assets, "*.part", SearchOption.AllDirectories));
        Assert.Empty(Directory.GetFiles(tmp.Assets, "*rowlimport*", SearchOption.AllDirectories));
    }

    [Fact]
    public async Task Progress_ThreeFiles_LastReport()
    {
        using var tmp = new TempDirs();
        string s1 = WriteSource(tmp.Source, "s1.png", new byte[] { 1 });
        string s2 = WriteSource(tmp.Source, "s2.png", new byte[] { 2 });
        string s3 = WriteSource(tmp.Source, "s3.wav", new byte[] { 3 });
        var prog = new CollectProgress();
        var list = await EditorAssetImportService.ImportAssetFilesAsync(
            new[] { s1, s2, s3 }, tmp.Assets, null, null, default, prog);
        Assert.Equal(3, list.Count);
        Assert.NotEmpty(prog.Reports);
        Assert.Equal(new ImportProgress(0, 3, string.Empty), prog.Reports[0]);
        var last = prog.Reports[^1];
        Assert.Equal(3, last.Completed);
        Assert.Equal(3, last.Total);
    }

    [Fact]
    public void ImportImageFile_DifferentContent_KeepBoth()
    {
        using var tmp = new TempDirs();
        string srcDir2 = Path.Combine(tmp.Root, "src2");
        string src = WriteSource(tmp.Source, "a.png", new byte[] { 5, 5 });
        string first = EditorAssetImportService.ImportImageFile(src, tmp.Assets);
        Assert.Equal("a.png", first);
        string srcOther = WriteSource(srcDir2, "a.png", new byte[] { 6, 6, 6 });
        string second = EditorAssetImportService.ImportImageFile(srcOther, tmp.Assets);
        Assert.Equal("a (2).png", second);
        Assert.Equal(new byte[] { 5, 5 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a.png")));
        Assert.Equal(new byte[] { 6, 6, 6 }, File.ReadAllBytes(Path.Combine(tmp.Assets, "images", "a (2).png")));
    }
}
