using System;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D01a: a build assigns/persists the game id (project_uuid) and refuses a
/// manifest it cannot trust. A corrupt manifest must stay byte-identical.
/// </summary>
public sealed class GameIdentityBuildTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), $"RowlGameIdentity_{Guid.NewGuid():N}");

    public GameIdentityBuildTests()
    {
        Directory.CreateDirectory(Path.Combine(_root, "Assets", "json"));
        File.WriteAllText(Path.Combine(_root, "Assets", "json", "full_story_graph.json"),
            "{\"start_node_id\":1,\"nodes\":[{\"id\":1}]}\n");
    }

    public void Dispose()
    {
        try { Directory.Delete(_root, true); } catch { }
        try { Directory.Delete(_root + "_out", true); } catch { }
    }

    private StandaloneBuildResult Build() =>
        ProjectBuildService.BuildStandalone(_root, Path.Combine(_root, "Assets"), _root + "_out");

    [Fact]
    public void LegacyProjectWithoutUuid_BuildPersistsOne()
    {
        string manifest = Path.Combine(_root, "project.rowlproj");
        File.WriteAllText(manifest, "{\n  \"name\": \"Legacy\"\n}\n");

        var result = Build();

        // The id is persisted before any other build step, whatever the outcome
        // (a missing native runtime in a bare checkout fails later).
        string? stored = ProjectIdentityService.TryReadStoredUuid(manifest, out string? error);
        Assert.Null(error);
        Assert.NotNull(stored);
        Assert.True(Guid.TryParse(stored, out _));
        using var doc = JsonDocument.Parse(File.ReadAllText(manifest));
        Assert.Equal("Legacy", doc.RootElement.GetProperty("name").GetString());

        // A second build keeps the same id (stable identity).
        if (Directory.Exists(_root + "_out")) Directory.Delete(_root + "_out", true);
        Build();
        Assert.Equal(stored, ProjectIdentityService.TryReadStoredUuid(manifest, out _));

        if (result.Succeeded)
        {
            // verify_release_package (run inside the build) already required
            // rowl/game.json and equality with the loose manifest id.
            Assert.True(File.Exists(Path.Combine(_root + "_out", "project.rowlproj")));
        }
    }

    [Fact]
    public void CorruptManifest_RefusesBuild_AndLeavesFileByteIdentical()
    {
        string manifest = Path.Combine(_root, "project.rowlproj");
        byte[] corrupt = System.Text.Encoding.UTF8.GetBytes("{ \"name\": \"Broken\", ");
        File.WriteAllBytes(manifest, corrupt);

        var result = Build();

        Assert.False(result.Succeeded);
        Assert.Equal(BuildDiagnosticCode.InvalidInput, result.Diagnostic?.Code);
        Assert.Contains("not valid JSON", result.Diagnostic!.Message);
        Assert.Equal(corrupt, File.ReadAllBytes(manifest));
        Assert.False(Directory.Exists(_root + "_out"));
    }

    [Fact]
    public void MissingManifest_RefusesBuild()
    {
        var result = Build();

        Assert.False(result.Succeeded);
        Assert.Equal(BuildDiagnosticCode.InvalidInput, result.Diagnostic?.Code);
        Assert.False(File.Exists(Path.Combine(_root, "project.rowlproj")));
    }
}

/// <summary>D01a: editor and player read the same id through the C API bridge.</summary>
[Collection("StaticRootSequential")]
public sealed class GameIdentityBridgeTests
{
    [Fact]
    public void MountedProject_ReportsManifestIdThroughBridge()
    {
        string root = Path.Combine(Path.GetTempPath(), $"RowlGameIdentityBridge_{Guid.NewGuid():N}");
        Directory.CreateDirectory(Path.Combine(root, "Assets", "images"));
        const string id = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90";
        File.WriteAllText(Path.Combine(root, "project.rowlproj"), "{\"project_uuid\":\"" + id + "\"}");
        try
        {
            using var host = new RowlEngine.Editor.Native.EngineHost { StartupProjectRoot = root };
            Assert.True(host.Initialize(64, 64, false), host.LastError);

            var info = host.InvokeOnEngineThread(
                RowlEngine.Editor.Native.GameIdentityBridge.Read,
                RowlEngine.Editor.Native.GameIdentityInfo.None);

            Assert.Equal(id, info.GameId);
            Assert.Equal(RowlEngine.Editor.Native.GameIdentitySource.Manifest, info.Source);
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }
}
