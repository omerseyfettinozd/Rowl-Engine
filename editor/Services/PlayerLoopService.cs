using System;
using System.IO;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 2 — save-slot independent read tracking and skip stepping
/// for one player profile.
/// <list type="bullet">
/// <item><see cref="AdvanceAndTrack"/> snapshots the presented content ids
/// <em>before</em> advancing (the line being left is the line just read),
/// advances, records them via <see cref="PlayerProfile.MarkRead"/>, and
/// atomically persists the profile.</item>
/// <item><see cref="TrySkipStep"/> evaluates <see cref="SkipGate"/> against
/// the current presentation and advances exactly once when skipping is
/// allowed. Continuous auto-skip belongs to the Faz 2 Playing-state loop;
/// this slice delivers the single-step gate the loop will drive.</item>
/// </list>
/// The engine is reached only through caller-supplied seams
/// (<paramref name="getActiveIds"/>, <paramref name="advance"/>) so the
/// tracking and gate logic is fully unit-testable without native code.
/// Production wiring passes <c>EngineHost</c> lambdas
/// (see <c>EngineHost.AdvancePlayerLoop</c>).
/// </summary>
public sealed class PlayerLoopService
{
    public PlayerProfile Profile { get; }

    /// <summary>Directory the profile persists to.</summary>
    public string ProfileDirectory => _profileDirectory;

    /// <summary>How the profile file resolved at load time.</summary>
    public PlayerProfileLoadStatus LoadStatus { get; private set; } = PlayerProfileLoadStatus.Loaded;

    /// <summary>Load diagnostic (corrupt / newer-version file); the caller should log or show it.</summary>
    public string? LoadDiagnostic { get; private set; }

    /// <summary>
    /// True when the profile must never be written: a newer-schema file, a file
    /// of another game, an unreadable file, or (native store) a project that
    /// changed under an open player.
    /// </summary>
    public bool IsReadOnly => _readOnly || (_backend is not null && !_backend.IsWritable);

    /// <summary>True when the native store owns the file (single-writer rule).</summary>
    public bool UsesNativeStore => _backend is not null;

    private readonly string _profileDirectory;
    private readonly IPlayerProfileBackend? _backend;
    private bool _dirty;
    // Daha yeni sürüm / başka oyun profili: okunur-yalnız, üzerine yazılmaz (veri kaybı olmasın).
    private bool _readOnly;
    // Per-game directories stamp the owning game into freshly written managed files.
    private string _gameId = string.Empty;
    // Store failures seen while marking reads; reported by the next flush.
    private string? _pendingBackendError;

    public PlayerLoopService(PlayerProfile profile, string profileDirectory)
        : this(profile, profileDirectory, null)
    {
    }

    internal PlayerLoopService(PlayerProfile profile, string profileDirectory, IPlayerProfileBackend? backend)
    {
        Profile = profile ?? throw new ArgumentNullException(nameof(profile));
        _profileDirectory = !string.IsNullOrWhiteSpace(profileDirectory)
            ? profileDirectory
            : throw new ArgumentException("A profile directory is required.", nameof(profileDirectory));
        _backend = backend;
    }

    /// <summary>
    /// Loads (or defaults) the profile for a directory with the managed store.
    /// <paramref name="gameId"/> (per-game directories) enforces and stamps the
    /// owning game: a file of another game is read-only and never written.
    /// </summary>
    public static PlayerLoopService Load(string profileDirectory, string? gameId = null)
    {
        var loaded = PlayerProfileStore.Load(profileDirectory, gameId);
        var service = new PlayerLoopService(loaded.Profile, profileDirectory)
        {
            LoadStatus = loaded.Status,
            LoadDiagnostic = loaded.Diagnostic,
            _gameId = gameId ?? string.Empty,
        };
        string path = Path.Combine(profileDirectory, PlayerProfileStore.FileName);
        if (loaded.Status is PlayerProfileLoadStatus.UnsupportedVersionDefaults or PlayerProfileLoadStatus.WrongGameDefaults)
        {
            service._readOnly = true;
        }
        else if (loaded.Status == PlayerProfileLoadStatus.CorruptDefaults && SettingsFileBackup.CanRead(path))
        {
            // Bozuk ama okunabilen dosya ilk Flush'ta varsayılanlarla ezilmeden yedeklenir.
            string? backup = SettingsFileBackup.TryMoveCorrupt(path);
            if (backup is not null)
                service.LoadDiagnostic = loaded.Diagnostic + $" Old file kept as '{backup}'.";
        }
        return service;
    }

    /// <summary>
    /// D01c-2 - builds a loop over a store the native engine owns. The already
    /// parsed profile and the store's own status (not the file) drive the
    /// load status, the diagnostic and the read-only decision.
    /// </summary>
    internal static PlayerLoopService LoadFromBackend(
        IPlayerProfileBackend backend,
        string profileDirectory,
        PlayerProfile profile,
        PlayerProfileLoadStatus status,
        string? diagnostic,
        bool readOnly)
    {
        return new PlayerLoopService(profile, profileDirectory, backend)
        {
            LoadStatus = status,
            LoadDiagnostic = string.IsNullOrWhiteSpace(diagnostic) ? null : diagnostic,
            _readOnly = readOnly,
        };
    }

