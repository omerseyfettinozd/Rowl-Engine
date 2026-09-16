using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 5 Dilim 4 fix turu 1 — chapter feed: temp dizin +
/// <c>chapter_index.json</c> + 2 bölüm dosyası fixture'ıyla
/// <c>PrefetchChapterFeedService.FeedFromAssetsDir</c> seam girişi.
/// Headless, native çağrı YOKTUR (handle + delegeler mock).
/// </summary>
public sealed class EditorPrefetchChapterFeedServiceTests
{
    private static readonly IntPtr LiveHandle = new(123);

    private sealed class Recorder
    {
        public readonly List<string> IndexJsons = new();
        public readonly List<string> ChapterJsons = new();
        public readonly List<string> Logs = new();

        public int LoadIndex(IntPtr handle, string json)
        {
            IndexJsons.Add(json);
            return 0;
        }

        public int AppendFile(IntPtr handle, string json)
        {
            ChapterJsons.Add(json);
            return 0;
        }

        public void Log(string message) => Logs.Add(message);
    }

    private static string MakeAssetsJson(out string root)
    {
        root = Path.Combine(Path.GetTempPath(), "RowlFeed_" + Guid.NewGuid().ToString("N"));
        string assetsJson = Path.Combine(root, "json");
        Directory.CreateDirectory(Path.Combine(assetsJson, "chapters"));
        return assetsJson;
    }

    private static void DeleteRoot(string root)
    {
        try { Directory.Delete(root, recursive: true); }
        catch (Exception) { }
    }

    private static void WriteIndex(string assetsJson) =>
        File.WriteAllText(
            Path.Combine(assetsJson, "chapters", "chapter_index.json"),
            "{\"format_version\":5,\"chapters\":[{\"id\":\"ch1\"},{\"id\":\"ch2\"}]}");

    private static void WriteChapter(string assetsJson, string chapterId) =>
        File.WriteAllText(
            Path.Combine(assetsJson, "chapters", chapterId + ".json"),
            $"{{\"chapter_id\":\"{chapterId}\",\"nodes\":[]}}");

    private static int Feed(
        string assetsJson, Recorder recorder, IntPtr handle) =>
        PrefetchChapterFeedService.FeedFromAssetsDir(
            assetsJson,
            () => handle,
            recorder.LoadIndex,
            recorder.AppendFile,
            recorder.Log);

    [Fact]
    public void Feed_IndexAndTwoFiles_ForwardedInOrder()
    {
        string assetsJson = MakeAssetsJson(out string root);
        try
        {
            WriteIndex(assetsJson);
            WriteChapter(assetsJson, "ch1");
            WriteChapter(assetsJson, "ch2");
            var recorder = new Recorder();

            int forwarded = Feed(assetsJson, recorder, LiveHandle);

            Assert.Equal(3, forwarded);
            Assert.Single(recorder.IndexJsons);
            Assert.Contains("\"chapters\"", recorder.IndexJsons[0]);
            Assert.Equal(2, recorder.ChapterJsons.Count);
            Assert.Contains("\"ch1\"", recorder.ChapterJsons[0]);
            Assert.Contains("\"ch2\"", recorder.ChapterJsons[1]);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_MissingChaptersDir_SilentNoOp()
    {
        string root = Path.Combine(Path.GetTempPath(), "RowlFeed_" + Guid.NewGuid().ToString("N"));
        string assetsJson = Path.Combine(root, "json");
        Directory.CreateDirectory(assetsJson);
        try
        {
            var recorder = new Recorder();

            int forwarded = Feed(assetsJson, recorder, LiveHandle);

            Assert.Equal(0, forwarded);
            Assert.Empty(recorder.IndexJsons);
            Assert.Empty(recorder.ChapterJsons);
            Assert.NotEmpty(recorder.Logs);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_MissingIndex_SilentNoOp()
    {
        string assetsJson = MakeAssetsJson(out string root);
        try
        {
            WriteChapter(assetsJson, "ch1");
            var recorder = new Recorder();

            int forwarded = Feed(assetsJson, recorder, LiveHandle);

            Assert.Equal(0, forwarded);
            Assert.Empty(recorder.IndexJsons);
            Assert.Empty(recorder.ChapterJsons);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_CorruptChapterFile_SkippedAndContinues()
    {
        string assetsJson = MakeAssetsJson(out string root);
        try
        {
            WriteIndex(assetsJson);
            WriteChapter(assetsJson, "ch1");
            File.WriteAllText(
                Path.Combine(assetsJson, "chapters", "chX.json"), "{ bozuk json");
            WriteChapter(assetsJson, "ch2");
            var recorder = new Recorder();

            int forwarded = Feed(assetsJson, recorder, LiveHandle);

            Assert.Equal(3, forwarded);
            Assert.Single(recorder.IndexJsons);
            Assert.Equal(2, recorder.ChapterJsons.Count);
            Assert.DoesNotContain(recorder.ChapterJsons, j => j.Contains("bozuk"));
            Assert.NotEmpty(recorder.Logs);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_CorruptIndex_FilesStillForwarded()
    {
        string assetsJson = MakeAssetsJson(out string root);
        try
        {
            File.WriteAllText(
                Path.Combine(assetsJson, "chapters", "chapter_index.json"), "{ bozuk");
            WriteChapter(assetsJson, "ch1");
            var recorder = new Recorder();

            int forwarded = Feed(assetsJson, recorder, LiveHandle);

            Assert.Equal(1, forwarded);
            Assert.Empty(recorder.IndexJsons);
            Assert.Single(recorder.ChapterJsons);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_DeadHandle_NoOp()
    {
        string assetsJson = MakeAssetsJson(out string root);
        try
        {
            WriteIndex(assetsJson);
            WriteChapter(assetsJson, "ch1");
            WriteChapter(assetsJson, "ch2");
            var recorder = new Recorder();

            int forwarded = Feed(assetsJson, recorder, IntPtr.Zero);

            Assert.Equal(0, forwarded);
            Assert.Empty(recorder.IndexJsons);
            Assert.Empty(recorder.ChapterJsons);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_ThrowingDelegate_FailClosed()
    {
        string assetsJson = MakeAssetsJson(out string root);
        try
        {
            WriteIndex(assetsJson);
            WriteChapter(assetsJson, "ch1");
            var recorder = new Recorder();
            var exception = Record.Exception(() => PrefetchChapterFeedService.FeedFromAssetsDir(
                assetsJson,
                () => LiveHandle,
                (_, _) => throw new InvalidOperationException("native kapalı"),
                recorder.AppendFile,
                recorder.Log));

            Assert.Null(exception);
            Assert.Empty(recorder.IndexJsons);
            Assert.Single(recorder.ChapterJsons);
        }
        finally
        {
            DeleteRoot(root);
        }
    }

    [Fact]
    public void Feed_NullHost_FailClosed()
    {
        var exception = Record.Exception(() =>
            PrefetchChapterFeedService.FeedFromAssetsDir(null, "x", null));
        Assert.Null(exception);
        Assert.Equal(0, PrefetchChapterFeedService.FeedFromAssetsDir(null, "x", null));
    }
}
