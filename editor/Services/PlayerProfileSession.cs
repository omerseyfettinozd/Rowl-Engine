using System;
using System.Globalization;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Services;

/// <summary>
/// D01c-2 - opens the player profile of the project that is mounted in the
/// editor engine.
/// <list type="bullet">
/// <item>With the native player-profile capability and a mounted project the
/// native per-game store is the only writer (see
/// <see cref="NativePlayerProfileBackend"/>); the editor reads through
/// <c>RowlEngine_ProfileGetJson</c>.</item>
/// <item>Without it (no engine, older library, nothing mounted) the managed
/// <see cref="PlayerProfileStore"/> is used, but still per game: the same
/// <c>games/&lt;game-id&gt;/profile</c> layout the native side resolves, or
/// <c>&lt;project&gt;/profile</c> for a project without an authored id. The old
/// global <c>profiles</c> directory is used only when no project is known.</item>
/// <item>The old single global editor profile is copied once into the first
/// project (with an authored id) whose player is opened, see
/// <see cref="TryMigrateGlobalProfile"/>.</item>
/// </list>
/// </summary>
internal static class PlayerProfileSession
{
    /// <summary>Claim marker written next to the legacy global profile.</summary>
    public const string MigrationMarkerFileName = "player-profile.migrated.json";

    /// <summary>
    /// Opens the profile for the player window. <paramref name="projectRoot"/>
    /// is the editor's current project (used only by the managed fallback);
    /// <paramref name="globalProfileDirectory"/> overrides the legacy global
    /// directory (tests).
    /// </summary>
    internal static PlayerLoopService Open(
        EngineHost host,
        string? projectRoot,
        Action<string>? log = null,
        string? globalProfileDirectory = null)
    {
        ArgumentNullException.ThrowIfNull(host);
        log ??= static _ => { };
        string globalDirectory = globalProfileDirectory ?? PlayerProfileStore.ResolveDefaultProfileDirectory();

        var native = NativePlayerProfileBackend.TryCreate(host);
        if (native is not null)
        {
            var opened = native.Open();
            var nativeLoop = PlayerLoopService.LoadFromBackend(
                native, native.ProfileDirectory, opened.Profile, opened.Status, opened.Diagnostic, opened.ReadOnly);
            log($"[Profile] Native per-game player profile: {native.ProfileDirectory} ({opened.Status}).");
            bool authored = opened.Identity.GameId.Length > 0
                && opened.Identity.Source is GameIdentitySource.Package or GameIdentitySource.Manifest;
            TryMigrateGlobalProfile(nativeLoop, authored ? opened.Identity.GameId : null, globalDirectory, log);
            return nativeLoop;
        }

        var (directory, gameId) = ResolveManagedLocation(projectRoot, globalDirectory);
        var loop = PlayerLoopService.Load(directory, gameId);
        log($"[Profile] Managed player profile (native store unavailable): {directory} ({loop.LoadStatus}).");
        TryMigrateGlobalProfile(loop, gameId, globalDirectory, log);
        return loop;
    }

    /// <summary>
    /// Managed mirror of the native layout for a project:
    /// <c>&lt;user-data&gt;/games/&lt;project_uuid&gt;/profile</c>,
    /// <c>&lt;project&gt;/profile</c> without an authored id, the legacy global
    /// directory when no project is known.
    /// </summary>
    internal static (string Directory, string? GameId) ResolveManagedLocation(string? projectRoot, string globalDirectory)
    {
        if (string.IsNullOrWhiteSpace(projectRoot))
            return (globalDirectory, null);
        string manifest = Path.Combine(projectRoot, "project.rowlproj");
        string? uuid = File.Exists(manifest) ? ProjectIdentityService.TryReadStoredUuid(manifest, out _) : null;
        if (uuid is null)
            return (Path.Combine(projectRoot, "profile"), null);
        string userData = Path.GetDirectoryName(globalDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar))
            ?? globalDirectory;
        return (Path.Combine(userData, "games", uuid, "profile"), uuid);
    }

    /// <summary>
    /// One-time copy of the legacy global editor profile (preferences + read
    /// set) into <paramref name="loop"/>, the profile of the first project with
    /// an authored <paramref name="gameId"/> that opens its player. Conditions:
    /// the project has no profile yet, is writable, and the global file exists,
    /// parses cleanly and is not claimed yet. A claim marker
    /// (<see cref="MigrationMarkerFileName"/>) is written next to the global
    /// file first so no other project inherits it; if the copy then fails the
    /// marker is removed again. The global file itself is never modified.
    /// Returns true when the profile was migrated.
    /// </summary>
    internal static bool TryMigrateGlobalProfile(
        PlayerLoopService loop, string? gameId, string globalDirectory, Action<string> log)
    {
        if (string.IsNullOrEmpty(gameId)
            || loop.LoadStatus != PlayerProfileLoadStatus.MissingDefaults
            || loop.IsReadOnly)
            return false;
        string globalFile = Path.Combine(globalDirectory, PlayerProfileStore.FileName);
        string marker = Path.Combine(globalDirectory, MigrationMarkerFileName);
        if (!File.Exists(globalFile) || File.Exists(marker))
            return false;

        var legacy = PlayerProfileStore.Load(globalDirectory);
        if (legacy.Status != PlayerProfileLoadStatus.Loaded)
        {
            log($"[Profile] Legacy global profile not migrated ({legacy.Status}): {legacy.Diagnostic}");
            return false;
        }

        try
        {
            var claim = new
            {
                format = "rowl.player-profile-migration",
                game_id = gameId,
                time = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture),
                read_count = legacy.Profile.ReadContentIds.Count,
            };
            ProjectFileSystem.WriteAllTextAtomically(
                marker, JsonSerializer.Serialize(claim, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            log($"[Profile] Legacy global profile not migrated (claim marker could not be written: {ex.Message}).");
            return false;
        }

        loop.Profile.CopyPreferencesFrom(legacy.Profile);
        loop.NotePresented(legacy.Profile.ReadContentIds);
        string? error = loop.Flush() ?? loop.SavePreferences() ?? loop.Close();
        if (error is not null)
        {
            try { File.Delete(marker); } catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
            log($"[Profile] Legacy global profile migration failed and was not claimed: {error}");
            return false;
        }

        log($"[Profile] Legacy global player profile copied once into game {gameId} "
            + $"({legacy.Profile.ReadContentIds.Count} read id(s)); claimed by {marker}. The global file is untouched.");
        return true;
    }
}
