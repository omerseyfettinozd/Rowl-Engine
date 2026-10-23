using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels.Player;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// D01c-2: the editor player uses the mounted game's profile (native per-game
/// store as the only writer; managed per-game mirror as fallback), never one
/// global file. Native-backed tests mount real projects on a real engine.
/// </summary>
[Collection("StaticRootSequential")]
public sealed class EditorPlayerProfilePerGameTests : IDisposable
{
    private const string ReadA = "0f0e0d0c-0b0a-0908-0706-050403020100";
    private const string ReadB = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";

    private readonly List<string> _dirs = new();
    private readonly string _scratch;

    public EditorPlayerProfilePerGameTests()
    {
        _scratch = Path.Combine(Path.GetTempPath(), $"RowlProfile2_{Guid.NewGuid():N}");
        Directory.CreateDirectory(_scratch);
        _dirs.Add(_scratch);
    }

    public void Dispose()
    {
        foreach (string dir in _dirs)
        {
            try { Directory.Delete(dir, true); } catch { }
        }
    }

    // ── helpers ────────────────────────────────────────────────────────────

    private static string UserDataRoot()
    {
        string? xdg = Environment.GetEnvironmentVariable("XDG_DATA_HOME");
        string baseDir = !string.IsNullOrEmpty(xdg) && Path.IsPathRooted(xdg)
            ? xdg
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "share");
        return Path.Combine(baseDir, "rowl-engine");
    }

    private string GameProfileDir(string uuid) => Path.Combine(UserDataRoot(), "games", uuid, "profile");

    private static string NewUuid() => Guid.NewGuid().ToString("D");

    private string NewProject(string? uuid)
    {
        string root = Path.Combine(_scratch, $"proj_{Guid.NewGuid():N}");
        Directory.CreateDirectory(Path.Combine(root, "Assets", "images"));
        if (uuid != null)
        {
            File.WriteAllText(Path.Combine(root, "project.rowlproj"), "{\"project_uuid\":\"" + uuid + "\"}");
            _dirs.Add(Path.Combine(UserDataRoot(), "games", uuid)); // per-game data lives outside the project
        }
        return root;
    }

    private static EngineHost Mount(string root)
    {
        var host = new EngineHost { StartupProjectRoot = root };
        Assert.True(host.Initialize(64, 64, false), host.LastError);
        return host;
    }

    /// <summary>Empty legacy-global directory (nothing to migrate unless a test fills it).</summary>
    private string NewGlobalDir()
    {
        string dir = Path.Combine(_scratch, "global_" + Guid.NewGuid().ToString("N"), "rowl-engine", "profiles");
        return dir;
    }

    private static PlayerLoopService Open(EngineHost host, string root, string globalDir, List<string>? log = null) =>
        PlayerProfileSession.Open(host, root, msg => log?.Add(msg), globalDir);

    private static bool NativeProfileUsable() =>
        OperatingSystem.IsLinux() && NativeProfileBridge.IsSupported();

    private static string FixturePath()
    {
        string? dir = AppContext.BaseDirectory;
        while (dir != null)
        {
            string candidate = Path.Combine(dir, "tests", "fixtures", "profile", "player-profile.v1.json");
            if (File.Exists(candidate))
                return candidate;
            dir = Path.GetDirectoryName(dir);
        }
        throw new FileNotFoundException("tests/fixtures/profile/player-profile.v1.json not found above " + AppContext.BaseDirectory);
    }

    private static string WriteGlobalProfile(string globalDir)
    {
        var profile = new PlayerProfile { MasterVolume = 0.4f, SkipMode = PlayerSkipMode.All, Language = "tr", AutoEnabled = true };
        profile.MarkRead(ReadA);
        profile.MarkRead(ReadB);
        Assert.Null(PlayerProfileStore.Save(globalDir, profile));
        return Path.Combine(globalDir, PlayerProfileStore.FileName);
    }

    // ── G1: per-game isolation ─────────────────────────────────────────────

    [Fact]
    public void Native_TwoProjects_HaveSeparateProfiles_AndPersistOnReopen()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuidA = NewUuid(), uuidB = NewUuid();
        string rootA = NewProject(uuidA), rootB = NewProject(uuidB);
        string globalDir = NewGlobalDir();

        using (var host = Mount(rootA))
        {
            var loop = Open(host, rootA, globalDir);
            Assert.True(loop.UsesNativeStore);
            Assert.Equal(GameProfileDir(uuidA), loop.ProfileDirectory);
            loop.Profile.MasterVolume = 0.3f;
            loop.Profile.SkipMode = PlayerSkipMode.All;
            Assert.Null(loop.SavePreferences());
            loop.NotePresented(new[] { ReadA });
            Assert.Null(loop.Close());
        }

        using (var host = Mount(rootB))
        {
            var loop = Open(host, rootB, globalDir);
            Assert.Equal(GameProfileDir(uuidB), loop.ProfileDirectory);
            Assert.Equal(PlayerProfileLoadStatus.MissingDefaults, loop.LoadStatus);
            Assert.Equal(1f, loop.Profile.MasterVolume);
            Assert.Equal(PlayerSkipMode.ReadOnly, loop.Profile.SkipMode);
            Assert.False(loop.Profile.IsRead(ReadA));
            loop.Profile.MasterVolume = 0.6f;
            Assert.Null(loop.SavePreferences());
            loop.NotePresented(new[] { ReadB });
            Assert.Null(loop.Close());
        }

        using (var host = Mount(rootA))
        {
            var loop = Open(host, rootA, globalDir);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loop.LoadStatus);
            Assert.Equal(0.3f, loop.Profile.MasterVolume, 3);
            Assert.Equal(PlayerSkipMode.All, loop.Profile.SkipMode);
            Assert.True(loop.Profile.IsRead(ReadA));
            Assert.False(loop.Profile.IsRead(ReadB));
        }

        using (var host = Mount(rootB))
        {
            var loop = Open(host, rootB, globalDir);
            Assert.Equal(0.6f, loop.Profile.MasterVolume, 3);
            Assert.True(loop.Profile.IsRead(ReadB));
            Assert.False(loop.Profile.IsRead(ReadA));
        }

        // No write ever went to the legacy global directory.
        Assert.False(Directory.Exists(globalDir) && Directory.GetFiles(globalDir).Length > 0);
    }

    /// <summary>
    /// G1 acceptance, behaviour only (no knowledge of where files live):
    /// what the player changes in project A is invisible in project B and
    /// still there when A is reopened.
    /// </summary>
    [Fact]
    public void G1_ChangesInProjectA_AreNotVisibleInProjectB_AndSurviveReopen()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string rootA = NewProject(NewUuid()), rootB = NewProject(NewUuid());

        using (var host = Mount(rootA))
        {
            var loop = Open(host, rootA, NewGlobalDir());
            loop.Profile.MasterVolume = 0.25f;
            loop.Profile.SkipMode = PlayerSkipMode.Off;
            Assert.Null(loop.SavePreferences());
            loop.NotePresented(new[] { ReadA });
            Assert.Null(loop.Close());
        }

        using (var host = Mount(rootB))
        {
            var loop = Open(host, rootB, NewGlobalDir());
            Assert.Equal(1f, loop.Profile.MasterVolume);
            Assert.Equal(PlayerSkipMode.ReadOnly, loop.Profile.SkipMode);
            Assert.False(loop.Profile.IsRead(ReadA));
        }

        using (var host = Mount(rootA))
        {
            var loop = Open(host, rootA, NewGlobalDir());
            Assert.Equal(0.25f, loop.Profile.MasterVolume, 3);
            Assert.Equal(PlayerSkipMode.Off, loop.Profile.SkipMode);
            Assert.True(loop.Profile.IsRead(ReadA));
        }
    }

    [Fact]
    public void Managed_Fallback_TwoProjects_AreIsolated_PerGameDirectory()
    {
        string uuidA = NewUuid(), uuidB = NewUuid();
        string rootA = NewProject(uuidA), rootB = NewProject(uuidB);
        string globalDir = NewGlobalDir();
        using var idle = new EngineHost(); // never initialized -> no native store

        var a = Open(idle, rootA, globalDir);
        Assert.False(a.UsesNativeStore);
        Assert.Equal(Path.Combine(Path.GetDirectoryName(globalDir)!, "games", uuidA, "profile"), a.ProfileDirectory);
        a.Profile.MasterVolume = 0.2f;
        Assert.Null(a.SavePreferences());
        a.NotePresented(new[] { ReadA });
        Assert.Null(a.Flush());

        var b = Open(idle, rootB, globalDir);
        Assert.Equal(1f, b.Profile.MasterVolume);
        Assert.False(b.Profile.IsRead(ReadA));

        var aAgain = Open(idle, rootA, globalDir);
        Assert.Equal(0.2f, aAgain.Profile.MasterVolume, 3);
        Assert.True(aAgain.Profile.IsRead(ReadA));
        Assert.Equal(uuidA, aAgain.Profile.GameId);
        Assert.False(File.Exists(Path.Combine(globalDir, PlayerProfileStore.FileName)));
    }

    [Fact]
    public void Managed_Fallback_WithoutProject_UsesGlobalDirectory_AndAuthoredIdIsMissingForLooseProject()
    {
        string globalDir = NewGlobalDir();
        using var idle = new EngineHost();
        var none = Open(idle, null, globalDir);
        Assert.Equal(globalDir, none.ProfileDirectory);

        string loose = NewProject(null);
        var project = Open(idle, loose, globalDir);
        Assert.Equal(Path.Combine(loose, "profile"), project.ProfileDirectory);
    }

    // ── Parity with the native store ───────────────────────────────────────

    [Fact]
    public void Managed_LoadsGoldenFixture()
    {
        string fixture = FixturePath();
        string dir = Path.Combine(_scratch, "fixture");
        Directory.CreateDirectory(dir);
        File.Copy(fixture, Path.Combine(dir, PlayerProfileStore.FileName));
        var loaded = PlayerProfileStore.Load(dir);
        Assert.Equal(PlayerProfileLoadStatus.Loaded, loaded.Status);
        Assert.Null(loaded.Diagnostic);
        var p = loaded.Profile;
        Assert.Equal("tr", p.Language);
        Assert.Equal(0.5f, p.MasterVolume);
        Assert.Equal(0.75f, p.BgmVolume);
        Assert.Equal(1f, p.VoiceVolume);
        Assert.Equal(0.25f, p.SfxVolume);
        Assert.Equal(1f, p.AmbienceVolume);
        Assert.Equal(0.5f, p.UiVolume);
        Assert.Equal(1.5f, p.TextSpeedMultiplier);
        Assert.Equal(3f, p.AutoAdvanceDelay);
        Assert.Equal(1.25f, p.TextScale);
        Assert.True(p.HighContrast);
        Assert.False(p.ReducedMotion);
        Assert.Equal(PlayerSkipMode.All, p.SkipMode);
        Assert.True(p.AutoEnabled);
        Assert.Equal("EqualPower", p.MixerFadeCurve);
        Assert.Equal(12, p.SfxPoolDepth);
        Assert.Equal(2, p.ReadContentIds.Count);
        Assert.True(p.IsRead(ReadA));
        Assert.True(p.IsRead(ReadB));
    }

    [Fact]
    public void Managed_Save_PreservesGameId_Unlocks_AndUnknownKeys()
    {
        string dir = Path.Combine(_scratch, "preserve");
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, PlayerProfileStore.FileName);
        File.WriteAllText(path, """
            {
              "version": 1,
              "game_id": "5a3c9e10-2b64-4f7d-8a15-9c0d1e2f3a4b",
              "language": "en",
              "read_content_ids": ["0f0e0d0c-0b0a-0908-0706-050403020100"],
              "unlocks": [{"id": "gallery_1", "n": 2}, "cg_final", 3],
              "future_flag": {"nested": [1, 2, {"k": null}], "on": true},
              "another_key": "kept"
            }
            """);
        var before = JsonNode.Parse(File.ReadAllText(path))!.AsObject();

        var loop = PlayerLoopService.Load(dir);
        Assert.Equal(PlayerProfileLoadStatus.Loaded, loop.LoadStatus);
        loop.Profile.MasterVolume = 0.2f;
        loop.NotePresented(new[] { ReadB });
        Assert.Null(loop.SavePreferences());

        var after = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        Assert.Equal("5a3c9e10-2b64-4f7d-8a15-9c0d1e2f3a4b", (string?)after["game_id"]);
        Assert.True(JsonNode.DeepEquals(before["unlocks"], after["unlocks"]));
        Assert.True(JsonNode.DeepEquals(before["future_flag"], after["future_flag"]));
        Assert.Equal("kept", (string?)after["another_key"]);
        Assert.Equal(0.2, (double)after["master_volume"]!, 3);
        Assert.Equal(2, after["read_content_ids"]!.AsArray().Count);

        // A second managed rewrite is stable (idempotent key set).
        Assert.Null(loop.SavePreferences());
        var again = JsonNode.Parse(File.ReadAllText(path))!.AsObject();
        Assert.True(JsonNode.DeepEquals(after, again));
    }

    [Fact]
    public void Managed_Save_WithoutUnlocks_WritesEmptyArray_AsNativeDoes()
    {
        string dir = Path.Combine(_scratch, "emptyunlocks");
        Assert.Null(PlayerProfileStore.Save(dir, new PlayerProfile()));
        var doc = JsonNode.Parse(File.ReadAllText(Path.Combine(dir, PlayerProfileStore.FileName)))!.AsObject();
        Assert.Empty(doc["unlocks"]!.AsArray());
        Assert.False(doc.ContainsKey("game_id"));
    }

    [Fact]
    public void Managed_WrongGameFile_IsReadOnly_AndByteIdentical()
    {
        string dir = Path.Combine(_scratch, "wrongmanaged");
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, PlayerProfileStore.FileName);
        const string text = "{\"version\":1,\"game_id\":\"11111111-1111-1111-1111-111111111111\",\"read_content_ids\":[]}";
        File.WriteAllText(path, text);

        var loop = PlayerLoopService.Load(dir, "22222222-2222-2222-2222-222222222222");
        Assert.Equal(PlayerProfileLoadStatus.WrongGameDefaults, loop.LoadStatus);
        Assert.True(loop.IsReadOnly);
        Assert.False(string.IsNullOrEmpty(loop.LoadDiagnostic));
        loop.Profile.MasterVolume = 0.1f;
        loop.NotePresented(new[] { ReadA });
        Assert.Null(loop.SavePreferences());
        Assert.Null(loop.Close());
        Assert.Equal(text, File.ReadAllText(path));
    }

    [Fact]
    public void Native_WrittenFile_LoadsInManaged_WithGameIdAndUnlocks()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuid = NewUuid();
        string root = NewProject(uuid);
        using var host = Mount(root);
        var loop = Open(host, root, NewGlobalDir());
        loop.Profile.MasterVolume = 0.35f;
        loop.Profile.SkipMode = PlayerSkipMode.Off;
        loop.Profile.TextScale = 1.5f;
        loop.Profile.Language = "tr";
        loop.Profile.MixerFadeCurve = "EqualPower";
        Assert.Null(loop.SavePreferences());
        loop.NotePresented(new[] { ReadA, ReadB });
        Assert.Null(loop.Close());

        var managed = PlayerProfileStore.Load(loop.ProfileDirectory, uuid);
        Assert.Equal(PlayerProfileLoadStatus.Loaded, managed.Status);
        var p = managed.Profile;
        Assert.Equal(uuid, p.GameId);
        Assert.NotNull(p.Unlocks);
        Assert.Empty(p.Unlocks!.AsArray());
        Assert.Equal(0.35f, p.MasterVolume, 3);
        Assert.Equal(PlayerSkipMode.Off, p.SkipMode);
        Assert.Equal(1.5f, p.TextScale);
        Assert.Equal("tr", p.Language);
        Assert.Equal("EqualPower", p.MixerFadeCurve);
        Assert.True(p.IsRead(ReadA));
        Assert.True(p.IsRead(ReadB));
    }

    [Fact]
    public void Native_LoadsManagedWrittenFile_AndKeepsUnknownKeysOnRewrite()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuid = NewUuid();
        string root = NewProject(uuid);
        string dir = GameProfileDir(uuid);
        var seeded = new PlayerProfile { GameId = uuid, MasterVolume = 0.45f };
        seeded.MarkRead(ReadA);
        seeded.ExtraKeys["future_flag"] = JsonNode.Parse("{\"a\":[1,2,3]}");
        seeded.Unlocks = JsonNode.Parse("[\"cg_1\"]");
        Assert.Null(PlayerProfileStore.Save(dir, seeded));

        using (var host = Mount(root))
        {
            var loop = Open(host, root, NewGlobalDir());
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loop.LoadStatus);
            Assert.Equal(0.45f, loop.Profile.MasterVolume, 3);
            Assert.True(loop.Profile.IsRead(ReadA));
            loop.Profile.MasterVolume = 0.5f;
            Assert.Null(loop.SavePreferences());
        }

        var after = JsonNode.Parse(File.ReadAllText(Path.Combine(dir, PlayerProfileStore.FileName)))!.AsObject();
        Assert.Equal(uuid, (string?)after["game_id"]);
        Assert.Equal(0.5, (double)after["master_volume"]!, 3);
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse("{\"a\":[1,2,3]}"), after["future_flag"]));
        Assert.True(JsonNode.DeepEquals(JsonNode.Parse("[\"cg_1\"]"), after["unlocks"]));
    }

    // ── Read-only profiles stay untouched ──────────────────────────────────

    [Fact]
    public void Native_FutureVersionProfile_StaysByteIdentical_AndDiagnosticIsSurfaced()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuid = NewUuid();
        string root = NewProject(uuid);
        string dir = GameProfileDir(uuid);
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, PlayerProfileStore.FileName);
        File.WriteAllText(path, "{\"version\": 999, \"game_id\": \"" + uuid + "\", \"read_content_ids\": [\"keep-me\"], \"x\": 1}");
        byte[] original = File.ReadAllBytes(path);

        using (var host = Mount(root))
        {
            var loop = Open(host, root, NewGlobalDir());
            Assert.Equal(PlayerProfileLoadStatus.UnsupportedVersionDefaults, loop.LoadStatus);
            Assert.True(loop.IsReadOnly);
            Assert.False(string.IsNullOrEmpty(loop.LoadDiagnostic));

            // The player window flow: view model over this loop, preference toggles + reads.
            var engine = new EditorPlayerLoopSlice5Tests.FakePlayerEngine();
            var vm = new PlayerViewModel(engine, loop);
            vm.HandleCommand(PlayerInputCommand.ToggleAuto);
            vm.HandleCommand(PlayerInputCommand.CycleTextScale);
            vm.Profile.MasterVolume = 0.1f;
            vm.SavePreferences();
            Assert.True(vm.NewGame());
            vm.OnAdvanceInput();
            Assert.Equal(string.Empty, vm.LastError);
            Assert.Null(loop.Close());
        }

        Assert.Equal(original, File.ReadAllBytes(path));
        Assert.Equal(new[] { path }, Directory.GetFiles(dir)); // no temp, no quarantine copy
    }

    [Fact]
    public void Native_WrongGameProfile_IsReadOnly_AndByteIdentical()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuid = NewUuid();
        string root = NewProject(uuid);
        string dir = GameProfileDir(uuid);
        Directory.CreateDirectory(dir);
        string path = Path.Combine(dir, PlayerProfileStore.FileName);
        File.WriteAllText(path, "{\"version\":1,\"game_id\":\"99999999-9999-9999-9999-999999999999\",\"master_volume\":0.5}");
        byte[] original = File.ReadAllBytes(path);

        using (var host = Mount(root))
        {
            var loop = Open(host, root, NewGlobalDir());
            Assert.Equal(PlayerProfileLoadStatus.WrongGameDefaults, loop.LoadStatus);
            Assert.True(loop.IsReadOnly);
            loop.Profile.MasterVolume = 0.9f;
            Assert.Null(loop.SavePreferences());
            loop.NotePresented(new[] { ReadA });
            Assert.Null(loop.Close());
        }

        Assert.Equal(original, File.ReadAllBytes(path));
    }

    [Fact]
    public void Native_ProjectSwitchUnderOpenPlayer_DetachesInsteadOfWritingTheOtherGame()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuidA = NewUuid(), uuidB = NewUuid();
        string rootA = NewProject(uuidA), rootB = NewProject(uuidB);
        using var host = Mount(rootA);
        var loop = Open(host, rootA, NewGlobalDir());
        Assert.False(loop.IsReadOnly);

        host.EndSession();
        Assert.True(host.SetProjectDirectoryChecked(rootB, out string detail), detail);

        loop.Profile.MasterVolume = 0.1f;
        string? first = loop.SavePreferences();
        Assert.False(string.IsNullOrEmpty(first));
        Assert.True(loop.IsReadOnly);
        Assert.Null(loop.SavePreferences()); // reported once, silent afterwards
        loop.NotePresented(new[] { ReadA });
        Assert.Null(loop.Close());

        host.Dispose();
        Assert.False(File.Exists(Path.Combine(GameProfileDir(uuidB), PlayerProfileStore.FileName)));
    }

    // ── One-time migration of the old global editor profile ────────────────

    [Fact]
    public void Native_Migration_CopiesGlobalProfileOnce_IntoFirstProject_AndClaimsIt()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuidA = NewUuid(), uuidB = NewUuid();
        string rootA = NewProject(uuidA), rootB = NewProject(uuidB);
        string globalDir = NewGlobalDir();
        string globalFile = WriteGlobalProfile(globalDir);
        byte[] globalBytes = File.ReadAllBytes(globalFile);
        string marker = Path.Combine(globalDir, PlayerProfileSession.MigrationMarkerFileName);
        var log = new List<string>();

        using (var host = Mount(rootA))
        {
            var loop = Open(host, rootA, globalDir, log);
            Assert.Equal(0.4f, loop.Profile.MasterVolume, 3);
            Assert.Equal(PlayerSkipMode.All, loop.Profile.SkipMode);
            Assert.Equal("tr", loop.Profile.Language);
            Assert.True(loop.Profile.IsRead(ReadA));
            Assert.True(loop.Profile.IsRead(ReadB));
        }
        Assert.Contains(log, line => line.Contains("copied once") && line.Contains(uuidA));
        Assert.True(File.Exists(marker));
        var claim = JsonNode.Parse(File.ReadAllText(marker))!.AsObject();
        Assert.Equal(uuidA, (string?)claim["game_id"]);
        Assert.False(string.IsNullOrEmpty((string?)claim["time"]));
        Assert.Equal(2, (int)claim["read_count"]!);
        Assert.Equal(globalBytes, File.ReadAllBytes(globalFile));

        // Native stamped the new per-game file.
        var perGame = PlayerProfileStore.Load(GameProfileDir(uuidA), uuidA);
        Assert.Equal(PlayerProfileLoadStatus.Loaded, perGame.Status);
        Assert.Equal(uuidA, perGame.Profile.GameId);
        Assert.Equal(2, perGame.Profile.ReadContentIds.Count);
        byte[] markerBytes = File.ReadAllBytes(marker);

        // A second project is not seeded from the claimed global profile.
        using (var host = Mount(rootB))
        {
            var loop = Open(host, rootB, globalDir);
            Assert.Equal(PlayerProfileLoadStatus.MissingDefaults, loop.LoadStatus);
            Assert.Equal(1f, loop.Profile.MasterVolume);
            Assert.Empty(loop.Profile.ReadContentIds);
        }
        Assert.False(File.Exists(Path.Combine(GameProfileDir(uuidB), PlayerProfileStore.FileName)));

        // Reopening the first project reads its own file and does not migrate again.
        using (var host = Mount(rootA))
        {
            var loop = Open(host, rootA, globalDir);
            Assert.Equal(PlayerProfileLoadStatus.Loaded, loop.LoadStatus);
            Assert.Equal(0.4f, loop.Profile.MasterVolume, 3);
            Assert.True(loop.Profile.IsRead(ReadB));
        }
        Assert.Equal(markerBytes, File.ReadAllBytes(marker));
        Assert.Equal(globalBytes, File.ReadAllBytes(globalFile));
    }

    [Fact]
    public void Native_Migration_SkipsProjectsWithoutAuthoredId_AndKeepsGlobalUnclaimed()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string loose = NewProject(null);
        string globalDir = NewGlobalDir();
        string globalFile = WriteGlobalProfile(globalDir);
        byte[] globalBytes = File.ReadAllBytes(globalFile);

        using (var host = Mount(loose))
        {
            var loop = Open(host, loose, globalDir);
            Assert.Equal(PlayerProfileLoadStatus.MissingDefaults, loop.LoadStatus);
            Assert.Equal(1f, loop.Profile.MasterVolume);
        }
        Assert.False(File.Exists(Path.Combine(globalDir, PlayerProfileSession.MigrationMarkerFileName)));
        Assert.Equal(globalBytes, File.ReadAllBytes(globalFile));

        // The next project with an authored id can still claim it.
        string uuid = NewUuid();
        string root = NewProject(uuid);
        using (var host = Mount(root))
        {
            var loop = Open(host, root, globalDir);
            Assert.Equal(0.4f, loop.Profile.MasterVolume, 3);
        }
        Assert.True(File.Exists(Path.Combine(globalDir, PlayerProfileSession.MigrationMarkerFileName)));
    }

    [Fact]
    public void Native_Migration_AlreadyClaimedGlobal_IsNotCopied()
    {
        Assert.True(NativeProfileUsable(), "native player-profile capability is required");
        string uuid = NewUuid();
        string root = NewProject(uuid);
        string globalDir = NewGlobalDir();
        WriteGlobalProfile(globalDir);
        File.WriteAllText(Path.Combine(globalDir, PlayerProfileSession.MigrationMarkerFileName), "{\"game_id\":\"someone-else\"}");

        using var host = Mount(root);
        var loop = Open(host, root, globalDir);
        Assert.Equal(1f, loop.Profile.MasterVolume);
        Assert.Empty(loop.Profile.ReadContentIds);
    }

    [Fact]
    public void Managed_Migration_CopiesOnce_IntoFirstProject()
    {
        string uuidA = NewUuid(), uuidB = NewUuid();
        string rootA = NewProject(uuidA), rootB = NewProject(uuidB);
        string globalDir = NewGlobalDir();
        string globalFile = WriteGlobalProfile(globalDir);
        byte[] globalBytes = File.ReadAllBytes(globalFile);
        using var idle = new EngineHost();

        var a = Open(idle, rootA, globalDir);
        Assert.Equal(0.4f, a.Profile.MasterVolume, 3);
        Assert.True(a.Profile.IsRead(ReadA));
        var onDisk = PlayerProfileStore.Load(a.ProfileDirectory, uuidA);
        Assert.Equal(PlayerProfileLoadStatus.Loaded, onDisk.Status);
        Assert.Equal(uuidA, onDisk.Profile.GameId);

        var b = Open(idle, rootB, globalDir);
        Assert.Equal(1f, b.Profile.MasterVolume);
        Assert.Empty(b.Profile.ReadContentIds);
        Assert.Equal(globalBytes, File.ReadAllBytes(globalFile));
        Assert.True(File.Exists(Path.Combine(globalDir, PlayerProfileSession.MigrationMarkerFileName)));
    }
}
