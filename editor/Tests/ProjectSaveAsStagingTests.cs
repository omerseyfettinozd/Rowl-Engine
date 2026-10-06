using System.Text.Json.Nodes;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

public sealed class ProjectSaveAsStagingTests : IDisposable
{
    private readonly string _parent = Path.Combine(Path.GetTempPath(), "RowlSaveAsStage_" + Guid.NewGuid().ToString("N"));
    private string Source => Path.Combine(_parent, "source");
    private string Target => Path.Combine(_parent, "copy");

    public ProjectSaveAsStagingTests()
    {
        Write("Assets/json/full_story_graph.json", "{\"nodes\":[{\"id\":101}],\"connections\":[]}");
        Write("Assets/scripts/chapter.lua", "return 42");
        Write("SourceAssets/章/hero.webp", "source media");
        Write("Assets/images/章/hero.png", "converted media");
        Write("Assets/images/章/hero.png.rowlconv.json", "{\"source_path\":\"章/hero.webp\",\"source_sha256\":\"original\"}");
        Write("project.rowlproj", "{\"name\":\"Example\",\"project_uuid\":\"stable-id\",\"custom\":{\"keep\":true}}");
        Write("README.md", "author notes");
        Write("golden_project.json", "{\"fixture\":1}");
        Write("build/cache.bin", "do not copy");
        Write(".git/config", "do not copy");
    }

    private void Write(string relative, string content)
    {
        string path = Path.Combine(Source, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
    }

    public void Dispose() => Directory.Delete(_parent, true);

    private void AssertNoStaging() => Assert.Empty(Directory.GetDirectories(_parent, ".copy.*.saving"));

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void CopiesAuthoringInventoryAndPreservesProvenanceAndManifestFields(bool trailingSeparator)
    {
        var original = Directory.GetFiles(Source, "*", SearchOption.AllDirectories)
            .ToDictionary(path => path, File.ReadAllBytes);
        string destination = trailingSeparator ? Target + Path.DirectorySeparatorChar : Target;
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, destination, 7, 101, () => true);
        Assert.True(result.Succeeded, result.Message);
        foreach (string relative in new[] { "Assets/json/full_story_graph.json", "Assets/scripts/chapter.lua",
                     "SourceAssets/章/hero.webp", "Assets/images/章/hero.png", "Assets/images/章/hero.png.rowlconv.json",
                     "README.md", "golden_project.json" })
            Assert.Equal(File.ReadAllBytes(Path.Combine(Source, relative)), File.ReadAllBytes(Path.Combine(Target, relative)));
        var manifest = JsonNode.Parse(File.ReadAllText(Path.Combine(Target, "project.rowlproj")))!;
        Assert.Equal("stable-id", (string?)manifest["project_uuid"]);
        Assert.True((bool)manifest["custom"]!["keep"]!);
        Assert.Equal(7, (int)manifest["nodeCount"]!);
        Assert.False(Directory.Exists(Path.Combine(Target, "build")));
        Assert.False(Directory.Exists(Path.Combine(Target, ".git")));
        foreach (var pair in original) Assert.Equal(pair.Value, File.ReadAllBytes(pair.Key));
        AssertNoStaging();
    }

    [Fact]
    public void LockedRawSourceFailsAfterAssetsCopyWithoutPublishingPartialProject()
    {
        using var held = new FileStream(Path.Combine(Source, "SourceAssets/章/hero.webp"),
            FileMode.Open, FileAccess.ReadWrite, FileShare.None);
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, Target, 1, 101, () => true);
        Assert.False(result.Succeeded);
        Assert.False(Directory.Exists(Target));
        AssertNoStaging();
    }

    [Fact]
    public void CancellationAfterFirstCopiedFileCleansPartialStage()
    {
        using var token = new CancellationTokenSource();
        int copied = 0;
        var result = EditorProjectLifecycleCoordinator.ExecuteSaveAs(Source, Target, 1, 101, () => true,
            cancellationToken: token.Token, reportCopiedFile: _ => { copied++; token.Cancel(); });
        Assert.Equal(1, copied);
        Assert.False(result.Succeeded);
        Assert.True(result.Cancelled);
        Assert.False(Directory.Exists(Target));
        AssertNoStaging();
    }

    [Fact]
    public void PreCancelledCopyNeverSavesSourceOrCreatesTarget()
    {
        using var token = new CancellationTokenSource();
        token.Cancel();
        var log = new List<string>();
        var result = EditorProjectLifecycleCoordinator.ExecuteSaveAs(Source, Target, 1, 101,
            () => throw new Exception("Source save must not run"), log.Add, token.Token);
        Assert.True(result.Cancelled);
        Assert.Single(log);
        Assert.Contains("iptal", log[0]);
        Assert.False(Directory.Exists(Target));
        AssertNoStaging();
    }

    [Fact]
    public void ConcurrentTargetCreationIsPreservedAndStageIsRemoved()
    {
        bool created = false;
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, Target, 1, 101, () => true,
            reportCopiedFile: _ =>
            {
                if (created) return;
                created = true;
                Directory.CreateDirectory(Target);
                File.WriteAllText(Path.Combine(Target, "keep.txt"), "other writer");
            });
        Assert.False(result.Succeeded);
        Assert.Equal("other writer", File.ReadAllText(Path.Combine(Target, "keep.txt")));
        Assert.Single(Directory.GetFiles(Target));
        AssertNoStaging();
    }

    [Theory]
    [InlineData("not-json")]
    [InlineData("[]")]
    public void InvalidManifestFailsWithoutInventingReplacement(string manifest)
    {
        Write("project.rowlproj", manifest);
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, Target, 1, 101, () => true);
        Assert.False(result.Succeeded);
        Assert.Equal(manifest, File.ReadAllText(Path.Combine(Source, "project.rowlproj")));
        Assert.False(Directory.Exists(Target));
        AssertNoStaging();
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ExistingTargetIsNotMergedAndSourceSaveDoesNotRun(bool fileTarget)
    {
        if (fileTarget) File.WriteAllText(Target, "existing project");
        else
        {
            Directory.CreateDirectory(Target);
            File.WriteAllText(Path.Combine(Target, "keep.txt"), "existing project");
        }
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, Target, 1, 101,
            () => throw new Exception("Source save must not run"));
        Assert.False(result.Succeeded);
        Assert.Equal("existing project", File.ReadAllText(fileTarget ? Target : Path.Combine(Target, "keep.txt")));
        if (!fileTarget) Assert.Single(Directory.GetFiles(Target));
        AssertNoStaging();
    }

    [Fact]
    public void MissingCanonicalGraphCannotPublishAManifestOnlyProject()
    {
        File.Delete(Path.Combine(Source, "Assets/json/full_story_graph.json"));
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, Target, 1, 101, () => true);
        Assert.False(result.Succeeded);
        Assert.False(Directory.Exists(Target));
        AssertNoStaging();
    }

    [Fact]
    public void LegacyProjectWithoutManifestOrSourceAssetsStillCopiesCanonicalGraph()
    {
        File.Delete(Path.Combine(Source, "project.rowlproj"));
        Directory.Delete(Path.Combine(Source, "SourceAssets"), true);
        var result = ProjectSaveAsCoordinator.SaveProjectCopy(Source, Target, 1, 101, () => true);
        Assert.True(result.Succeeded, result.Message);
        Assert.True(File.Exists(Path.Combine(Target, "Assets/json/full_story_graph.json")));
        Assert.True(File.Exists(Path.Combine(Target, "project.rowlproj")));
        AssertNoStaging();
    }
}
