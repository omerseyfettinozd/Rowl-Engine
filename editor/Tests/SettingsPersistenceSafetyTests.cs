using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>C04 — ayar/profil yazımı: bozuk dosya ezilmez, IO hatası UI'ya fırlamaz, hata bildirilir.</summary>
public sealed class SettingsPersistenceSafetyTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(), "rowl-settings-safety-" + Guid.NewGuid().ToString("N"));

    public SettingsPersistenceSafetyTests() => Directory.CreateDirectory(_dir);

    public void Dispose()
    {
        try
        {
            if (!OperatingSystem.IsWindows())
                foreach (var d in Directory.GetDirectories(_dir, "*", SearchOption.AllDirectories).Append(_dir))
                    File.SetUnixFileMode(d, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
            Directory.Delete(_dir, true);
        }
        catch { }
    }

    private static bool CanTestReadOnlyDirs => !OperatingSystem.IsWindows() && Environment.UserName != "root";

    private string MakeReadOnlyDir()
    {
        string ro = Path.Combine(_dir, "ro");
        Directory.CreateDirectory(ro);
        File.SetUnixFileMode(ro, UnixFileMode.UserRead | UnixFileMode.UserExecute);
        return ro;
    }

    [Fact]
    public void ProjectRuntimeSettings_Save_CorruptManifest_RefusesAndPreservesBytes()
    {
        string manifest = Path.Combine(_dir, "project.rowlproj");
        byte[] corrupt = System.Text.Encoding.UTF8.GetBytes("{ \"name\": \"Benim Oyunum\", ");
        File.WriteAllBytes(manifest, corrupt);

        string? error = ProjectRuntimeSettingsService.Save(manifest, new ProjectRuntimeSettings { SaveSlotCount = 20 });

        Assert.False(string.IsNullOrEmpty(error));
        Assert.Equal(corrupt, File.ReadAllBytes(manifest));

        File.WriteAllText(manifest, "[1,2,3]");
        Assert.False(string.IsNullOrEmpty(ProjectRuntimeSettingsService.Save(manifest, new ProjectRuntimeSettings())));
        Assert.Equal("[1,2,3]", File.ReadAllText(manifest));
    }

    [Fact]
    public void ProjectRuntimeSettings_Save_ReadOnlyDir_NoThrow_ReportsFailure()
    {
        if (!CanTestReadOnlyDirs) return;
        string ro = MakeReadOnlyDir();
        string? error = ProjectRuntimeSettingsService.Save(Path.Combine(ro, "project.rowlproj"), new ProjectRuntimeSettings());
        Assert.False(string.IsNullOrEmpty(error));
    }

    [Fact]
    public void PlayerSettingsProfile_Save_ReadOnlyDir_NoThrow_ReportsFailure()
    {
        if (!CanTestReadOnlyDirs) return;
        string ro = MakeReadOnlyDir();
        string? error = new PlayerSettingsProfile().Save(Path.Combine(ro, "player.json"));
        Assert.False(string.IsNullOrEmpty(error));
    }

    [Fact]
    public void EditorSettingsProfile_Update_ReadOnlyDir_NoThrow_ReportsFailure()
    {
        if (!CanTestReadOnlyDirs) return;
        string ro = MakeReadOnlyDir();
        string? reported = null;
        EditorSettingsProfile.Update(Path.Combine(ro, "editor.json"), p => p.ReduceMotion = true, m => reported = m);
        Assert.False(string.IsNullOrEmpty(reported));
    }

    [Fact]
    public void EditorSettingsProfile_Update_CorruptFile_GetsBackupBeforeOverwrite()
    {
        string path = Path.Combine(_dir, "editor.json");
        File.WriteAllText(path, "{ not json");
        string? reported = null;

        EditorSettingsProfile.Update(path, p => p.ReduceMotion = true, m => reported = m);

        string backup = Assert.Single(Directory.GetFiles(_dir, "editor.json.corrupt-*"));
        Assert.Equal("{ not json", File.ReadAllText(backup));
        Assert.False(string.IsNullOrEmpty(reported));
        Assert.True(EditorSettingsProfile.Load(path).ReduceMotion);
    }

    [Fact]
    public void PlayerSettingsProfile_Load_CorruptFile_GetsBackupAndDiagnostic()
    {
        string path = Path.Combine(_dir, "player.json");
        File.WriteAllText(path, "garbage");
        string? reported = null;

        var profile = PlayerSettingsProfile.Load(path, m => reported = m);

        Assert.Equal(1, profile.MasterVolume);
        Assert.Equal("garbage", File.ReadAllText(Assert.Single(Directory.GetFiles(_dir, "player.json.corrupt-*"))));
        Assert.False(string.IsNullOrEmpty(reported));
    }

    [Fact]
    public void PlayerLoopService_FutureVersionProfile_IsNotOverwritten_AndDiagnosticKept()
    {
        string path = Path.Combine(_dir, PlayerProfileStore.FileName);
        string future = "{\"version\": 999, \"read_content_ids\": [\"keep-me\"]}";
        File.WriteAllText(path, future);

        var loop = PlayerLoopService.Load(_dir);
        loop.NotePresented(new[] { "11111111-2222-3333-4444-555555555555" });
        loop.Flush();

        Assert.Equal(future, File.ReadAllText(path));
        Assert.Equal(PlayerProfileLoadStatus.UnsupportedVersionDefaults, loop.LoadStatus);
        Assert.False(string.IsNullOrEmpty(loop.LoadDiagnostic));
    }

    [Fact]
    public void PlayerLoopService_CorruptProfile_IsBackedUpBeforeFlush()
    {
        string path = Path.Combine(_dir, PlayerProfileStore.FileName);
        File.WriteAllText(path, "{ broken");

        var loop = PlayerLoopService.Load(_dir);
        loop.NotePresented(new[] { "11111111-2222-3333-4444-555555555555" });
        Assert.Null(loop.Flush());

        Assert.Equal("{ broken", File.ReadAllText(Assert.Single(Directory.GetFiles(_dir, PlayerProfileStore.FileName + ".corrupt-*"))));
        Assert.False(string.IsNullOrEmpty(loop.LoadDiagnostic));
    }
}
