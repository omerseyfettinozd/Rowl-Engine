using System;
using System.IO;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Tests;

/// <summary>D01b: the editor reads the engine's per-game data directory through the bridge.</summary>
[Collection("StaticRootSequential")]
public sealed class GameDataBridgeTests
{
    private static string UserDataRoot()
    {
        string? xdg = Environment.GetEnvironmentVariable("XDG_DATA_HOME");
        string baseDir = !string.IsNullOrEmpty(xdg) && Path.IsPathRooted(xdg)
            ? xdg
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "share");
        return Path.Combine(baseDir, "rowl-engine");
    }

    private static GameDataInfo Mount(string root)
    {
        using var host = new EngineHost { StartupProjectRoot = root };
        Assert.True(host.Initialize(64, 64, false), host.LastError);
        return host.InvokeOnEngineThread(GameDataBridge.Read, GameDataInfo.None);
    }

    private static string NewProject(string tag, string? uuid)
    {
        string root = Path.Combine(Path.GetTempPath(), $"RowlGameData{tag}_{Guid.NewGuid():N}");
        Directory.CreateDirectory(Path.Combine(root, "Assets", "images"));
        if (uuid != null)
            File.WriteAllText(Path.Combine(root, "project.rowlproj"), "{\"project_uuid\":\"" + uuid + "\"}");
        return root;
    }

    [Fact]
    public void ProjectWithStoredUuid_UsesPerGameDirectory()
    {
        if (!OperatingSystem.IsLinux()) return;
        const string id = "5a3c9e10-2b64-4f7d-8a15-9c0d1e2f3a4b";
        string root = NewProject("Id", id);
        try
        {
            var info = Mount(root);
            Assert.Equal(Path.Combine(UserDataRoot(), "games", id), info.DataDirectory);
            Assert.Equal(string.Empty, info.MigrationJson); // nothing legacy to copy
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }

    [Fact]
    public void ProjectWithoutUuid_KeepsProjectLocalData()
    {
        string root = NewProject("Local", null);
        try
        {
            var info = Mount(root);
            Assert.Equal(root, info.DataDirectory); // an unsaved project never persists per-game
        }
        finally
        {
            try { Directory.Delete(root, true); } catch { }
        }
    }
}