    /// <summary>
    /// Records presented content ids as read. Invalid/empty ids are ignored
    /// (fail closed). Returns how many ids were newly added.
    /// </summary>
    public int NotePresented(IEnumerable<string?> activeContentIds)
    {
        ArgumentNullException.ThrowIfNull(activeContentIds);
        int added = 0;
        List<string>? fresh = _backend is null ? null : new List<string>();
        foreach (string? id in activeContentIds)
        {
            if (Profile.MarkRead(id))
            {
                added++;
                if (fresh is not null && ContentIdService.Normalize(id) is string normalized)
                    fresh.Add(normalized);
            }
        }
        if (added > 0)
            _dirty = true;
        if (fresh is { Count: > 0 } && !_readOnly)
            _pendingBackendError ??= _backend!.MarkRead(fresh);
        return added;
    }

    /// <summary>
    /// Persists new reads. Managed store: atomic write when reads arrived since
    /// the last flush. Native store: reads were already handed to the engine
    /// (which writes them ~2 s later, on <see cref="Close"/> and on shutdown),
    /// so this only reports a store failure seen while marking. Returns null on
    /// success (or when nothing changed), otherwise a structured error the
    /// caller can log or toast.
    /// </summary>
    public string? Flush()
    {
        if (_pendingBackendError is not null)
        {
            string error = _pendingBackendError;
            _pendingBackendError = null;
            return error;
        }
        if (!_dirty || _readOnly)
            return null;
        if (_backend is not null)
            return null;
        string? saveError = PlayerProfileStore.Save(_profileDirectory, StampedProfile());
        if (saveError is null)
            _dirty = false;
        return saveError;
    }

    /// <summary>
    /// Persists the preference fields and writes the file now (explicit save
    /// point: preferences screen, toggles). A read-only profile is never
    /// written; its diagnostic was surfaced at load time. Returns null on
    /// success or a structured error.
    /// </summary>
    public string? SavePreferences()
    {
        // The native backend decides for itself (read-only store, project changed).
        if (_readOnly)
            return null;
        string? error = _backend is null
            ? PlayerProfileStore.Save(_profileDirectory, StampedProfile())
            : _backend.SavePreferences(Profile);
        if (error is null)
            _dirty = false;
        return error;
    }

    /// <summary>
    /// Final save point (player window closed): writes pending changes now.
    /// Idempotent; a read-only profile is never written.
    /// </summary>
    public string? Close()
    {
        if (_readOnly)
            return null;
        if (_backend is null)
            return Flush();
        string? error = _backend.Flush();
        if (error is null)
            _dirty = false;
        return error ?? Flush();
    }

    private PlayerProfile StampedProfile()
    {
        if (_gameId.Length > 0 && string.IsNullOrEmpty(Profile.GameId))
            Profile.GameId = _gameId;
        return Profile;
    }

    /// <summary>Evaluates the skip gate for the current presentation.</summary>
    public bool ShouldSkip(IReadOnlyList<string?> activeContentIds, bool hasChoices) =>
        SkipGate.ShouldSkip(Profile.SkipMode, activeContentIds, Profile.IsRead, hasChoices);

    /// <summary>
    /// Advances once and tracks the departed line as read.
    /// Returns a save error when persistence failed, null otherwise.
    /// </summary>
    public string? AdvanceAndTrack(
        Func<IReadOnlyList<string?>> getActiveIds,
        Action<uint> advance,
        uint choiceIndex = 0)
    {
        ArgumentNullException.ThrowIfNull(getActiveIds);
        ArgumentNullException.ThrowIfNull(advance);
        IReadOnlyList<string?> presented;
        try
        {
            presented = getActiveIds() ?? Array.Empty<string?>();
        }
        catch (Exception queryFailure)
        {
            return $"Active content ids could not be queried ({queryFailure.GetType().Name}: {queryFailure.Message}); advance aborted before tracking.";
        }
        advance(choiceIndex);
        NotePresented(presented);
        return Flush();
    }

    /// <summary>
    /// Performs a single skip step when the gate allows it.
    /// Returns true when an advance happened.
    /// </summary>
    public bool TrySkipStep(
        Func<IReadOnlyList<string?>> getActiveIds,
        Func<bool> hasChoices,
        Action<uint> advance,
        out string? saveError)
    {
        ArgumentNullException.ThrowIfNull(getActiveIds);
        ArgumentNullException.ThrowIfNull(hasChoices);
        ArgumentNullException.ThrowIfNull(advance);
        saveError = null;
        IReadOnlyList<string?> presented = getActiveIds() ?? Array.Empty<string?>();
        if (!ShouldSkip(presented, hasChoices()))
            return false;
        advance(0);
        NotePresented(presented);
        saveError = Flush();
        return true;
    }
}
